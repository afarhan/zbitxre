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
//   rows 3 to 7, on the left: LOG, SET, TERM, and one more for the mode
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
// The radio's sound comes as it does to anyone listening from the globe,
// over a second WebSocket, /listen: twenty milliseconds of mu-law to a
// message, played by sound.js, which says how. VOL is the loudness in this browser and is not sent to the
// radio: the author's rule is that whoever listens from afar sets their
// own volume.
//
// TUNING
//
// The radio knows only FREQ. STEP and RIT are this page's own, as they are
// the window's. A finger drawn along the graph or the waterfall tunes, a
// step for every few pixels, and so does a mouse's wheel. With RIT on, it
// is the receiver's offset that moves, and the dial stays: the large
// figures are where the receiver listens and the small line where the
// transmitter is to be. SPLIT and the second VFO are not here yet.
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
    "STACK"];
  var FULL_SCALE_DB = 80, RIT_MOST_HZ = 25000, PIXELS_A_STEP = 14;

  var values = {};        // every control's value, as the radio tells them
  var schema = {};        // name -> { group, kind, min, max, step, choices }
  var order = [];         // the controls' names in the radio's order
  var own = { STEP: stored("step", "1K"), RIT: "OFF", VOL: stored("vol", "60") };
  var dialHz = 0, ritHz = 0;
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
  tile("RIT", 5, 1, "RIT", function () {
    own.RIT = own.RIT === "ON" ? "OFF" : "ON";
    if (own.RIT === "OFF") { ritHz = 0; }
    sendFrequency();
    show();
  });
  tile("SPLIT", 6, 1, "SPLIT", null).box.className += " dim";
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
  var power = make("div", "", "", middle);
  power.id = "power";
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
    if (name === "MODULATION") { envelope = unhex(value); drawScope(); return; }
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
    if (name === "FREQ") { dialHz = Number(value) - (own.RIT === "ON" ? ritHz : 0); }
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

  var freqTimer = null;
  function sendFrequency() {
    values.FREQ = String(dialHz + (own.RIT === "ON" ? ritHz : 0));
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
    if (own.RIT === "ON") {
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
    tiles.RIT.value.textContent = own.RIT;
    tiles.RIT.box.className = "tile" + (own.RIT === "ON" ? " on" : "");
    tiles.SPLIT.value.textContent = "OFF";
    tiles.DRIVE.value.textContent = values.DRIVE || "";
    tiles.AGC.value.textContent = values.AGC || "";
    tiles.IF.value.textContent = values.IF || "";
    tiles.VOL.value.textContent = own.VOL;
    tiles.MODE.value.textContent = values.MODE || "";
    BANDS.forEach(function (band) {
      var t = tiles["BAND " + band];
      t.value.textContent = band;
      t.box.className = "tile button" + (values.BAND === band ? " in-use" : "");
    });

    var listensAt = dialHz + (own.RIT === "ON" ? ritHz : 0);
    frequency.className = "tile" + (isSending() ? " sending" : "");
    if (own.RIT === "ON") {
      waiting.textContent = (isSending() ? "RX:" : "TX:") + dotted(isSending() ? listensAt : dialHz);
      figures.textContent = (isSending() ? "TX:" : "RX:") + dotted(isSending() ? dialHz : listensAt);
    } else {
      waiting.textContent = NAME;
      figures.textContent = (isSending() ? "TX:" : "A:") + dotted(dialHz);
    }

    var labels = ["LOG", "SET", "TERM", ""];
    if (group === "CW") { labels[3] = "ESC"; }
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

    var watts = Number(values.POWER || 0), swr = Number(values.SWR || 0);
    power.textContent = watts > 0 ? (watts / 10).toFixed(1) + " W  SWR " + (swr / 10).toFixed(1) : "";
    power.style.display = watts > 0 ? "block" : "none";

    showList();
    drawMeter();
  }

  function footerLabel(place, group, macros) {
    if (group === "VOICE") { return ["", "TX", "RX", "", "", ""][place]; }
    if (group === "2TONE") { return ["TX", "RX", "", "", "", ""][place]; }
    if (group === "CW") { return macros[place] || ""; }
    return "";
  }

  function footerTapped(place) {
    var label = footer[place].value.textContent;
    if (label === "TX") { set("TX", "ON"); }
    else if (label === "RX") { set("TX", "OFF"); }
    else if (label && modeGroup() === "CW") { send("MACRO " + label); }
  }

  function columnTapped(place) {
    var label = column[place].value.textContent;
    if (label === "LOG") { openLog(); }
    if (label === "SET") { openSettings(); }
    if (label === "ESC") { send("CW_DO ESC"); }
    if (label === "CQ") { send("FT8_DO CQ"); }
    if (label === "HALT") { send("FT8_DO HALT"); }
  }

  /* the list of controls */

  var rows = [];
  function buildList() {
    list.textContent = "";
    rows = [];
    ["TX", "BW"].concat(order).forEach(function (name) {
      var control = schema[name];
      if (name !== "BW" && (!control || control.kind === "action" || control.kind === "text"
          || PLACED.indexOf(name) >= 0)) {
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
    if (name === "TX") { return group !== "VOICE" && group !== "2TONE" && group !== "FT8"; }
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
    function angle(p) { return -Math.PI / 2 - sweep + 2 * sweep * p; }
    function ray(a, from, to) {
      pen.beginPath();
      pen.moveTo(x + from * Math.cos(a), y + from * Math.sin(a));
      pen.lineTo(x + to * Math.cos(a), y + to * Math.sin(a));
      pen.stroke();
    }
    pen.setTransform(scale, 0, 0, scale, 0, 0);
    pen.fillStyle = "#101820"; pen.fillRect(0, 0, wide, high);
    pen.strokeStyle = "#405060"; pen.lineWidth = 1; pen.strokeRect(0.5, 0.5, wide - 1, high - 1);
    pen.lineWidth = Math.max(1, high / 48);
    pen.strokeStyle = "#d9e0e6"; pen.fillStyle = "#d9e0e6";
    pen.beginPath(); pen.arc(x, y, radius, angle(0), angle(0.7)); pen.stroke();
    pen.font = (radius * 0.17) + "px sans-serif"; pen.textAlign = "center";
    [1, 3, 5, 7, 9].forEach(function (unit) {
      var a = angle(unit / 9 * 0.7);
      ray(a, radius, radius * 1.09);
      pen.fillText(String(unit), x + radius * 1.15 * Math.cos(a), y + radius * 1.15 * Math.sin(a));
    });
    pen.strokeStyle = "#ff4033";
    pen.beginPath(); pen.arc(x, y, radius, angle(0.7), angle(1)); pen.stroke();
    ray(angle(1), radius, radius * 1.09);
    pen.strokeStyle = "#ffe600";
    ray(angle(place), radius * 0.3, radius * 1.09);
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
      // REMOTE is not offered here: turned off from afar, the radio could
      // not be reached to turn it on again
      if (!control || name === "REMOTE" || (control.group !== "STATION" && control.group !== "REMOTE")
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
    if (player) { player.setLoudness(Number(own.VOL) / 100); }
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
    if (!ask) { note("no whole screen to ask for"); return; }
    if (hasWholeScreen()) { return; }
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
  make("p", "", "The page is no longer on the whole screen.", away);
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
    var out = document.exitFullscreen || document.webkitExitFullscreen;
    if (out && hasWholeScreen()) {
      try { out.call(document); } catch (unable) { /* going anyway */ }
    }
    window.location.href = "/";
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
