// sound.js: plays a radio's sound, as it comes from /listen, in a browser
//
// The globe (/map) and the owner's web client (/operate) both use this.
//
//   var player = radioSound();
//   player.play(bytes);       each binary message from /listen
//   player.lost(count);       when /listen says "lost <count>"
//   player.setLoudness(0.6);  0 is silent, 1 is as the radio sent it
//   player.openMicrophone(taken).then(...)   speech to transmit, see below
//   player.openKeyer(keyed).then(...)        CW keyed on the page, see below
//   player.close();
//
// WHAT COMES IN
//
// The sound is as the radio makes it: 8000 samples a second, each one byte
// of mu-law (RTP's payload type 0), 160 of them to a packet, fifty packets a
// second. Packets come late, in bursts, and some not at all, and the
// service says when it saw that some were lost.
//
// WHO DOES THE WORK
//
// sound_worklet.js, which runs in the browser's sound thread: it brings the
// samples up to the browser's 48000, keeps a little in hand against late
// packets, and covers the lost ones. It says how and why at its head. This
// file starts it and passes the packets to it.
//
// The browser's sound is asked to run at 48000, which the worklet makes
// its samples at; if it runs at another rate the worklet fits them to it.
//
// A browser without sound worklets gets each packet played as it comes,
// as was done at first. It clicks when
// packets are late or lost, and has a grain on it; it is better than
// silence.

function radioSound() {
  "use strict";

  var RATE_IN = 8000, RATE_OUT = 48000;
  var AHEAD = 0.1, TOO_LATE = 0.5;

  var Context = window.AudioContext || window.webkitAudioContext, context;
  try {
    context = new Context({ sampleRate: RATE_OUT });
  } catch (refused) {
    context = new Context();
  }
  var gain = context.createGain();
  gain.connect(context.destination);

  var worklet = null, isPlain = false, loaded = null, microphone = null;
  if (context.audioWorklet && window.AudioWorkletNode) {
    loaded = context.audioWorklet.addModule("/static/sound_worklet.js");
    loaded.then(function () {
      worklet = new AudioWorkletNode(context, "radio-sound",
        { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [1] });
      worklet.connect(gain);
    }, function () { isPlain = true; });
  } else {
    isPlain = true;
  }

  /* the microphone, for speech to transmit. The browser asks its owner
     the first time. It is asked to take out the echo of what the page is
     playing, to quieten the room, and to hold the voice at an even level,
     which a telephone does well and a transmitter wants. Once open it
     stays open, so that a transmission does not begin with the wait for
     it; what it hears goes nowhere unless the page sends it. */

  function openMicrophone(taken) {
    if (microphone) { return Promise.resolve(); }
    if (!loaded || !navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      return Promise.reject(new Error("this browser cannot give a page its microphone"));
    }
    return loaded.then(function () {
      return navigator.mediaDevices.getUserMedia({ audio: {
        channelCount: 1, echoCancellation: true, noiseSuppression: true, autoGainControl: true
      } });
    }).then(function (stream) {
      var silent = context.createGain();
      silent.gain.value = 0;
      silent.connect(context.destination);
      microphone = new AudioWorkletNode(context, "radio-microphone",
        { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1] });
      microphone.port.onmessage = function (message) { taken(new Uint8Array(message.data)); };
      context.createMediaStreamSource(stream).connect(microphone);
      // a node that leads nowhere is not run; this one leads to silence
      microphone.connect(silent);
    });
  }

  /* the plain way */

  var linear = new Float32Array(256);
  (function () {
    for (var b = 0; b < 256; b++) {
      var u = ~b & 255, size = (((u & 15) << 3) + 132) << ((u & 112) >> 4);
      linear[b] = ((u & 128) ? (132 - size) : (size - 132)) / 32768;
    }
  })();
  var playAt = 0;

  function playPlain(bytes) {
    var now = context.currentTime;
    if (playAt > now + TOO_LATE) { return; }
    if (playAt < now + 0.02) { playAt = now + AHEAD; }
    var buffer = context.createBuffer(1, bytes.length, RATE_IN);
    var out = buffer.getChannelData(0);
    for (var i = 0; i < bytes.length; i++) { out[i] = linear[bytes[i]]; }
    var source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(gain);
    source.start(playAt);
    playAt += buffer.duration;
  }

  /* the keyer, for CW keyed on the page: sound_worklet.js has it. keyed(
     code, gapMs) is given each character as it is finished. What comes
     back is told the pads as they are touched and let go, and the speed,
     the pitch and how loud the sidetone is to be. The sidetone does not go
     by the loudness set above: the page turns that down while keying, so
     as not to play the radio's own sidetone late. */

  function openKeyer(keyed) {
    if (!loaded) {
      return Promise.reject(new Error("this browser cannot run a keyer in a page"));
    }
    return loaded.then(function () {
      var keyer = new AudioWorkletNode(context, "radio-keyer",
        { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [1] });
      keyer.port.onmessage = function (message) {
        keyed(message.data.code, message.data.gapMs);
      };
      keyer.connect(context.destination);
      return { tell: function (what) { keyer.port.postMessage(what); } };
    });
  }

  return {
    play: function (bytes) {
      if (worklet) {
        var sound = bytes.slice().buffer;
        worklet.port.postMessage({ sound: sound }, [sound]);
      } else if (isPlain) {
        playPlain(bytes);
      }
    },
    lost: function (count) {
      if (worklet) { worklet.port.postMessage({ lost: count }); }
    },
    setLoudness: function (loudness) { gain.gain.value = loudness; },
    // taken(bytes) is given each twenty milliseconds of speech, 160 bytes
    // of mu-law, from when the promise is kept
    openMicrophone: openMicrophone,
    openKeyer: openKeyer,
    close: function () { context.close(); }
  };
}
