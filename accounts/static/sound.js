// sound.js: plays a radio's sound, as it comes from /listen, in a browser
//
// The globe (/map) and the owner's web client (/operate) both use this.
//
// WHAT COMES IN
//
// The sound is as the radio makes it: 8000 samples a second, each one byte
// of mu-law (RTP's payload type 0), 160 of them to a packet, fifty packets a
// second. play() is given each packet's bytes.
//
// WHY THE SAMPLES ARE BROUGHT UP TO 48000 HERE
//
// A browser's sound runs at 48000 samples a second, or thereabouts. At
// first each packet was handed to the browser as a little buffer "at 8000",
// for the browser to bring up to its own rate. It does that to each buffer
// by itself, knowing nothing of the packet before or the one after, so its
// filter starts and ends on silence fifty times a second. That was heard: a
// grain on the sound, taken by the author for quantization noise. A 1 kHz
// tone played that way in Chrome measured 24 dB above everything that was
// not the tone.
//
// So the samples are brought up here instead, six for each one, through one
// filter that runs on from packet to packet and remembers the last samples
// of the one before. The browser is given buffers at its own rate and has
// only to play them end to end. The same tone then measures 37 dB, which is
// what mu-law itself allows: eight bits a sample cannot do better, and the
// rest of the grain will stay for as long as the sound is sent as payload
// type 0.
//
// THE FILTER
//
// TAPS_A_PHASE * UP taps, a sinc cut at 4 kHz under a Blackman window: it
// passes the speech, which the radio has already held under 3.6 kHz, and
// stops the copies of it that putting five zeros between samples leaves
// from 4.4 kHz up. It delays the sound by 3 ms.
//
// A browser that will not run at 48000 gets the packets the old way.
//
// WHEN PACKETS ARE LATE OR MANY
//
// Sound is laid down a tenth of a second ahead of now. If the network holds
// packets back and then lets them all go, those that would put the sound
// more than half a second behind are thrown away; nearly live matters more
// than whole.

function radioSound(given) {
  "use strict";

  var RATE_IN = 8000, UP = 6, TAPS_A_PHASE = 48;
  var AHEAD = 0.1, TOO_LATE = 0.5;

  var linear = new Float32Array(256);
  (function () {
    for (var b = 0; b < 256; b++) {
      var u = ~b & 255, size = (((u & 15) << 3) + 132) << ((u & 112) >> 4);
      linear[b] = ((u & 128) ? (132 - size) : (size - 132)) / 32768;
    }
  })();

  // phases[p][j] multiplies the sample j back from the newest, for the p-th
  // of the six that each sample becomes
  var phases = [];
  (function () {
    var count = UP * TAPS_A_PHASE, middle = (count - 1) / 2, p, j, n, t, tap, sum;
    for (p = 0; p < UP; p++) {
      phases.push(new Float32Array(TAPS_A_PHASE));
      sum = 0;
      for (j = 0; j < TAPS_A_PHASE; j++) {
        n = p + UP * j;
        t = (n - middle) / UP;
        tap = (t === 0 ? 1 : Math.sin(Math.PI * t) / (Math.PI * t))
          * (0.42 - 0.5 * Math.cos(2 * Math.PI * n / (count - 1))
            + 0.08 * Math.cos(4 * Math.PI * n / (count - 1)));
        phases[p][j] = tap;
        sum += tap;
      }
      for (j = 0; j < TAPS_A_PHASE; j++) { phases[p][j] /= sum; }
    }
  })();

  var context = given;
  if (!context) {
    var Context = window.AudioContext || window.webkitAudioContext;
    try {
      context = new Context({ sampleRate: RATE_IN * UP });
    } catch (refused) {
      context = new Context();
    }
  }
  var isBroughtUp = context.sampleRate === RATE_IN * UP;
  var gain = context.createGain();
  gain.connect(context.destination);

  // the last samples heard, newest at `newest`, going round
  var past = new Float32Array(TAPS_A_PHASE), newest = 0;
  // where the next buffer goes: so many frames after `begun`
  var begun = 0, frames = 0;

  function broughtUp(bytes) {
    var buffer = context.createBuffer(1, bytes.length * UP, RATE_IN * UP);
    var out = buffer.getChannelData(0), i, p, j, at, sum, taps;
    for (i = 0; i < bytes.length; i++) {
      newest = (newest + 1) % TAPS_A_PHASE;
      past[newest] = linear[bytes[i]];
      for (p = 0; p < UP; p++) {
        taps = phases[p];
        sum = 0;
        at = newest;
        for (j = 0; j < TAPS_A_PHASE; j++) {
          sum += taps[j] * past[at];
          at = at === 0 ? TAPS_A_PHASE - 1 : at - 1;
        }
        out[i * UP + p] = sum;
      }
    }
    return buffer;
  }

  function asItCame(bytes) {
    var buffer = context.createBuffer(1, bytes.length, RATE_IN);
    var out = buffer.getChannelData(0);
    for (var i = 0; i < bytes.length; i++) { out[i] = linear[bytes[i]]; }
    return buffer;
  }

  function play(bytes) {
    var now = context.currentTime, rate = context.sampleRate;
    var at = begun + frames / rate;
    if (at > now + TOO_LATE) { return; }
    if (at < now + 0.02) {
      // begun, or fallen behind: start again a little ahead, on a whole frame
      begun = Math.ceil((now + AHEAD) * rate) / rate;
      frames = 0;
      at = begun;
    }
    var buffer = isBroughtUp ? broughtUp(bytes) : asItCame(bytes);
    var source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(gain);
    source.start(at);
    frames += Math.round(buffer.duration * rate);
  }

  return {
    play: play,
    // 0 is silent, 1 is as the radio sent it
    setLoudness: function (loudness) { gain.gain.value = loudness; },
    close: function () { if (context.close) { context.close(); } }
  };
}
