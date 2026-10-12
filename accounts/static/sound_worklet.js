// sound_worklet.js: the part of the player that runs with the browser's sound
//
// sound.js says what the player is for and hands this each packet as it
// comes. This is where the packets become the samples the browser plays. It
// runs in the browser's sound thread, which asks it for 128 samples at a
// time, 375 times a second, and must be given them at once.
//
// WHAT WAS WRONG, AND WAS HEARD AS SHOT NOISE
//
// Measured on 2026-10-12, the author's radio in Bangalore heard through the
// service in New York by a laptop in the same house:
//
//   at the radio      50.01 packets a second, none missing, never more than
//                     30 ms apart, and no step in the sound at their edges
//   at the service    between 1 in 170 and 1 in 16 missing, and a dozen
//                     waits of 120 to 255 ms in forty seconds
//   at the browser    the same, and fifty waits of over 100 ms, some of half
//                     a second, in forty seconds: the WebSocket is TCP, and
//                     when a packet of its own is lost everything behind it
//                     waits for it to be sent again
//
// The player kept a tenth of a second in hand. Every wait longer than that
// left the sound cut off in mid wave and begun again in mid wave, and every
// missing packet joined two that did not belong together. Each was a click.
//
// WHAT IS DONE ABOUT IT
//
// The samples wait in a queue, and playing does not begin until `target`
// of them are there, LEAST to begin with.
//
// - When the queue runs dry, the last sample is let fall away to nothing
//   over a few milliseconds, the sound waits for the queue to fill again and
//   comes back up as smoothly, and target is raised by STEP_UP, to MOST at
//   the most: a line that stalls often is listened to further behind.
// - When there has been no running dry for some seconds target comes down
//   again, a little at a time, to LEAST.
// - When more is waiting than target asks for, one sample in a hundred is
//   left out until it is not: the sound runs one part in a hundred fast,
//   which is not heard, and what is heard is never far behind the radio. The
//   same keeps this browser's clock and the radio's from drifting apart.
//   When far too much is waiting, after a long stall, the oldest is thrown
//   away at once.
// - When the service says packets were lost, the gap is filled with the
//   packet before it, quieter each time it is used, the joins faded; a gap
//   of more than CONCEALED_MOST packets is left a gap, with its edges faded.
//
// Nothing here mends the line: a packet that is lost is lost. It keeps the
// losses from being heard as clicks, at the cost of the sound being a
// little later, LEAST at the best.
//
// BRINGING THE SAMPLES UP TO 48000
//
// The sound comes at 8000 samples a second, a byte of mu-law each; this
// runs only where the browser's sound is at 48000. Six samples are made of
// each one through one filter that runs on from packet to packet:
// TAPS_A_PHASE * UP taps, a sinc cut at 4 kHz under a Blackman window. It
// passes the speech, which the radio has held under 3.6 kHz, and stops the
// copies of it that begin at 4.4 kHz. Handing the browser each packet "at
// 8000" to bring up by itself, which is what was done at first, measured 24
// dB of tone over all else at 1 kHz and 11 dB at 2.5 kHz; this measures 36
// to 38 dB, which is all that mu-law allows.

var RATE = 48000, UP = 6, TAPS_A_PHASE = 48;
var LEAST = 0.12 * RATE, MOST = 0.8 * RATE, STEP_UP = 0.08 * RATE, STEP_DOWN = 0.01 * RATE;
var SPARE = 0.03 * RATE;     // this much over target is let be
var FAR_TOO_MUCH = RATE;     // this much over target is thrown away at once
var WINDOW = 2 * RATE;       // how often the queue is looked at
var CALM_WINDOWS = 5;        // so many windows without running dry bring target down
var FADE = 240;              // samples over which sound comes and goes: 5 ms
var FALL = 0.99;             // what is held when the queue is dry falls by this each sample
var CONCEALED_MOST = 3, CONCEALED_GAIN = 0.6;
var QUEUE = 4 * RATE;

class RadioSound extends AudioWorkletProcessor {
  constructor() {
    super();
    var b, u, size, p, j, n, t, tap, sum;
    var count = UP * TAPS_A_PHASE, middle = (count - 1) / 2;

    this.linear = new Float32Array(256);
    for (b = 0; b < 256; b++) {
      u = ~b & 255;
      size = (((u & 15) << 3) + 132) << ((u & 112) >> 4);
      this.linear[b] = ((u & 128) ? (132 - size) : (size - 132)) / 32768;
    }
    // phases[p][j] multiplies the sample j back from the newest, for the
    // p-th of the six that each sample becomes
    this.phases = [];
    for (p = 0; p < UP; p++) {
      this.phases.push(new Float32Array(TAPS_A_PHASE));
      sum = 0;
      for (j = 0; j < TAPS_A_PHASE; j++) {
        n = p + UP * j;
        t = (n - middle) / UP;
        tap = (t === 0 ? 1 : Math.sin(Math.PI * t) / (Math.PI * t))
          * (0.42 - 0.5 * Math.cos(2 * Math.PI * n / (count - 1))
            + 0.08 * Math.cos(4 * Math.PI * n / (count - 1)));
        this.phases[p][j] = tap;
        sum += tap;
      }
      for (j = 0; j < TAPS_A_PHASE; j++) { this.phases[p][j] /= sum; }
    }
    this.past = new Float32Array(TAPS_A_PHASE);  // the last samples in, going round
    this.newest = 0;

    this.queue = new Float32Array(QUEUE);        // going round: `waiting` samples from `next`
    this.next = 0;
    this.waiting = 0;
    this.target = LEAST;
    this.isPlaying = false;
    this.held = 0;          // the last sample played, falling away while nothing plays
    this.risen = FADE;      // how far the sound has come back up since it last began
    this.before = null;     // the packet before, as samples, to fill a gap with
    this.isAfterGap = false;
    this.lowest = 0;        // the least that waited in this window
    this.looked = 0;        // samples played since the queue was last looked at
    this.calm = 0;
    this.isHurried = false;
    this.ranDry = 0;

    var self = this;
    this.port.onmessage = function (message) {
      if (message.data.lost) {
        self.conceal(message.data.lost);
      } else {
        self.take(new Uint8Array(message.data.sound));
      }
    };
  }

