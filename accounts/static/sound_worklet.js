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
// - The radio's clock and this browser's are two clocks. The radio was
//   measured making 8001.6 samples a second by the laptop's reckoning; left
//   alone, the queue would slowly fill, or with the difference the other
//   way slowly empty and run dry, however much was kept in hand. So the
//   queue is not read a sample at a time but at a pace, a little over or
//   under one, from a place between two samples, and what is played is on
//   the straight line between them; the samples are six times as many as
//   the sound needs, and between neighbours a straight line is as good as
//   true. The pace is set once a second from the least that waited over
//   the last LOOKS seconds: more than FLOOR, and the queue is read up to 3
//   parts in a hundred fast; less, up to 1 slow. The pitch moves by as
//   much, which is not heard.
//
//   This was first done by leaving out one sample in a hundred. That is a
//   step in the wave 480 times a second, and the author heard it as a
//   crackle and named the cause: clocks that differ, and buffers copied
//   one sample out.
// - The same pace fits the queue, which is at 48000, to a browser whose
//   sound runs at some other rate.
// - When far too much is waiting, after a long stall, the oldest is thrown
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
// The sound comes at 8000 samples a second, a byte of mu-law each. Six samples are made of
// each one through one filter that runs on from packet to packet:
// TAPS_A_PHASE * UP taps, a sinc cut at 4 kHz under a Blackman window. It
// passes the speech, which the radio has held under 3.6 kHz, and stops the
// copies of it that begin at 4.4 kHz. Handing the browser each packet "at
// 8000" to bring up by itself, which is what was done at first, measured 24
// dB of tone over all else at 1 kHz and 11 dB at 2.5 kHz; this measures 36
// to 38 dB, which is all that mu-law allows.

var RATE = 48000, UP = 6, TAPS_A_PHASE = 48;
var LEAST = 0.12 * RATE, MOST = 0.8 * RATE, STEP_UP = 0.08 * RATE, STEP_DOWN = 0.01 * RATE;
var FLOOR = 0.05 * RATE;     // the least that should ever be waiting, as the pace aims
var LOOKS = 8;               // the pace goes by the least that waited over so many looks
var PACE_A_SECOND = 0.1;     // a second too much waiting quickens the pace by this
var FASTEST = 1.03, SLOWEST = 0.99;
var FAR_TOO_MUCH = RATE;     // this much over target is thrown away at once
var WINDOW = RATE;           // how often the queue is looked at, in samples played
var CALM_WINDOWS = 10;       // so many looks without running dry bring target down
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
    this.between = 0;       // how far the place being played is past the sample at `next`
    this.pace = 1;          // how fast the queue is read: 1 is as the radio's clock goes
    this.lowest = 0;        // the least that waited since the last look
    this.lows = [];         // and at each of the last LOOKS looks
    this.looked = 0;        // samples played since the queue was last looked at
    this.calm = 0;
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
      this.lowest = this.waiting;
      this.lows = [];
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

  // every WINDOW samples: is more waiting than need be, or less, and has
  // the line been steady long enough to listen nearer to it
  look() {
    var least, i;
    this.lows.push(this.lowest);
    if (this.lows.length > LOOKS) { this.lows.shift(); }
    least = this.lows[0];
    for (i = 1; i < this.lows.length; i++) { least = Math.min(least, this.lows[i]); }
    this.pace = Math.max(SLOWEST, Math.min(FASTEST,
      1 + (least - FLOOR) / RATE * PACE_A_SECOND));
    this.calm++;
    if (this.calm >= CALM_WINDOWS && this.target > LEAST) {
      this.target = Math.max(LEAST, this.target - STEP_DOWN);
    }
    this.lowest = this.waiting;
    this.looked = 0;
  }

  process(inputs, outputs) {
    var out = outputs[0][0], i, sample, weight, after, passed;
    // sampleRate is the browser's, given to every worklet
    var step = this.pace * RATE / sampleRate;
    for (i = 0; i < out.length; i++) {
      if (!this.isPlaying && this.waiting >= this.target) {
        this.isPlaying = true;
        this.risen = 0;
        this.between = 0;
        this.pace = 1;
        step = RATE / sampleRate;
        this.lowest = this.waiting;
        this.lows = [];
        this.looked = 0;
      }
      // two are needed, to play what lies between them
      if (this.isPlaying && this.waiting < 2) {
        this.isPlaying = false;
        this.ranDry++;
        this.calm = 0;
        this.discard(this.waiting);
        this.target = Math.min(MOST, this.target + STEP_UP);
      }
      if (!this.isPlaying) {
        this.held *= FALL;
        out[i] = this.held;
        continue;
      }
      after = this.next + 1 === QUEUE ? 0 : this.next + 1;
      sample = this.queue[this.next]
        + (this.queue[after] - this.queue[this.next]) * this.between;
      this.between += step;
      passed = Math.floor(this.between);
      this.between -= passed;
      this.discard(Math.min(passed, this.waiting));
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
      if (this.looked >= WINDOW) {
        this.look();
        step = this.pace * RATE / sampleRate;
      }
    }
    return true;
  }
}

registerProcessor("radio-sound", RadioSound);
