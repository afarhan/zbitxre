// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// operate.js — the radio worked from a web browser, anywhere.
//
// The author asked for a web client as close to the window on the radio's
// own screen as could be made, clients/gtk/zbitx_gui.c, for a phone held on
// its side, with the logbook and the settings as panels that come up over
// it. This is that page's script; operate.css lays it out, on the window's
// plan: ten places across and seven down.
//
//   row 1   STEP | the meter | the frequency, two wide | RIT, SPLIT, DRIVE,
//           AGC, IF, VOL
//   row 2   a button for each band, 10M to 80M, and MODE
//   rows 3 to 7, on the left: LOG, SET, TERM or in CW KEY, which brings up
//           the keyer's two pads over rows 5 to 7, and one more for the mode
//           in the middle, six wide: the status line, and under it the
//           graph and the waterfall; in FT8 the messages copied have the
//           left half
//           on the right, three wide: the list of the radio's other
//           controls and the mode's own, which scrolls; a tap on the
//           frequency lays the tuning wheel over it
//   row 7   under the waterfall, six places for the mode's own buttons:
//           TX and RX in the voice modes, the CW macros in CW
//
// HOW IT REACHES THE RADIO
//
// A browser cannot make the plain connection the window makes. The page
// opens a WebSocket to my.sbitx.net, /work, and the service joins it to
// the radio; accounts/remote.py says how, and that it is for the radio's
// owner, signed in. What goes over it is the radio's own protocol, a line
// at a time, as PROTOCOL.md at the top of the repository has it, so this
// script is a client of the radio as the window is: it is told every
// control's value, asks for the schema and builds its list from that, so
// that a control a modem adds is here without this script knowing of it,
// and asks for the spectrum and for the messages copied off the air. A
// line that begins with "!" is the service's own and not the radio's:
// connected, offline, busy.
//
// The page sends a full stop every five seconds. The service takes a page
// that has said nothing for twenty to be gone, and tells the radio to stop
// transmitting: a phone goes out of reach of its network without saying.
//
// In the voice modes TX opens this browser's microphone, and what the owner
// says goes to the radio to be sent; "sending, and in the voice modes
// speaking", below, says how.
//
// The radio's sound comes as it does to anyone listening from the globe,
// over a second WebSocket, /listen: twenty milliseconds of mu-law to a
// message, played by sound.js, which says how. VOL is the loudness in this browser and is not sent to the
// radio: the author's rule is that whoever listens from afar sets their
// own volume.
//
// TUNING
//
// The radio knows only FREQ. STEP, VFO and OFFSET are this page's own, as
// they are the window's, and work as the window's do. VFO and OFFSET are
// beside the frequency on the top row, being what its box shows: the
// author made one control, OFFSET, of what were two switches, RIT and
// SPLIT, which are never wanted together, a split being a RIT of very
// wide range; and VFO has the place that left.
//
//   VFO     A or B, each with a dial of its own. Choosing the other keeps
//           the dial of the one left and goes to the other's; one not yet
//           used opens where the other is. The radio is told both dials,
//           VFO_A and VFO_B, to keep, and the one it has kept for the VFO
//           in waiting is taken up.
//   OFFSET  how far the receiver is from the transmitter:
//           NORM   both are on the dial.
//           RIT    tuning moves the receiver's offset, up to 25 kHz either
//                  way, and the dial stays; the receiver listens at the
//                  dial and the offset, and the transmitter is on the
//                  dial. Leaving RIT puts the offset back to nothing.
//           SPLIT  the transmitter is on the other VFO's dial. Choosing it
//                  goes to VFO A, so that it receives on A and sends on B.
//                  To set where it sends: VFO B, tune, VFO A.
//
// The frequency's two lines are the window's: the large one is where the
// radio is working and the small one what is in waiting, the other VFO as
// a rule, and with RIT or SPLIT on where the transmitter is to be, or
// while sending where the receiver was.
//
// The radio is told, as FREQ, where the receiver listens; and when it goes
// to transmit with RIT or SPLIT on, where the transmitter is to be, and
// back again after. When this page keys the radio, the frequency is sent
// before TX ON and after TX OFF, so nothing is sent off frequency. That
// is TX in the voice modes and 2TONE.
//
// When the radio keys itself, for a CW macro or in FT8, SPLIT and RIT do
// nothing yet: it sends where the receiver listens. The page hears that
// the radio is sending only after it has begun, a third of a second after
// on the author's line, and to move the frequency then would be to move a
// transmission that is under way; and CW comes and goes between words
// faster than the page could follow. That is the radio's to do, and it
// does not know of SPLIT or RIT yet.
//
// A finger drawn along the graph or the waterfall tunes, a step for every
// few pixels, and so do a mouse's wheel and the tuning wheel.
//
// A tap on the graph goes to the next span, as in the window. In FT8 a tap
// there puts the pitch we send at where it fell.
//
// SETTING A CONTROL
//
// There is no knob on a phone. A tap on a control brings up something to
// set it with, sized for a thumb: for a number, a slider and a button
// either side of it for a step at a time; for a choice, a button for each;
// for a text, a box to type in. A choice of two is simply changed over.
//
// WHAT IS NOT HERE YET
//
// Speaking into the radio from the phone's microphone; SPLIT and VFO B;
// the tuning wheel; TERM.

