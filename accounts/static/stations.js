// stations.js: the Listen buttons of /stations, the radios as a list
//
// The list is for the blind operators who cannot use the globe, the
// author's asking of 2026-10-20. Each radio on line has a button, "Listen
// to VU2ESE". Pressing it plays that radio, as the globe does, through
// sound.js and the service's /listen; pressing it again, or another one,
// stops it. One radio is heard at a time.
//
// What is going on is said in one line above the list, which a screen
// reader reads out as it changes: whom we are listening to, what the radio
// is tuned to, as the radio says it while it is listened to, and whether
// it has gone off line or the connection was lost. The button's own words
// change to "Stop VU2ESE" while it plays.

(function () {
  "use strict";

  var now = document.getElementById("now");
  var buttons = document.querySelectorAll("button.listen");
  var chosen = null, player = null, socket = null, again = null;
  var isWanted = false, isFull = false;

  function say(words) {
    now.hidden = words === "";
    now.textContent = words;
  }

  function megahertz(hz) {
    var digits = ("000000" + hz).slice(-Math.max(7, hz.length));
    return digits.slice(0, -6).replace(/^0+(?=\d)/, "") + "." + digits.slice(-6, -3) + "."
      + digits.slice(-3) + " MHz";
  }

  function stop(words) {
    isWanted = false;
    if (again) { clearTimeout(again); again = null; }
    if (socket) { socket.onclose = null; socket.close(); socket = null; }
    if (player) { player.close(); player = null; }
    if (chosen) { chosen.textContent = "Listen to " + chosen.dataset.callsign; }
    chosen = null;
    say(words);
  }

  function listen() {
    var callsign = chosen.dataset.callsign;
    again = null;
    isFull = false;
    socket = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://")
      + location.host + "/listen?radio=" + chosen.dataset.radio);
    socket.binaryType = "arraybuffer";
    socket.onmessage = function (message) {
      if (typeof message.data !== "string") {
        player.play(new Uint8Array(message.data));
        return;
      }
      var words = message.data.split(" ");
      if (words[0] === "lost") {
        player.lost(Number(words[1]));
      } else if (words[0] === "tuned") {
        say("Listening to " + callsign + ", " + megahertz(words[1]) + " " + words[2]);
      } else if (message.data === "online") {
        say("Listening to " + callsign);
      } else if (message.data === "offline") {
        say(callsign + " is not on line; waiting for it");
      } else if (message.data === "closed") {
        stop(callsign + " is not streaming");
      } else if (message.data === "full") {
        isFull = true;
        say(callsign + " has as many listeners as it can take; waiting for room");
      }
    };
    socket.onclose = function () {
      socket = null;
      if (!isWanted) { return; }
      if (!isFull) { say("The connection to " + callsign + " was lost; trying again"); }
      again = setTimeout(listen, isFull ? 30000 : 3000);
    };
  }

  function start(button) {
    chosen = button;
    chosen.textContent = "Stop " + chosen.dataset.callsign;
    player = radioSound();
    isWanted = true;
    say("Connecting to " + chosen.dataset.callsign);
    listen();
  }

  Array.prototype.forEach.call(buttons, function (button) {
    button.addEventListener("click", function () {
      var wasChosen = chosen === button;
      if (chosen) { stop(wasChosen ? "Stopped" : ""); }
      if (!wasChosen) { start(button); }
    });
  });
}());
