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

  /* the sound goes to the speaker straight from the page while the page is
     seen, and through a media player, an <audio> on the page, while it is
     not. A telephone whose screen is put out stops a page's own sound, and
     the page with it, and its connection to the radio goes; the author's
     went quiet so. A page that is playing media it keeps running and heard,
     as it does a music player, and shows it on the locked screen, named for
     the page. But on the author's Android telephone the player ran short of
     sound often, which the page's own sound does not; so it is used only
     with the screen out. It plays from the start, silent while the page is
     seen, so that it is already playing when the screen goes out: a browser
     lets a player start only when the page is touched. Where there is no
     such player, or it will not start, the sound always goes straight to
     the speaker. */

  var media = null, toMedia = null, isOnMedia = false;
  function route() {
    var wantsMedia = toMedia !== null && document.visibilityState !== "visible";
    if (wantsMedia === isOnMedia) { return; }
    gain.disconnect();
    gain.connect(wantsMedia ? toMedia : context.destination);
    isOnMedia = wantsMedia;
  }
  function startMedia() {
    if (!context.createMediaStreamDestination || !window.Audio) { return; }
    toMedia = context.createMediaStreamDestination();
    media = new Audio();
    media.srcObject = toMedia.stream;
    var started = media.play();
    if (started && started.catch) {
      started.catch(function () {
        toMedia = null;
        media = null;
        route();
      });
    }
    if ("mediaSession" in navigator && window.MediaMetadata) {
      navigator.mediaSession.metadata = new MediaMetadata({
        title: document.title.replace(/ - my\.sbitx\.net$/, ""), artist: "my.sbitx.net" });
    }
  }
  gain.connect(context.destination);
  startMedia();
  document.addEventListener("visibilitychange", route);

  /* the screen is kept on for as long as the player lives. A telephone
     left to itself puts its screen out after a minute or so, and a browser
     whose screen is out stops the page and lets its connection go. The
     author's own listening, on the globe, was ending every minute, and so
     was everyone's who listened from a telephone and did not touch it;
     the service's log showed the connections ending after fifty or sixty
     seconds each. A browser gives up the hold whenever the page is hidden,
     so it is asked for again each time the page is seen. Where a browser
     has no such thing, nothing is done. */

  var awake = null, isClosed = false;

  function stayAwake() {
    if (isClosed || !navigator.wakeLock || document.visibilityState !== "visible") { return; }
    navigator.wakeLock.request("screen").then(function (held) {
      if (isClosed) { held.release(); return; }
      awake = held;
    }, function () { /* a telephone low on charge refuses; it plays all the same */ });
  }
  document.addEventListener("visibilitychange", stayAwake);
  stayAwake();

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

  // The sidetone has a sound path of its own, apart from the radio's
  // sound. The author found the first keyer's sidetone so late behind his
  // thumb that he took it for the radio's coming back. It was made here,
  // but through the path the radio's sound plays by, which is asked to run
  // at 48000 whatever the telephone's own rate is and so may be one the
  // telephone has to convert and hold back. This one asks for nothing but
  // the least delay the telephone can give, at the telephone's own rate.
  // It is opened from the touch on KEY, which a browser wants before it
  // will make a sound.
  var keyerContext = null;

  //
  // What the author's telephone then said of itself, 2026-10-12: on either
  // path, 128 ms held before the sound is handed over and 299 ms from there
  // to the speaker. So the sidetone was some 0.4 s behind the thumb, and
  // none of it the page's doing. The least delay is now asked for by its
  // number, nought, and his telephone heeded that where the word for it
  // had got only its usual: 3 ms held, 174 ms to the speaker. A buzz of
  // the telephone with each element was tried for the rest and taken out
  // again at once; he did not want it.
  function openKeyer(keyed) {
    if (!loaded) {
      return Promise.reject(new Error("this browser cannot run a keyer in a page"));
    }
    try {
      keyerContext = new Context({ latencyHint: 0 });
    } catch (unheeded) {
      try {
        keyerContext = new Context({ latencyHint: "interactive" });
      } catch (refused) {
        return Promise.reject(refused);
      }
    }
    return keyerContext.audioWorklet.addModule("/static/sound_worklet.js").then(function () {
      var keyer = new AudioWorkletNode(keyerContext, "radio-keyer",
        { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [1] });
      keyer.port.onmessage = function (message) {
        keyed(message.data.code, message.data.gapMs);
      };
      keyer.connect(keyerContext.destination);
      return {
        tell: function (what) { keyer.port.postMessage(what); },
        // how late what it makes is heard, as far as the browser knows
        delay: function () { return delayOf(keyerContext); }
      };
    });
  }

  function delayOf(one) {
    return one.sampleRate + " Hz, " + Math.round((one.baseLatency || 0) * 1000) + " ms held, "
      + (one.outputLatency === undefined ? "?" : Math.round(one.outputLatency * 1000))
      + " ms to the speaker";
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
    delay: function () { return delayOf(context); },
    close: function () {
      isClosed = true;
      document.removeEventListener("visibilitychange", stayAwake);
      if (awake) { awake.release(); awake = null; }
      document.removeEventListener("visibilitychange", route);
      if (media) { media.pause(); media.srcObject = null; media = null; }
      context.close();
      if (keyerContext) { keyerContext.close(); }
    }
  };
}