  // a packet's bytes, as six times as many samples
  broughtUp(bytes) {
    var out = new Float32Array(bytes.length * UP), i, p, j, at, sum, taps;
    for (i = 0; i < bytes.length; i++) {
      this.newest = (this.newest + 1) % TAPS_A_PHASE;
      this.past[this.newest] = this.linear[bytes[i]];
      for (p = 0; p < UP; p++) {
        taps = this.phases[p];
        sum = 0;
        at = this.newest;
        for (j = 0; j < TAPS_A_PHASE; j++) {
          sum += taps[j] * this.past[at];
          at = at === 0 ? TAPS_A_PHASE - 1 : at - 1;
        }
        out[i * UP + p] = sum;
      }
    }
    return out;
  }

  append(samples) {
    var i, at = (this.next + this.waiting) % QUEUE;
    for (i = 0; i < samples.length; i++) {
      this.queue[at] = samples[i];
      at = at + 1 === QUEUE ? 0 : at + 1;
    }
    this.waiting += samples.length;
    if (this.waiting > QUEUE) { this.discard(this.waiting - QUEUE); }
    if (this.waiting > this.target + FAR_TOO_MUCH) {
      this.discard(this.waiting - this.target);
      this.risen = 0;
    }
  }

  discard(count) {
    this.next = (this.next + count) % QUEUE;
    this.waiting -= count;
  }

  // the end of what is waiting is brought down to nothing
  fadeEnd() {
    var count = Math.min(FADE, this.waiting), i, at;
    for (i = 0; i < count; i++) {
      at = (this.next + this.waiting - 1 - i) % QUEUE;
      this.queue[at] *= i / count;
    }
  }

  take(bytes) {
    var samples = this.broughtUp(bytes), i;
    this.before = samples.slice();
    if (this.isAfterGap) {
      for (i = 0; i < FADE; i++) { samples[i] *= i / FADE; }
      this.isAfterGap = false;
    }
    this.append(samples);
  }

  conceal(lost) {
    var k, i, piece, loudness = 1;
    this.fadeEnd();
    this.isAfterGap = true;
    // the filter forgets what came before the gap: carried across, it
    // would join the two sides with a step of its own
    this.past.fill(0);
    if (!this.before || lost > CONCEALED_MOST) { return; }
    for (k = 0; k < lost; k++) {
      loudness *= CONCEALED_GAIN;
      piece = this.before.slice();
      for (i = 0; i < piece.length; i++) { piece[i] *= loudness; }
      for (i = 0; i < FADE; i++) {
        piece[i] *= i / FADE;
        piece[piece.length - 1 - i] *= i / FADE;
      }
      this.append(piece);
    }
  }

  // every WINDOW samples: is more waiting than need be, and has the line
  // been steady long enough to listen nearer to it
  look() {
    this.isHurried = this.lowest > this.target + SPARE;
    this.calm++;
    if (this.calm >= CALM_WINDOWS && this.target > LEAST) {
      this.target = Math.max(LEAST, this.target - STEP_DOWN);
    }
    this.lowest = this.waiting;
    this.looked = 0;
  }

  process(inputs, outputs) {
    var out = outputs[0][0], i, sample, weight;
    for (i = 0; i < out.length; i++) {
      if (!this.isPlaying && this.waiting >= this.target) {
        this.isPlaying = true;
        this.risen = 0;
        this.lowest = this.waiting;
        this.looked = 0;
      }
      if (this.isPlaying && this.waiting === 0) {
        this.isPlaying = false;
        this.ranDry++;
        this.calm = 0;
        this.isHurried = false;
        this.target = Math.min(MOST, this.target + STEP_UP);
      }
      if (!this.isPlaying) {
        this.held *= FALL;
        out[i] = this.held;
        continue;
      }
      sample = this.queue[this.next];
      this.discard(1);
      if (this.isHurried && this.looked % 100 === 99 && this.waiting > 0) { this.discard(1); }
      if (this.risen < FADE) {
        weight = this.risen / FADE;
        this.held *= FALL;
        out[i] = sample * weight + this.held * (1 - weight);
        this.risen++;
      } else {
        out[i] = sample;
        this.held = sample;
      }
      if (this.waiting < this.lowest) { this.lowest = this.waiting; }
      this.looked++;
      if (this.looked >= WINDOW) { this.look(); }
    }
    return true;
  }
}

registerProcessor("radio-sound", RadioSound);