(function () {
  "use strict";

  var RADIO = document.body.getAttribute("data-radio");
  var NAME = document.body.getAttribute("data-name");
  var SOCKETS = document.body.getAttribute("data-sockets");
  var BANDS = ["10M", "12M", "15M", "17M", "20M", "30M", "40M", "60M", "80M"];
  var STEPS = { "10H": 10, "100H": 100, "500H": 500, "1K": 1000, "10K": 10000 };
  // what has a place of its own, or is this page's business and not a row
  var PLACED = ["FREQ", "BAND", "MODE", "AUDIO", "IF", "AGC", "DRIVE", "BW_VOICE", "BW_CW",
    "STACK", "MIC"];
  var FULL_SCALE_DB = 80, RIT_MOST_HZ = 25000, PIXELS_A_STEP = 14;

  var values = {};        // every control's value, as the radio tells them
  var schema = {};        // name -> { group, kind, min, max, step, choices }
  var order = [];         // the controls' names in the radio's order
  var own = { STEP: stored("step", "1K"), OFFSET: "NORM", VFO: "A",
    VOL: stored("vol", "60") };
  var dialHz = 0, ritHz = 0;
  var vfoHz = { A: 0, B: 0 };  // each VFO's dial; the one in use is dialHz
  var work = null, isConnected = false, retry = null;
  var points = [], spanHz = 0, envelope = null;
  var cwRead = "", marked = null, logQuery = "";

  function stored(name, otherwise) {
    try { return window.localStorage.getItem("zbitx." + name) || otherwise; }
    catch (e) { return otherwise; }
  }
  function store(name, value) {
    try { window.localStorage.setItem("zbitx." + name, value); } catch (e) { /* no matter */ }
  }

  function make(tag, className, text, parent) {
    var element = document.createElement(tag);
    if (className) { element.className = className; }
    if (text !== undefined && text !== null) { element.textContent = text; }
    if (parent) { parent.appendChild(element); }
    return element;
  }

  /* the page */

  var app = make("div", "", null, document.body);
  app.id = "app";
  var tiles = {};

  function tile(name, column, row, title, tapped) {
    var box = make("div", "tile", null, app);
    box.style.gridColumn = String(column);
    box.style.gridRow = String(row);
    var t = { box: box, title: null, value: null };
    if (title) { t.title = make("div", "title", title, box); }
    t.value = make("div", "value", "", box);
    if (tapped) { box.addEventListener("click", tapped); }
    tiles[name] = t;
    return t;
  }

  tile("STEP", 1, 1, "STEP", function () { chooseOwn("STEP", Object.keys(STEPS)); });
  var meterBox = make("div", "canvas-box", null, app);
  meterBox.id = "meter-box";
  var meter = make("canvas", "", null, meterBox);
  meter.id = "meter";
  var frequency = make("div", "tile", null, app);
  frequency.id = "frequency";
  var waiting = make("div", "waiting", "", frequency);
  var figures = make("div", "figures", "", frequency);
  tile("VFO", 5, 1, "VFO", function () {
    chooseVfo(vfoInWaiting());
    show();
  });
  tile("OFFSET", 6, 1, "OFFSET", function () {
    choose("OFFSET", ["NORM", "RIT", "SPLIT"], own.OFFSET, function (choice) {
      own.OFFSET = choice;
      // leaving RIT puts the receiver back on the dial
      if (!isRit()) { ritHz = 0; }
      // a split receives on A and sends on B
      if (isSplit()) {
        chooseVfo("A");
        if (!vfoHz[vfoInWaiting()]) { vfoHz[vfoInWaiting()] = dialHz; }
      }
      retune();
      show();
    });
  });
  tile("DRIVE", 7, 1, "DRIVE", function () { adjust("DRIVE"); });
  tile("AGC", 8, 1, "AGC", function () { adjust("AGC"); });
  tile("IF", 9, 1, "IF", function () { adjust("IF"); });
  tile("VOL", 10, 1, "VOL", function () {
    adjustNumber("VOL", 0, 100, 5, Number(own.VOL), function (value) {
      own.VOL = String(value);
      store("vol", own.VOL);
      setLoudness();
      show();
    });
  });
  BANDS.forEach(function (band, i) {
    tile("BAND " + band, i + 1, 2, null, function () { set("BAND", band); }).box.className
      += " button";
  });
  tile("MODE", 10, 2, "MODE", function () { adjust("MODE"); });

  // the first place of the bottom row, beside TX and RX, in the modes that
  // are spoken into: where the panel has the microphone's gain
  tile("MIC", 1, 7, "MIC", function () { adjust("MIC"); });

  var column = ["LOG", "SET", "TERM", ""].map(function (label, i) {
    var t = tile("COLUMN " + i, 1, i + 3, null, function () { columnTapped(i); });
    t.box.className += " button";
    return t;
  });
  var footer = [0, 1, 2, 3, 4, 5].map(function (i) {
    var t = tile("FOOTER " + i, i + 1, 7, null, function () { footerTapped(i); });
    t.box.className += " button";
    return t;
  });

  var middle = make("div", "", null, app);
  middle.id = "middle";
  var status = make("div", "", "", middle);
  status.id = "status";
  var views = make("div", "", null, middle);
  views.id = "views";
  var messages = make("div", "", null, views);
  messages.id = "messages";
  var scopeBox = make("div", "canvas-box", null, views);
  scopeBox.id = "scope-box";
  var scope = make("canvas", "", null, scopeBox);
  scope.id = "scope";
  var list = make("div", "", null, app);
  list.id = "list";
  var keyerBox = make("div", "gone", null, app);
  keyerBox.id = "keyer";
  var wheelBox = make("div", "canvas-box gone", null, app);
  wheelBox.id = "wheel-box";
  var wheel = make("canvas", "", null, wheelBox);
  wheel.id = "wheel";

  var veil = make("div", "", null, document.body);
  veil.id = "veil";
  var popup = make("div", "", null, veil);
  popup.id = "popup";
  veil.addEventListener("click", function (e) { if (e.target === veil) { closePopup(); } });
  var panel = make("div", "", null, document.body);
  panel.id = "panel";
  var turn = make("div", "", null, document.body);
  turn.id = "turn";
  make("p", "", "Turn the phone on its side to work the radio.", turn);
  make("button", "", "Leave the radio", turn).addEventListener("click", function () { leave(); });

  /* talking to the radio */

  function send(line) {
    if (work && work.readyState === 1) { work.send(line); }
  }

  // a control is set here at once and the radio told; the radio does not
  // tell a client its own changes back
  function set(name, value) {
    values[name] = String(value);
    if (name === "TX") {
      // the transmitter's frequency is in place before the radio sends,
      // and the receiver's is not gone back to until it has stopped
      if (value === "ON") { retune(); }
      send("TX " + value);
      if (value !== "ON") { retune(); }
      show();
      return;
    }
    send(name + " " + value);
    show();
  }

  function say(text, className) {
    status.className = className || "";
    status.textContent = text;
  }

  function connect() {
    retry = null;
    say("connecting to " + NAME);
    work = new WebSocket(SOCKETS + "/work?radio=" + RADIO);
    work.onmessage = function (message) {
      var lines = String(message.data).split("\n"), i;
      for (i = 0; i < lines.length; i++) {
        if (lines[i]) { take(lines[i]); }
      }
    };
    work.onclose = function () {
      isConnected = false;
      if (!retry) {
        if (status.textContent.indexOf("another") < 0) { say(NAME + ": not connected; trying again"); }
        retry = setTimeout(connect, 3000);
      }
    };
  }
  setInterval(function () { send("."); }, 5000);

  function take(line) {
    var space = line.indexOf(" ");
    var name = space < 0 ? line : line.slice(0, space);
    var value = space < 0 ? "" : line.slice(space + 1);
    if (name === "!") { takeFromService(value); return; }
    if (name === "SPECTRUM") { takeSpectrum(value); return; }
    // While the pads are being keyed nothing is drawn that can wait: not
    // the graph and waterfall, nor what is being sent, nor the meter. A
    // touch on a pad waits its turn behind whatever the page is doing, and
    // on a telephone the drawing was most of that; the author found the
    // pads slow to answer.
    if (name === "MODULATION") {
      if (!keyedTimer) { envelope = unhex(value); drawScope(); }
      return;
    }
    if (keyedTimer && (name === "SMETER" || name === "POWER" || name === "SWR"
        || name === "BATTERY" || name === "CLOCK")) {
      values[name] = value;
      // the power and the SWR are wanted while sending, and the dial alone
      // is little to draw
      if (name === "POWER" || name === "SWR") { drawMeter(); }
      return;
    }
    if (name === "CONTROL") { takeSchema(value); return; }
    if (name === "MESSAGE") { takeMessage(value); return; }
    if (name === "CW") { cwRead = (cwRead + value).slice(-80); say(cwRead); return; }
    if (name === "SAY" || name === "ERROR") { say(value); return; }
    if (name === "WARN") { say(value, "warning"); return; }
    if (name === "QSO") { say(value, value ? "qso" : ""); return; }
    if (name === "LOG_LIST") { logBegins(); return; }
    if (name === "LOG") { logRow(value); return; }
    if (name === "TIME") { return; }
    values[name] = value;
    if (name === "FREQ") { takeRadioFrequency(Number(value)); }
    if (name === "VFO_" + vfoInWaiting() && Number(value) > 0) {
      vfoHz[vfoInWaiting()] = Number(value);
    }
    // the radio has stopped sending of its own accord: back to where the
    // receiver listens. When it begins of its own accord the frequency is
    // left alone; the tuning notes above say why
    if (name === "TX" && value !== "ON") { retune(); }
    showSoon();
  }

  function takeFromService(said) {
    var words = said.split(" ");
    if (words[0] === "connected") {
      isConnected = true;
      schema = {};
      order = [];
      messages.textContent = "";
      say("connected to " + words[1] + " through my.sbitx.net");
      send("schema\nspectrum on\nmessages on\ntime");
    } else if (words[0] === "busy") {
      say(NAME + " is being worked by another client; trying again");
    } else if (words[0] === "closed") {
      say(NAME + ": remote operation is switched off at the radio", "warning");
    } else if (words[0] === "offline") {
      say(NAME + " is not on line; trying again");
    }
  }

  // "CW WPM number 5 50 1", "RADIO MODE choice USB/LSB/CW", "END"
  function takeSchema(said) {
    var words = said.split(" ");
    if (words[0] === "END") { buildList(); show(); return; }
    var control = { group: words[0], kind: words[2], min: 0, max: 0, step: 1, choices: [] };
    if (control.kind === "number") {
      control.min = Number(words[3]); control.max = Number(words[4]);
      control.step = Number(words[5]) || 1;
    } else if (words[3]) {
      control.choices = words.slice(3).join(" ").split("/");
    }
    schema[words[1]] = control;
    order.push(words[1]);
  }

  /* what the mode is */

  function modeGroup() {
    var mode = values.MODE || "";
    if (mode === "CW" || mode === "CWR") { return "CW"; }
    if (mode === "USB" || mode === "LSB" || mode === "AM") { return "VOICE"; }
    return mode;
  }
  function isFt8() { return modeGroup() === "FT8"; }
  function isSending() { return values.TX === "ON"; }

  /* tuning */

  function vfoInWaiting() { return own.VFO === "A" ? "B" : "A"; }
  function isRit() { return own.OFFSET === "RIT"; }
  function isSplit() { return own.OFFSET === "SPLIT"; }

  // where the receiver listens: the dial, moved by the offset while RIT is on
  function receiverHz() { return dialHz + (isRit() ? ritHz : 0); }

  // where the transmitter is to be: the dial, or while SPLIT is on the
  // other VFO's dial, if that VFO has been used
  function transmitterHz() {
    if (!isSplit() || !vfoHz[vfoInWaiting()]) { return dialHz; }
    return vfoHz[vfoInWaiting()];
  }

  // where the radio should be now
  function workingHz() { return isSending() ? transmitterHz() : receiverHz(); }

  // the radio is told where it should be, at once, if it is not there:
  // for going to transmit and coming back, which cannot wait
  function retune() {
    if (!dialHz || Number(values.FREQ) === workingHz()) { return; }
    if (freqTimer) { clearTimeout(freqTimer); freqTimer = null; }
    values.FREQ = String(workingHz());
    send("FREQ " + values.FREQ);
  }

  // the radio says it is somewhere else: a band chosen, or its own panel
  // or another client tuning it. That is where the receiver listens, and
  // the dial is worked back from it. While it sends on another frequency
  // than it receives on, what it says is where it sends, and is let pass.
  function takeRadioFrequency(hz) {
    if (isSending() && transmitterHz() !== receiverHz()) { return; }
    dialHz = hz - (isRit() ? ritHz : 0);
    // a VFO never used is put here once, and does not follow after
    if (!vfoHz[vfoInWaiting()]) { vfoHz[vfoInWaiting()] = dialHz; }
  }

  // a VFO chosen: the dial of the one left is kept and the other's taken
  // up, and the radio is told both, to keep
  function chooseVfo(vfo) {
    if (vfo === own.VFO || !dialHz) { return; }
    vfoHz[own.VFO] = dialHz;
    if (!vfoHz[vfo]) { vfoHz[vfo] = dialHz; }
    own.VFO = vfo;
    dialHz = vfoHz[vfo];
    send("VFO_A " + vfoHz.A);
    send("VFO_B " + vfoHz.B);
    retune();
  }

  var freqTimer = null;
  function sendFrequency() {
    values.FREQ = String(workingHz());
    if (freqTimer) { return; }
    // a finger drawn along the waterfall moves many steps a second; the
    // radio is told where it has got to ten times a second, not each step
    freqTimer = setTimeout(function () {
      freqTimer = null;
      send("FREQ " + values.FREQ);
    }, 100);
  }

  function tune(steps) {
    var stepHz = STEPS[own.STEP] || 1000;
    if (!steps || !dialHz) { return; }
    if (isRit()) {
      ritHz = Math.max(-RIT_MOST_HZ, Math.min(RIT_MOST_HZ, ritHz + steps * stepHz));
    } else {
      dialHz = Math.round(dialHz / stepHz) * stepHz + steps * stepHz;
    }
    sendFrequency();
    show();
  }

  function dotted(hz) {
    var text = ("000000" + Math.max(0, hz)).slice(-Math.max(7, String(hz).length));
    return text.slice(0, -6).replace(/^0+(?=\d)/, "") + "." + text.slice(-6, -3) + "."
      + text.slice(-3);
  }

  /* showing what is */

  var isShowDue = false;
  function showSoon() {
    if (isShowDue) { return; }
    isShowDue = true;
    window.requestAnimationFrame(function () { isShowDue = false; show(); });
  }

  function show() {
    var group = modeGroup(), macros = schema.MACRO ? schema.MACRO.choices : [], i;
    tiles.STEP.value.textContent = own.STEP;
    tiles.VFO.value.textContent = own.VFO;
    tiles.OFFSET.value.textContent = own.OFFSET;
    tiles.OFFSET.box.className = "tile" + (own.OFFSET === "NORM" ? "" : " on");
    tiles.DRIVE.value.textContent = values.DRIVE || "";
    tiles.AGC.value.textContent = values.AGC || "";
    tiles.IF.value.textContent = values.IF || "";
    tiles.VOL.value.textContent = own.VOL;
    tiles.MODE.value.textContent = values.MODE || "";
    tiles.MIC.value.textContent = values.MIC || "";
    tiles.MIC.box.className = "tile" + (group === "VOICE" || group === "RADE2" ? "" : " hidden");
    BANDS.forEach(function (band) {
      var t = tiles["BAND " + band];
      t.value.textContent = band;
      t.box.className = "tile button" + (values.BAND === band ? " in-use" : "");
    });

    var receiveHz = receiverHz(), transmitHz = transmitterHz(), other = vfoInWaiting();
    frequency.className = "tile" + (isSending() ? " sending" : "");
    if (receiveHz !== transmitHz && isSending()) {
      waiting.textContent = "RX:" + dotted(receiveHz);
      figures.textContent = "TX:" + dotted(transmitHz);
    } else if (receiveHz !== transmitHz) {
      waiting.textContent = "TX:" + dotted(transmitHz);
      figures.textContent = "RX:" + dotted(receiveHz);
    } else {
      waiting.textContent = other + ":" + dotted(vfoHz[other] || dialHz);
      figures.textContent = (isSending() ? "TX" : own.VFO) + ":" + dotted(dialHz);
    }

    var labels = ["LOG", "SET", "TERM", ""];
    if (group === "CW") { labels[2] = "KEY"; labels[3] = "ESC"; }
    if (group !== "CW" && keyerBox.className !== "gone") { showKeyer(false); }
    if (group !== "CW" && typingBar.className !== "gone") { showTyping(false); }
    tellKeyer();
    if (group === "FT8") { labels[2] = "CQ"; labels[3] = "HALT"; }
    column.forEach(function (t, n) {
      t.value.textContent = labels[n];
      t.box.className = "tile button" + (labels[n] === "TERM" ? " dim" : "")
        + (labels[n] ? "" : " hidden");
    });
    for (i = 0; i < footer.length; i++) {
      var label = footerLabel(i, group, macros);
      footer[i].value.textContent = label;
      footer[i].box.className = "tile button" + (label ? "" : " hidden")
        + (label === "TX" && isSending() ? " on" : "")
        + (label === "RX" && !isSending() ? " focused" : "");
    }
    app.className = isFt8() ? "ft8" : "";

    showList();
    drawMeter();
  }

  function footerLabel(place, group, macros) {
    // RADE2 is laid out as the voice modes are
    if (group === "VOICE" || group === "RADE2") { return ["", "TX", "RX", "", "", ""][place]; }
    if (group === "2TONE") { return ["TX", "RX", "", "", "", ""][place]; }
    if (group === "CW") { return macros[place] || ""; }
    return "";
  }

  /* sending, and in the voice modes speaking

     In CW, FT8 and 2TONE the radio makes what it sends, and TX is a line
     to it like any other. In the voice modes what is sent is what the
     owner says, into this browser's microphone: sound.js opens it, and
     each twenty milliseconds it hears is sent to the service as a binary
     message on the connection the radio is worked by, from where it goes
     to the radio; accounts/remote.py says how.

     - The radio is told to send only when the microphone is open. If the
       browser refuses it, the radio is not keyed: it would send whatever
       its own microphone hears in an empty room.
     - While speaking, the radio's sound is not played here. The radio
       plays a monitor of what it sends, which would come back half a
       second late and into the microphone.
     - At the end, speech stops going at once and the radio is told to
       stop SPEECH_TAIL_MS later: the last word is still on its way. */

  var SPEECH_TAIL_MS = 300, MONITOR_TAIL_MS = 500;
  var isSpeaking = false, isMuted = false, tailTimer = null;

  function sendSpeech(bytes) {
    if (isSpeaking && work && work.readyState === 1) { work.send(bytes); }
  }

  function startSending() {
    // in RADE2 as in the voice modes, what is sent is what is said
    if (modeGroup() !== "VOICE" && modeGroup() !== "RADE2") { set("TX", "ON"); return; }
    if (!player) { return; }
    if (tailTimer) { clearTimeout(tailTimer); tailTimer = null; }
    player.openMicrophone(sendSpeech).then(function () {
      isSpeaking = true;
      isMuted = true;
      setLoudness();
      set("TX", "ON");
    }, function (refused) {
      say("the microphone could not be opened, and the radio is not sending: "
        + refused.message, "warning");
    });
  }

  function stopSending() {
    if (!isSpeaking) { set("TX", "OFF"); return; }
    isSpeaking = false;
    tailTimer = setTimeout(function () {
      set("TX", "OFF");
      tailTimer = setTimeout(function () {
        tailTimer = null;
        isMuted = false;
        setLoudness();
      }, MONITOR_TAIL_MS);
    }, SPEECH_TAIL_MS);
  }

  /* the keyer, in CW: KEY brings up two pads over the lower half of the
     page, DOT and DASH side by side, which are a paddle's two contacts. The keyer itself and its sidetone are in the
     browser's sound thread, sound_worklet.js, at the radio's WPM and PITCH
     and in iambic A, or B if the radio's CW_INPUT is IAMBICB. Each
     character it finishes is sent to the radio as CW_KEY, its dots and
     dashes and the milliseconds the key was up before it, and the radio
     sends it. ESC, beside the pads, stops what the radio still has to
     send.

     The radio's sound is not played from the first touch on a pad until
     the pads have been still for KEYED_QUIET_MS and the radio has stopped
     sending: the radio's own sidetone comes back a third of a second and
     more behind the telephone's, and the author found the two together
     very disconcerting. It was first turned down only when a character
     had been sent, and turned up two seconds after the last, whether or
     not the radio had finished. */

  var KEYED_QUIET_MS = 1500, KEYED_LOOK_MS = 250;
  var keyer = null, isKeyerAsked = false, keyedTimer = null, keyedAt = 0;

  function keying() {
    keyedAt = Date.now();
    if (!isMuted) {
      isMuted = true;
      setLoudness();
    }
    if (keyedTimer) { return; }
    keyedTimer = setInterval(function () {
      if (Date.now() - keyedAt < KEYED_QUIET_MS || isSending()) {
        // the radio is counted as sending until it has been quiet a while
        if (isSending()) { keyedAt = Math.max(keyedAt, Date.now() - KEYED_QUIET_MS + 600); }
        return;
      }
      clearInterval(keyedTimer);
      keyedTimer = null;
      isMuted = isSpeaking;
      setLoudness();
      // and what was not drawn meanwhile is
      show();
    }, KEYED_LOOK_MS);
  }

  function tellKeyer() {
    if (!keyer) { return; }
    keyer.tell({
      wpm: Number(values.WPM) || 18, pitch: Number(values.PITCH) || 600,
      // SIDETONE is set for the radio's own speaker; here twice that,
      // by this page's VOL, never none: it is all there is to key by
      level: Math.max(0.05, Math.min(1, (Number(values.SIDETONE) || 30) / 50
        * Number(own.VOL) / 100)),
      isB: values.CW_INPUT === "IAMBICB"
    });
  }

  function keyed(code, gapMs) {
    send("CW_KEY " + code + (gapMs ? " " + gapMs : ""));
    keying();
  }

  function showKeyer(on) {
    keyerBox.className = on ? "" : "gone";
    if (!on) {
      if (keyer) { keyer.tell({ dot: false, dash: false }); }
      return;
    }
    if (isKeyerAsked || !player) { return; }
    isKeyerAsked = true;
    player.openKeyer(keyed).then(function (opened) {
      keyer = opened;
      tellKeyer();
      note("keyer: " + keyer.delay() + "; the radio's sound: " + player.delay());
    }, function (refused) {
      isKeyerAsked = false;
      showKeyer(false);
      say("the keyer could not be started: " + refused.message, "warning");
    });
  }

  function pad(label, sign, which) {
    var box = make("div", "pad", null, keyerBox);
    make("div", "sign", sign, box);
    make("div", "name", label, box);
    var isHeld = false;
    function held(is) {
      var told = {};
      if (is === isHeld) { return; }
      isHeld = is;
      told[which] = is;
      box.className = "pad" + (is ? " held" : "");
      if (keyer) { keyer.tell(told); }
      if (is) { keying(); }
    }
    // A pad is held while any finger that came down on it is still on
    // the screen. The screen's own touches are listened to, and the
    // browser is told at each to do nothing of its own with them: two
    // fingers on a page are a browser's sign to zoom or to go back, and a
    // browser that takes them so takes them away from the page, when both
    // pads would light and at once go out. That is what the author saw,
    // with only a dot or a dash sent. If a touch is taken away all the
    // same, it is noted in the web server's log.
    function touched(e) {
      e.preventDefault();
      held(e.targetTouches.length > 0);
    }
    box.addEventListener("touchstart", touched, { passive: false });
    box.addEventListener("touchend", touched, { passive: false });
    box.addEventListener("touchmove", function (e) { e.preventDefault(); }, { passive: false });
    box.addEventListener("touchcancel", function (e) {
      held(e.targetTouches.length > 0);
      note("a touch on the " + which + " pad was taken away by the browser");
    });
    // and a mouse, on a computer
    box.addEventListener("mousedown", function (e) {
      e.preventDefault();
      held(true);
    });
    window.addEventListener("mouseup", function () { held(false); });
    box.addEventListener("contextmenu", function (e) { e.preventDefault(); });
  }
  /* the keyboard, in CW: what is typed is sent, a character as it is
     struck, as the earlier program sent what was typed. The radio is told
     CW_TEXT, and a key struck while a macro is being sent stops the macro
     and is sent in its place, which is the radio's doing, as a touch of
     its paddle stops one.

     On a computer the keys are simply struck, with nothing to open. A
     telephone has no keys until a box to type in is touched: ABC, beside
     the pads, brings up a bar along the top of the page with such a box,
     where the telephone's own keyboard, which takes the lower half of the
     screen, does not cover it. What the box holds is only what has been
     typed, to be seen: rubbing a letter out of it does not call it back
     from the radio. Enter sends a space, and ESC stops what is being sent.

     The radio's sound is left on while typing: there is no sidetone here
     for what is typed, and the radio's own, late as it is, is how it is
     heard going out. */

  var typedBefore = "";

  function typeCw(text) {
    var sendable = text.replace(/[\r\n\t ]/g, "_").replace(/[^\x21-\x7e]/g, "");
    if (sendable && modeGroup() === "CW") { send("CW_TEXT " + sendable); }
  }

  var typingBar = make("div", "gone", null, document.body);
  typingBar.id = "typing";
  var typingBox = document.createElement("input");
  typingBox.type = "text";
  typingBox.autocomplete = "off";
  typingBox.autocapitalize = "characters";
  typingBox.spellcheck = false;
  typingBox.setAttribute("autocorrect", "off");
  typingBox.setAttribute("enterkeyhint", "send");
  typingBox.placeholder = "type to send";
  typingBar.appendChild(typingBox);
  make("button", "", "ESC", typingBar).addEventListener("click", function () {
    send("CW_DO ESC");
    typingBox.value = "";
    typedBefore = "";
    typingBox.focus();
  });
  make("button", "", "\u2715", typingBar).addEventListener("click", function () {
    showTyping(false);
  });

  function showTyping(on) {
    typingBar.className = on ? "" : "gone";
    typingBox.value = "";
    typedBefore = "";
    if (on) { typingBox.focus(); } else { typingBox.blur(); }
  }

  // a telephone's keyboard gives a page no keys, only what the box has
  // come to hold; what it holds beyond what it held is what was typed
  typingBox.addEventListener("input", function () {
    var now = typingBox.value;
    if (now.length > typedBefore.length && now.indexOf(typedBefore) === 0) {
      typeCw(now.slice(typedBefore.length));
    }
    // the box is kept short, the newest at its end
    if (now.length > 60) {
      now = now.slice(-30);
      typingBox.value = now;
    }
    typedBefore = now;
  });
  typingBox.addEventListener("keydown", function (e) {
    if (e.key === "Enter") {
      e.preventDefault();
      typingBox.value += " ";
      typedBefore = typingBox.value;
      typeCw(" ");
    }
    e.stopPropagation();
  });

  // a computer's keys, struck with nothing open to type in
  document.addEventListener("keydown", function (e) {
    var into = e.target && e.target.tagName;
    if (modeGroup() !== "CW" || !isStarted || e.ctrlKey || e.altKey || e.metaKey
        || into === "INPUT" || into === "TEXTAREA" || veil.className === "shown"
        || panel.className === "shown") {
      return;
    }
    if (e.key === "Escape") {
      send("CW_DO ESC");
    } else if (e.key === "Enter" || e.key === " ") {
      typeCw(" ");
    } else if (e.key.length === 1) {
      typeCw(e.key);
    } else {
      return;
    }
    e.preventDefault();
  });

  // the two pads lie side by side with nothing between, as a paddle's two
  // sides do, which the author asked for; ESC and the cross are to their left
  var keyerSide = make("div", "side", null, keyerBox);
  make("button", "", "ESC", keyerSide).addEventListener("click", function () {
    send("CW_DO ESC");
  });
  make("button", "", "ABC", keyerSide).addEventListener("click", function () {
    showTyping(typingBar.className === "gone");
  });
  make("button", "", "\u2715", keyerSide).addEventListener("click", function () {
    showKeyer(false);
  });
  pad("DOT", "\u2022", "dot");
  pad("DASH", "\u2014", "dash");

  function footerTapped(place) {
    var label = footer[place].value.textContent;
    if (label === "TX") { startSending(); }
    else if (label === "RX") { stopSending(); }
    else if (label && modeGroup() === "CW") { send("MACRO " + label); }
  }

  function columnTapped(place) {
    var label = column[place].value.textContent;
    if (label === "LOG") { openLog(); }
    if (label === "SET") { openSettings(); }
    if (label === "KEY") { showKeyer(keyerBox.className === "gone"); }
    if (label === "ESC") { send("CW_DO ESC"); }
    if (label === "CQ") { send("FT8_DO CQ"); }
    if (label === "HALT") { send("FT8_DO HALT"); }
  }

  /* the list of controls */

  var rows = [];
  function buildList() {
    list.textContent = "";
    rows = [];
    // The order is the author's, for CW: what is touched often is at the
    // top, BW, the speed and the pitch, and what is set once and left is
    // under it. The rest follow as the radio lists them. TX is not in
    // the list: the voice modes and
    // 2TONE have their TX button, and CW and FT8 key themselves.
    var listed = ["BW", "WPM", "PITCH", "SIDETONE", "CW_DELAY", "CW_INPUT"].concat(
      order.filter(function (name) { return name !== "TX"; }));
    listed.forEach(function (name) {
      var control = schema[name];
      // BW is one of two controls
      if (name !== "BW" && (!control || control.kind === "action"
          || control.kind === "text" || PLACED.indexOf(name) >= 0)) {
        return;
      }
      if (rows.some(function (r) { return r.name === name; })) { return; }
      var row = make("div", "row", null, list);
      make("div", "title", name, row);
      var r = { name: name, box: row, value: make("div", "value", "", row) };
      row.addEventListener("click", function () { adjust(rowControl(name)); });
      rows.push(r);
    });
  }

  // BW is BW_VOICE in the voice modes and BW_CW in CW, as in the window
  function rowControl(name) {
    if (name !== "BW") { return name; }
    return modeGroup() === "CW" ? "BW_CW" : "BW_VOICE";
  }

  function isRowShown(name) {
    var group = modeGroup(), control = schema[rowControl(name)];
    if (!control) { return false; }
    if (name === "BW") { return group !== "FT8"; }
    return control.group === "RADIO" || control.group === group;
  }

  function showList() {
    rows.forEach(function (r) {
      r.box.style.display = isRowShown(r.name) ? "flex" : "none";
      r.value.textContent = values[rowControl(r.name)] || "";
    });
  }

  /* something to set a control with */

  function closePopup() {
    veil.className = "";
    popup.textContent = "";
  }

  function openPopup(title) {
    popup.textContent = "";
    make("h2", "", title, popup);
    veil.className = "shown";
  }

  function adjust(name) {
    var control = schema[name];
    if (!control) { return; }
    if (control.kind === "number") {
      adjustNumber(name, control.min, control.max, control.step, Number(values[name] || 0),
        function (value) { set(name, value); });
    } else if (control.kind === "choice" && control.choices.length === 2) {
      set(name, control.choices[values[name] === control.choices[0] ? 1 : 0]);
    } else if (control.kind === "choice") {
      choose(name, control.choices, values[name], function (choice) { set(name, choice); });
    } else if (control.kind === "text") {
      typeText(name, values[name] || "", function (text) { set(name, text); });
    }
  }

  function adjustNumber(title, least, most, step, value, changed) {
    openPopup(title);
    var now = make("div", "now", String(value), popup);
    var steps = make("div", "steps", null, popup);
    var slider = document.createElement("input");
    function to(next) {
      value = Math.max(least, Math.min(most, least + Math.round((next - least) / step) * step));
      now.textContent = String(value);
      slider.value = String(value);
      changed(value);
    }
    make("button", "", "−", steps).addEventListener("click", function () { to(value - step); });
    make("button", "", "+", steps).addEventListener("click", function () { to(value + step); });
    slider.type = "range"; slider.min = String(least); slider.max = String(most);
    slider.step = String(step); slider.value = String(value);
    slider.addEventListener("input", function () { to(Number(slider.value)); });
    popup.appendChild(slider);
    var foot = make("div", "foot", null, popup);
    make("button", "", "Done", foot).addEventListener("click", closePopup);
  }

  function choose(title, choices, chosen, changed) {
    openPopup(title);
    var box = make("div", "choices", null, popup);
    choices.forEach(function (choice) {
      make("button", choice === chosen ? "chosen" : "", choice, box)
        .addEventListener("click", function () { closePopup(); changed(choice); });
    });
  }

  function chooseOwn(name, choices) {
    choose(name, choices, own[name], function (choice) {
      own[name] = choice;
      store(name.toLowerCase(), choice);
      show();
    });
  }

  function typeText(title, text, changed) {
    openPopup(title);
    var box = document.createElement("input");
    box.type = "text"; box.value = text; box.autocapitalize = "characters";
    box.autocomplete = "off"; box.spellcheck = false;
    popup.appendChild(box);
    var foot = make("div", "foot", null, popup);
    make("button", "", "Cancel", foot).addEventListener("click", closePopup);
    make("button", "", "Save", foot).addEventListener("click", function () {
      closePopup();
      if (box.value.trim()) { changed(box.value.trim().toUpperCase()); }
    });
    box.focus();
  }

  /* the graph and the waterfall */

  var fall = document.createElement("canvas");
  fall.width = 1024; fall.height = 256;
  var fallPen = fall.getContext("2d");
  fallPen.fillStyle = "#000"; fallPen.fillRect(0, 0, fall.width, fall.height);
  var fallRow = fallPen.createImageData(fall.width, 1);

  function unhex(hex) {
    var out = [], i;
    for (i = 0; i + 1 < hex.length; i += 2) { out.push(parseInt(hex.substr(i, 2), 16)); }
    return out;
  }

  // blue rises, then green rises, then blue falls, then red rises, then
  // green falls: the front panel's colours
  function heat(shade, into, at) {
    var band = Math.floor(shade / 32), step = (shade % 32) * 8, r = 0, g = 0, b = 0;
    if (band === 0) { b = step; }
    else if (band === 1) { g = step; b = 255; }
    else if (band === 2) { g = 255; b = 255 - step; }
    else if (band === 3) { r = step; g = 255; }
    else if (band === 4) { r = 255; g = 255 - step; }
    else { r = 255; }
    into[at] = r; into[at + 1] = g; into[at + 2] = b; into[at + 3] = 255;
  }

  function takeSpectrum(said) {
    var space = said.indexOf(" "), i;
    if (space < 0) { return; }
    // while keying, nothing is drawn: see take()
    if (keyedTimer) { return; }
    spanHz = Number(said.slice(0, space));
    points = unhex(said.slice(space + 1));
    envelope = null;
    fallPen.drawImage(fall, 0, 1);
    for (i = 0; i < fall.width; i++) {
      heat(i < points.length ? points[i] * 2 : 0, fallRow.data, i * 4);
    }
    fallPen.putImageData(fallRow, 0, 0);
    drawScope();
  }

  // the Hz from the dial at the left and the right of what is shown, and
  // the first point and how many points that is
  function shownRange() {
    var range = { low: 0, high: 0, first: 0, count: points.length };
    if (!spanHz || points.length < 2) { return range; }
    var hzAPoint = spanHz / points.length, middle = points.length / 2;
    if (isFt8()) {
      range.low = 0; range.high = 4000;
    } else {
      var chosen = parseFloat(values.SPAN || "25") * 1000;
      if (!(chosen > 0) || chosen > spanHz) { chosen = spanHz; }
      range.low = -chosen / 2; range.high = chosen / 2;
    }
    range.first = Math.max(0, Math.round(middle + range.low / hzAPoint));
    range.count = Math.max(2, Math.min(points.length - range.first,
      Math.round((range.high - range.low) / hzAPoint)));
    return range;
  }

  function passband() {
    var mode = values.MODE || "", pitch = Number(values.PITCH || 0);
    var bwCw = Number(values.BW_CW || 0), bw = Number(values.BW_VOICE || 0);
    var band = { tuned: 0, low: 300, high: 300 + bw };
    if (mode === "LSB") { band.low = -300 - bw; band.high = -300; }
    if (mode === "AM") { band.low = -bw; band.high = bw; }
    if (mode === "FT8") { band.tuned = Number(values.TX_PITCH || 0); band.low = 50; band.high = 4000; }
    if (mode === "CW") { band.tuned = pitch; band.low = pitch - bwCw / 2; band.high = pitch + bwCw / 2; }
    if (mode === "CWR") { band.tuned = -pitch; band.low = -pitch - bwCw / 2; band.high = -pitch + bwCw / 2; }
    return band;
  }

  function drawScope() {
    var scale = window.devicePixelRatio || 1;
    var wide = scope.clientWidth, high = scope.clientHeight;
    if (!wide || !high) { return; }
    if (scope.width !== Math.round(wide * scale) || scope.height !== Math.round(high * scale)) {
      scope.width = Math.round(wide * scale); scope.height = Math.round(high * scale);
    }
    var pen = scope.getContext("2d"), graph = Math.round(high * 0.4), i, x, y;
    pen.setTransform(scale, 0, 0, scale, 0, 0);
    pen.fillStyle = "#000d1a"; pen.fillRect(0, 0, wide, graph);
    pen.fillStyle = "#000"; pen.fillRect(0, graph, wide, high - graph);

    if (envelope) {
      // what is being sent, as an oscilloscope shows it
      pen.strokeStyle = "#334050"; pen.beginPath();
      pen.moveTo(0, graph / 2); pen.lineTo(wide, graph / 2); pen.stroke();
      pen.strokeStyle = "#ffe600"; pen.beginPath();
      for (i = 0; i < wide; i++) {
        y = envelope[Math.floor(i * envelope.length / wide)] * (graph / 2) / 255;
        pen.moveTo(i + 0.5, graph / 2 - y - 0.5); pen.lineTo(i + 0.5, graph / 2 + y + 0.5);
      }
      pen.stroke();
    }
    if (points.length < 2 || !spanHz) { return; }
    var range = shownRange(), band = passband();
    function xOf(hz) { return (hz - range.low) * wide / (range.high - range.low); }

    if (!envelope) {
      pen.fillStyle = "rgba(102,102,128,0.35)";
      pen.fillRect(xOf(band.low), 0, xOf(band.high) - xOf(band.low), graph);
      pen.strokeStyle = "#ffe600"; pen.lineWidth = 1; pen.beginPath();
      for (i = 0; i < range.count; i++) {
        x = i * wide / (range.count - 1);
        y = Math.max(0, graph - points[range.first + i] * graph / FULL_SCALE_DB);
        if (i === 0) { pen.moveTo(x, y); } else { pen.lineTo(x, y); }
      }
      pen.stroke();
      pen.strokeStyle = "#ff4d4d"; pen.beginPath();
      pen.moveTo(xOf(band.tuned), 0); pen.lineTo(xOf(band.tuned), graph); pen.stroke();
      pen.fillStyle = "#8fa1b0"; pen.font = (high * 0.055) + "px sans-serif";
      pen.textBaseline = "top";
      if (!isFt8()) { pen.fillText(values.SPAN || "", 4, 3); }
    }
    pen.imageSmoothingEnabled = false;
    pen.drawImage(fall, range.first, 0, range.count, fall.height, 0, graph, wide, high - graph);
    if (band.tuned !== 0 || isFt8()) {
      pen.strokeStyle = "#ff4d4d"; pen.setLineDash([3, 3]); pen.beginPath();
      pen.moveTo(xOf(band.tuned), graph); pen.lineTo(xOf(band.tuned), high); pen.stroke();
      pen.setLineDash([]);
    }
  }

  // a finger drawn along the graph tunes; a tap goes to the next span, or
  // in FT8 puts our pitch where it fell
  var held = null;
  scope.addEventListener("pointerdown", function (e) {
    held = { x: e.clientX, from: e.clientX, moved: false };
    scope.setPointerCapture(e.pointerId);
  });
  scope.addEventListener("pointermove", function (e) {
    if (!held || isFt8()) { return; }
    var steps = Math.trunc((held.x - e.clientX) / PIXELS_A_STEP);
    if (steps !== 0) {
      held.moved = true;
      held.x -= steps * PIXELS_A_STEP;
      tune(steps);
    }
  });
  scope.addEventListener("pointerup", function (e) {
    var was = held;
    held = null;
    if (!was || was.moved || Math.abs(e.clientX - was.from) > 6) { return; }
    if (isFt8()) {
      var box = scope.getBoundingClientRect(), pitch = schema.TX_PITCH;
      if (pitch) {
        var hz = (e.clientX - box.left) * 4000 / box.width;
        hz = Math.round(hz / pitch.step) * pitch.step;
        set("TX_PITCH", Math.max(pitch.min, Math.min(pitch.max, hz)));
      }
    } else if (schema.SPAN) {
      var spans = schema.SPAN.choices, at = spans.indexOf(values.SPAN);
      set("SPAN", spans[(at + 1) % spans.length]);
    }
  });
  scope.addEventListener("wheel", function (e) {
    e.preventDefault();
    tune(e.deltaY < 0 ? 1 : -1);
  }, { passive: false });
  frequency.addEventListener("wheel", function (e) {
    e.preventDefault();
    tune(e.deltaY < 0 ? 1 : -1);
  }, { passive: false });
  window.addEventListener("resize", function () { drawScope(); drawMeter(); drawWheel(); });

  /* the tuning wheel: the window's, for a finger. A tap on the frequency
     brings it up over the list of controls, where it does not hide the
     spectrum, which is what is watched while tuning. A finger taken round
     it turns it, clockwise up, a step of STEP for every ten degrees, 36 to
     the turn; with RIT on it moves the receiver alone, as all tuning here
     does. It is the angle the finger goes through that counts, not where
     it is put down. The dimple goes round with the wheel so that it is
     seen to turn. The hub does not turn and has a cross on it: a tap
     there, or another on the frequency, puts the wheel away. */

  var WHEEL_TICKS = 36, WHEEL_NOTCH = 2 * Math.PI / WHEEL_TICKS;
  var WHEEL_HUB_SHARE = 0.32, WHEEL_DIMPLE_AT = 0.68, WHEEL_DIMPLE_SHARE = 0.16;
  var WHEEL_TICK_SHARE = 0.08, WHEEL_MARGIN = 0.06;
  var wheelAngle = 0, wheelFingerAngle = 0, wheelOwed = 0, isWheelHeld = false;

  function wheelRadius() {
    return Math.min(wheel.clientWidth, wheel.clientHeight) * (0.5 - WHEEL_MARGIN);
  }
  // the angle of the finger from the wheel's middle, growing clockwise,
  // and how far from the middle it is
  function wheelPlace(e) {
    var box = wheel.getBoundingClientRect();
    var across = e.clientX - box.left - box.width / 2, down = e.clientY - box.top - box.height / 2;
    return { angle: Math.atan2(down, across), out: Math.hypot(across, down) };
  }
  function showWheel(on) {
    isWheelHeld = false;
    wheelBox.className = "canvas-box" + (on ? "" : " gone");
    drawWheel();
  }

  function drawWheel() {
    var scale = window.devicePixelRatio || 1;
    var wide = wheel.clientWidth, high = wheel.clientHeight, radius = wheelRadius(), i, angle;
    if (!wide || !high) { return; }
    if (wheel.width !== Math.round(wide * scale) || wheel.height !== Math.round(high * scale)) {
      wheel.width = Math.round(wide * scale); wheel.height = Math.round(high * scale);
    }
    var pen = wheel.getContext("2d"), arm = radius * WHEEL_HUB_SHARE * 0.35;
    pen.setTransform(scale, 0, 0, scale, 0, 0);
    pen.fillStyle = "#101820"; pen.fillRect(0, 0, wide, high);
    pen.strokeStyle = "#ffc000"; pen.lineWidth = 1;
    pen.strokeRect(0.5, 0.5, wide - 1, high - 1);
    pen.translate(wide / 2, high / 2);
    pen.fillStyle = isWheelHeld ? "#384d61" : "#293847";
    pen.beginPath(); pen.arc(0, 0, radius, 0, 2 * Math.PI); pen.fill();
    pen.strokeStyle = "#8fa1b0"; pen.beginPath();
    for (i = 0; i < WHEEL_TICKS; i++) {
      angle = wheelAngle + i * WHEEL_NOTCH;
      pen.moveTo(radius * Math.cos(angle), radius * Math.sin(angle));
      pen.lineTo(radius * (1 - WHEEL_TICK_SHARE) * Math.cos(angle),
        radius * (1 - WHEEL_TICK_SHARE) * Math.sin(angle));
    }
    pen.stroke();
    pen.fillStyle = "#0a0f14"; pen.beginPath();
    pen.arc(radius * WHEEL_DIMPLE_AT * Math.cos(wheelAngle),
      radius * WHEEL_DIMPLE_AT * Math.sin(wheelAngle), radius * WHEEL_DIMPLE_SHARE, 0, 2 * Math.PI);
    pen.fill();
    pen.fillStyle = "#101820"; pen.beginPath();
    pen.arc(0, 0, radius * WHEEL_HUB_SHARE, 0, 2 * Math.PI); pen.fill();
    pen.strokeStyle = "#ffffff"; pen.lineWidth = 2; pen.beginPath();
    pen.moveTo(-arm, -arm); pen.lineTo(arm, arm);
    pen.moveTo(arm, -arm); pen.lineTo(-arm, arm);
    pen.stroke();
  }

  frequency.addEventListener("click", function () {
    showWheel(wheelBox.className.indexOf("gone") >= 0);
  });
  wheel.addEventListener("pointerdown", function (e) {
    var at = wheelPlace(e);
    if (at.out <= wheelRadius() * WHEEL_HUB_SHARE) { showWheel(false); return; }
    isWheelHeld = true;
    wheelFingerAngle = at.angle;
    wheelOwed = 0;
    wheel.setPointerCapture(e.pointerId);
    drawWheel();
  });
  wheel.addEventListener("pointermove", function (e) {
    if (!isWheelHeld) { return; }
    var now = wheelPlace(e).angle, turned = now - wheelFingerAngle, notches;
    // the short way round, across where the angle wraps
    if (turned > Math.PI) { turned -= 2 * Math.PI; }
    if (turned < -Math.PI) { turned += 2 * Math.PI; }
    wheelFingerAngle = now;
    wheelAngle += turned;
    wheelOwed += turned;
    notches = Math.trunc(wheelOwed / WHEEL_NOTCH);
    wheelOwed -= notches * WHEEL_NOTCH;
    if (notches !== 0) { tune(notches); }
    drawWheel();
  });
  function letGoOfWheel() { isWheelHeld = false; drawWheel(); }
  wheel.addEventListener("pointerup", letGoOfWheel);
  wheel.addEventListener("pointercancel", letGoOfWheel);

  /* the meter: the signal's strength on a dial with a needle */

  // While the radio receives, the dial is the signal's strength, S1 to S9
  // and 20 dB over. While it sends, the same dial is the power, nought to
  // a hundred watts; under it are the power and the SWR in figures, and a
  // bar that is the SWR, from nought to 4.5: green to 1.5, yellow to 3
  // and red above. The author asked
  // for the two here: they were written at the end of the status line,
  // where they flickered through what the line was saying.
  function drawMeter() {
    var scale = window.devicePixelRatio || 1;
    var wide = meter.clientWidth, high = meter.clientHeight;
    if (!wide || !high) { return; }
    if (meter.width !== Math.round(wide * scale) || meter.height !== Math.round(high * scale)) {
      meter.width = Math.round(wide * scale); meter.height = Math.round(high * scale);
    }
    var pen = meter.getContext("2d"), sweep = 48 * Math.PI / 180;
    var radius = wide * 0.8 / (2 * Math.sin(sweep)), x = wide / 2, y = high * 0.36 + radius;
    var strength = Number(values.SMETER || 0), units = Math.floor(strength / 100) + (strength % 100) / 6;
    var place = units <= 9 ? units / 9 * 0.7 : 0.7 + Math.min(20, (units - 9) * 6) / 20 * 0.3;
    var watts = Number(values.POWER || 0) / 10, swr = Number(values.SWR || 0) / 10;
    function angle(p) { return -Math.PI / 2 - sweep + 2 * sweep * p; }
    function ray(a, from, to) {
      pen.beginPath();
      pen.moveTo(x + from * Math.cos(a), y + from * Math.sin(a));
      pen.lineTo(x + to * Math.cos(a), y + to * Math.sin(a));
      pen.stroke();
    }
    function mark(p, text) {
      var a = angle(p);
      ray(a, radius, radius * 1.09);
      // a figure at the scale's end is kept inside the dial's edge
      var half = text ? pen.measureText(text).width / 2 + 2 : 0;
      if (text) {
        pen.fillText(text, Math.max(half, Math.min(wide - half, x + radius * 1.15 * Math.cos(a))),
          y + radius * 1.15 * Math.sin(a));
      }
    }
    pen.setTransform(scale, 0, 0, scale, 0, 0);
    pen.fillStyle = "#101820"; pen.fillRect(0, 0, wide, high);
    pen.strokeStyle = "#405060"; pen.lineWidth = 1; pen.strokeRect(0.5, 0.5, wide - 1, high - 1);
    pen.lineWidth = Math.max(1, high / 48);
    pen.strokeStyle = "#d9e0e6"; pen.fillStyle = "#d9e0e6";
    pen.font = (radius * 0.17) + "px sans-serif"; pen.textAlign = "center";
    if (isSending()) {
      pen.beginPath(); pen.arc(x, y, radius, angle(0), angle(1)); pen.stroke();
      mark(0, "0"); mark(0.25, ""); mark(0.5, "50"); mark(0.75, ""); mark(1, "100");
      place = Math.max(0, Math.min(1, watts / 100));
    } else {
      pen.beginPath(); pen.arc(x, y, radius, angle(0), angle(0.7)); pen.stroke();
      [1, 3, 5, 7, 9].forEach(function (unit) { mark(unit / 9 * 0.7, String(unit)); });
      pen.strokeStyle = "#ff4033";
      pen.beginPath(); pen.arc(x, y, radius, angle(0.7), angle(1)); pen.stroke();
      ray(angle(1), radius, radius * 1.09);
    }
    pen.strokeStyle = "#ffe600";
    ray(angle(place), radius * 0.3, radius * 1.09);
    if (!isSending()) { return; }
    // under the dial, over the needle's foot: the power and the SWR in
    // figures, in the middle, "25W 1.3", and under them the SWR's bar
    var foot = high * 0.67, bar = high * 0.07, barLeft = wide * 0.1, barWide = wide * 0.8;
    var barTop = high * 0.89, filled = Math.max(0, Math.min(1, swr / 4.5));
    var zones = [["#30d060", "#17402a"], ["#ffe000", "#4a4410"], ["#ff4033", "#4a1a16"]];
    pen.fillStyle = "#101820"; pen.fillRect(1, foot, wide - 2, high - foot - 1);
    pen.fillStyle = "#ffffff"; pen.textAlign = "center"; pen.textBaseline = "middle";
    pen.font = "bold " + (high * 0.2) + "px sans-serif";
    pen.fillText((watts < 10 ? watts.toFixed(1) : Math.round(watts)) + "W"
      + (swr >= 1 ? " " + swr.toFixed(1) : ""), wide / 2, high * 0.78);
    pen.textBaseline = "alphabetic";
    // the bar is in three parts, each a third of it: nought to 1.5, green,
    // 1.5 to 3, yellow, and from 3, red, the author's; each is dim until
    // the SWR has reached into it, and bright as far as it has. A matched
    // aerial, at 1, fills two thirds of the green
    zones.forEach(function (zone, n) {
      var from = n / 3, lit = Math.max(0, Math.min(1 / 3, filled - from));
      pen.fillStyle = zone[1];
      pen.fillRect(barLeft + barWide * from, barTop, barWide / 3 - 1, bar);
      pen.fillStyle = zone[0];
      pen.fillRect(barLeft + barWide * from, barTop, Math.max(0, barWide * lit - (lit >= 1 / 3 ? 1 : 0)), bar);
    });
  }

  /* FT8's messages */

  function takeMessage(line) {
    var fields = line.split("|");
    if (fields.length < 4) { return; }
    var text = fields.slice(3).join("|"), mine = (values.MYCALLSIGN || "").toUpperCase();
    var row = make("div", "", fields[0].slice(-2) + " " + ("   " + fields[1]).slice(-3) + " " + text,
      messages);
    var slot = Math.floor((Number(fields[0].slice(2, 4)) * 60 + Number(fields[0].slice(4, 6))) / 15);
    row.className = (slot % 2 ? "other-slot " : "")
      + (fields[1] === "TX" ? "sent"
        : mine && text.toUpperCase().replace(/[<>]/g, " ").split(" ").indexOf(mine) >= 0 ? "to-us"
        : text.indexOf("CQ ") === 0 ? "calling" : "");
    row.addEventListener("click", function () {
      if (marked === row) {
        send("answer " + line);
        row.className = row.className.replace(" marked", "");
        marked = null;
        return;
      }
      if (marked) { marked.className = marked.className.replace(" marked", ""); }
      marked = row;
      row.className += " marked";
      say("tap it again to answer");
    });
    while (messages.childNodes.length > 200) { messages.removeChild(messages.firstChild); }
    if (!marked) { messages.scrollTop = messages.scrollHeight; }
  }

  /* the settings and the logbook, which come up over the page */

  function openPanel(title, search) {
    panel.textContent = "";
    var bar = make("div", "bar", null, panel);
    make("button", "", "←", bar).addEventListener("click", function () {
      panel.className = "";
      drawScope();
    });
    make("h2", "", title, bar);
    if (search) { bar.appendChild(search); }
    panel.className = "shown";
    return make("div", "inside", null, panel);
  }

  function openSettings() {
    var leaving = document.createElement("button");
    leaving.className = "leave";
    leaving.textContent = "Leave the radio";
    leaving.addEventListener("click", function () { leave(); });
    var inside = openPanel("Settings of " + NAME, leaving);
    order.forEach(function (name) {
      var control = schema[name];
      // the radio's CONNECTIONS, REMOTE and STREAMING among them, are not
      // offered here: they are set at the radio and nowhere else, the
      // author's rule. What the radio says of its link is shown
      if (!control || (control.group !== "STATION" && control.group !== "REMOTE")
          || control.kind === "action" || control.kind === "number") {
        return;
      }
      var row = make("div", "row", null, inside);
      make("div", "title", name, row);
      var value = make("div", "value", values[name] || "", row);
      if (control.group === "STATION") {
        row.addEventListener("click", function () {
          if (control.kind === "text") {
            typeText(name, values[name] || "", function (text) { set(name, text); value.textContent = text; });
          } else {
            adjust(name);
            value.textContent = values[name] || "";
          }
        });
      }
    });
  }

  var logBody = null;
  function openLog() {
    var search = document.createElement("input");
    search.type = "text"; search.placeholder = "callsign"; search.value = logQuery;
    search.autocapitalize = "characters"; search.autocomplete = "off";
    search.addEventListener("input", function () {
      logQuery = search.value.trim().toUpperCase();
      send("log " + logQuery);
    });
    var inside = openPanel("Logbook", search);
    var entry = make("div", "entry", null, panel);
    panel.insertBefore(entry, inside);
    ["CALL", "SENT", "RECV", "EXCH", "NR"].forEach(function (name) {
      if (!schema[name]) { return; }
      var label = make("label", "", name, entry), box = document.createElement("input");
      box.type = "text"; box.value = values[name] || ""; box.autocapitalize = "characters";
      box.autocomplete = "off";
      box.addEventListener("change", function () {
        values[name] = box.value.trim().toUpperCase();
        send(name + " " + values[name]);
      });
      label.appendChild(box);
    });
    make("button", "", "SAVE", entry).addEventListener("click", function () {
      send("LOGGER SAVE");
      setTimeout(openLog, 400);
    });
    make("button", "", "WIPE", entry).addEventListener("click", function () {
      send("LOGGER WIPE");
      setTimeout(openLog, 400);
    });
    var table = make("table", "", null, inside);
    var head = make("tr", "", null, table);
    ["Date", "UTC", "kHz", "Mode", "Station", "Sent", "Received", ""].forEach(function (name) {
      make("th", "", name, head);
    });
    logBody = table;
    send("log " + logQuery);
  }

  function logBegins() {
    if (!logBody) { return; }
    while (logBody.rows.length > 1) { logBody.deleteRow(1); }
  }

  function logRow(line) {
    if (!logBody || panel.className !== "shown") { return; }
    var fields = line.split("|"), row = make("tr", "", null, logBody);
    fields.slice(1, 8).forEach(function (field) { make("td", "", field, row); });
    make("button", "", "delete", make("td", "", null, row)).addEventListener("click", function () {
      if (window.confirm("Delete the entry with " + fields[5] + " of " + fields[1] + "?")) {
        send("logdel " + fields[0] + " " + logQuery);
      }
    });
  }

  /* the radio's sound */

  var player = null, sound = null;

  function setLoudness() {
    if (player) { player.setLoudness(isMuted ? 0 : Number(own.VOL) / 100); }
  }

  function listen() {
    sound = new WebSocket(SOCKETS + "/listen?radio=" + RADIO);
    sound.binaryType = "arraybuffer";
    sound.onmessage = function (message) {
      if (typeof message.data !== "string") {
        player.play(new Uint8Array(message.data));
      } else if (message.data.indexOf("lost ") === 0) {
        player.lost(Number(message.data.slice(5)));
      }
    };
    sound.onclose = function () { setTimeout(listen, 3000); };
  }

  /* the whole screen, and leaving

     The page asks for the whole screen with the touch on Start, since a
     browser gives it only to a touch, and then asks that the screen stay
     on its side. An iPhone's browser gives neither; there the page is put
     on the home screen (Share, Add to Home Screen) and opened from it.

     What the author found on his phone on 2026-10-12, and what is done:

     - Turned upright, the page said to turn it back, and turned back it
       was no longer on the whole screen, with nothing to say so or to take
       it back but a touch on the status line that nobody would guess at.
       Now, whenever the whole screen is lost and the page did not leave it
       itself, a notice comes up over the page with three buttons: back to
       the whole screen, stay as it is, or leave the radio. A browser gives
       the whole screen back only to a touch, so the page cannot take it
       back by itself.
     - On the whole screen there is none of the browser left to close the
       page with, and he had to turn the phone upright to get out. "Leave
       the radio" is now in that notice, on the page that says to turn the
       phone, and in the settings. It stops the radio sending, as closing
       the page does, gives the screen back and goes to the list of radios.
     - Why the screen did not stay on its side is not known: a refusal was
       thrown away. It is now said on the status line, and noted.

     note() tells the service's log what the browser did: the whole screen
     given or lost, the screen held or not and the browser's reason, each
     turn of the phone. It is a request for an address that does not exist,
     with the note in it, which the web server writes down like any other.
     It is there because these things happen on a phone in the owner's hand
     and nowhere that can be looked at; it says nothing of the radio or of
     him. */

  var isStarted = false, isLeaving = false;

  // opened from the telephone's home screen the page is an application: it
  // has the whole screen and is held on its side by the telephone, with
  // nothing to ask for and nothing to lose. accounts.py's manifest for
  // this page says how it comes to be there
  function isInstalled() {
    return window.matchMedia("(display-mode: fullscreen)").matches
      || window.matchMedia("(display-mode: standalone)").matches;
  }

  function note(text) {
    var facing = screen.orientation ? screen.orientation.type : "?";
    try {
      fetch("/note?" + encodeURIComponent(text + " | " + facing + " " + window.innerWidth + "x"
        + window.innerHeight + (hasWholeScreen() ? " whole" : " window")),
        { cache: "no-store" }).catch(function () {});
    } catch (unable) { /* a note is not worth an error */ }
  }

  function hasWholeScreen() {
    return !!(document.fullscreenElement || document.webkitFullscreenElement);
  }

  function holdSideways() {
    if (!screen.orientation || !screen.orientation.lock) { note("no way to hold the screen"); return; }
    screen.orientation.lock("landscape").then(function () {
      note("screen held sideways");
    }, function (refused) {
      note("screen not held: " + refused.name + ": " + refused.message);
      say("this browser will not hold the screen on its side: " + refused.message);
    });
  }

  function fillScreen() {
    var root = document.documentElement;
    var ask = root.requestFullscreen || root.webkitRequestFullscreen;
    if (isInstalled()) { return; }
    if (!ask) { note("no whole screen to ask for"); return; }
    if (hasWholeScreen()) { return; }
    // a browser that neither gives nor refuses, which the author's did
    // after a page had left the whole screen in a hurry: the notice comes
    // up, where another touch can ask again
    setTimeout(function () {
      if (isStarted && !isLeaving && !hasWholeScreen()) {
        note("whole screen neither given nor refused");
        away.className = "";
      }
    }, 1500);
    function refused(why) {
      note("whole screen refused: " + why.name + ": " + why.message);
      say("this browser did not give the whole screen: " + why.message);
    }
    try {
      var asked = ask.call(root, { navigationUI: "hide" });
      if (asked && asked.then) { asked.then(holdSideways, refused); }
    } catch (why) {
      refused(why);
    }
  }
  status.addEventListener("click", fillScreen);

  var away = make("div", "gone", null, document.body);
  away.id = "away";
  make("p", "", "The page is not on the whole screen.", away);
  make("p", "small", "A browser takes the whole screen back when it likes. To keep it for good, "
    + "choose Add to Home screen in the browser's menu, and open the radio from there.", away);
  var awayButtons = make("div", "", null, away);
  make("button", "main", "Whole screen", awayButtons).addEventListener("click", fillScreen);
  make("button", "", "Stay as it is", awayButtons).addEventListener("click", function () {
    away.className = "gone";
  });
  make("button", "", "Leave the radio", awayButtons).addEventListener("click", function () {
    leave();
  });

  function onWholeScreenChange() {
    note(hasWholeScreen() ? "whole screen given" : "whole screen lost");
    away.className = (hasWholeScreen() || !isStarted || isLeaving) ? "gone" : "";
    drawScope(); drawMeter(); drawWheel();
  }
  document.addEventListener("fullscreenchange", onWholeScreenChange);
  document.addEventListener("webkitfullscreenchange", onWholeScreenChange);
  if (screen.orientation && screen.orientation.addEventListener) {
    screen.orientation.addEventListener("change", function () { note("turned"); });
  }

  // the radio is stopped sending by the service when the page's connection
  // closes; this closes it, and does not wait for the browser to
  function leave() {
    isLeaving = true;
    note("leaving");
    if (retry) { clearTimeout(retry); }
    if (work) { work.onclose = null; work.close(); }
    if (sound) { sound.onclose = null; sound.close(); }
    if (player) { player.close(); }
    // the whole screen is given back before the page goes, and the page
    // waits for that: leaving at once left the author's browser unable to
    // give the whole screen to the next page
    var out = document.exitFullscreen || document.webkitExitFullscreen;
    var given = null;
    function go() { window.location.href = "/"; }
    if (out && hasWholeScreen()) {
      try { given = out.call(document); } catch (unable) { /* going anyway */ }
    }
    if (given && given.then) {
      given.then(go, go);
      setTimeout(go, 1000);
    } else {
      go();
    }
  }

  /* beginning: a browser will make no sound until the page has been touched */

  var start = make("div", "", null, document.body);
  start.id = "start";
  make("h1", "", NAME, start);
  make("p", "", "Hold the phone on its side. The radio's sound plays through this phone, at the "
    + "loudness set by VOL here.", start);
  make("button", "", "Start", start).addEventListener("click", function () {
    start.className = "gone";
    isStarted = true;
    note("started");
    player = radioSound();
    setLoudness();
    fillScreen();
    listen();
    connect();
    show();
    drawScope();
  });
  show();
})();
