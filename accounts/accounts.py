#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
accounts.py — the accounts of sbitx and zbitx owners, at my.sbitx.net.

    accounts.py serve  [settings.ini]         the web service
    accounts.py list   [settings.ini]         and the rest: customer service's
                                              own commands, see ADMIN below

It needs Python 3 and nothing else: everything used is in Python's standard
library, and what it keeps is one SQLite file. The author asked for as few
dependencies as could be. It listens on this machine only, and nginx, which
has the certificate, passes the world's requests to it.

WHO HAS AN ACCOUNT

Anyone may register, with a callsign and an email address. The callsign is
the user's name here. A link is mailed to the address; following it proves
the address is theirs, and there they choose a password. After that they
sign in with the callsign and the password. Nothing proves the callsign is
theirs: the first to register one has it. The author's ruling is that a
callsign in dispute is settled by customer service by hand, with a scan of
the licence, and that once the service is paid for, squatting will cost
money. Payment is not built yet.

Registration is open, by invitation or closed, as the settings say. By
invitation, the form asks for a code that the test group is given.

An account is "pending" until its address is verified, then "active", and
"suspended" when customer service has shut it: a suspended account cannot
sign in and its radios' keys are refused.

A RADIO, ITS COOKIE AND ITS CLIENT PASSWORD

An owner has up to five radios here. Each has a name of the owner's
choosing, home or zbitx, and is known by the callsign with that after it:
vu2ese-home, vu2ese-zbitx. The author's design.

A radio is not given a secret by this site: it makes its own. The first
time remote access is turned on, the radio makes a cookie of twelve letters
and shows it among its settings, "abcd-jklm-wxyz". The owner, signed in
here, adds a radio by giving it a name and typing the cookie. Nothing is
typed on the radio, which on a zbitx has no keyboard, and no file on it is
edited. From then on the cookie is what the radio answers this service's
challenge with, and the radio no longer shows it.

Each radio also has a client password, twelve letters that this site makes
and shows beside the radio. That, with the radio's name, is what the owner
types into a client, the window on another computer, to reach the radio
from afar. It is apart from the cookie so that it can be changed here, when
a laptop is lost, without anything being done at the radio, and so that
whoever has it cannot pass for the radio.

Neither is ever sent to remote.py, the part of this service that is reached
over plain TCP: each is the secret in the answer to a challenge. PROTOCOL.md
says how. To check an answer the service has to work out the same answer,
so both are kept here as they are and not as hashes; the client password
has to be shown to the owner anyway. The database is readable by the
service's user alone.

WHAT IS KEPT, AND HOW

    accounts   callsign, email, the password's hash, status, a note
    tokens     the links mailed out, to verify an address or reset a password
    sessions   who is signed in, in which browser
    radios     each radio's name, its cookie and its client password; and
               from remote.py, whether it is on line now and the grid
               locator set on it, which the radio's daemon tells the service
               along with the callsign, as the author asked
    snapshots  the newest copy of each radio's settings and logbook, which
               remote.py takes when the radio comes on line
    attempts   what has been tried lately, to slow down guessing
    spent      the proof-of-work challenges already answered

A password is kept only as scrypt's hash of it with a salt of its own, at
a cost of some 32 MB and a tenth of a second a try. Mailed links and
sessions are long random strings, and what is kept of each is its SHA-256,
so neither can be used by reading the file. A link works once and for a
limited time.

GUARDING THE FORMS

A sign-in is refused after five wrong passwords for a callsign, or twenty
from one address, in a quarter of an hour. Every form posted must come from
this site (the browser's Origin header), and when signed in must carry a
value tied to the session, so that another site cannot post in the user's
name. The session's cookie is not readable by scripts and is sent only over
HTTPS and only to this site.

PROOF OF WORK ON THE REGISTRATION FORM

The author asked for a proof-of-work captcha, so that spammers do not stuff
the registration form. The form comes with a challenge, a random string
that this service has signed and that runs out in ten minutes. Before the
form can be sent, the browser has to find a number which, put after the
challenge, gives a SHA-256 that begins with so many zero bits, 18 unless
the settings say otherwise: some quarter of a million tries, a second or
so on a computer and a few on a phone. The service checks the answer with
one hash, and takes each challenge once only, so an answer cannot be used
again. It costs a person nothing they notice and a program that fills the
form ten thousand times a few hours of processor for each run.

It does not tell a person from a program, as a picture puzzle tries to: a
program that is willing to do the work gets through, one registration at a
time, and the limit on tries from one address is still what holds it there.

The work is done by pow.js, the only script on the site, which is served
from here and does nothing else. With scripts turned off the form cannot
be sent, and says so.

LISTENING TO A RADIO

Beside each of the owner's radios is "listen", a page with a button and a
slider. The author asked for a player in the browser. Its script,
listen.js below, opens a WebSocket to /listen, which nginx hands to
remote.py, and is sent the radio's sound as it comes, twenty milliseconds
of mu-law to a message; remote.py says how the sound reaches it and who may
listen. The script turns each message into samples and plays them one
after another through the browser's Web Audio, a tenth of a second behind
their arrival so that an uneven network does not break the sound up, and
lets go of sound that has fallen more than half a second behind. The
slider is the loudness here, in this browser, and nothing at the radio:
the author's rule is that whoever listens from afar sets their own volume.

A browser has no UDP, so this last stretch is TCP, inside the site's HTTPS.

THE MAP

/map is a page anyone may see, signed in or not: a map of the world with
the radios that are on line now, each at the middle of its grid square with
its owner's callsign beside it. The author asked for it, after
pskreporter's. It asks /map.json for the radios every half minute. What is
told of a radio there is the callsign and the first four characters of the
grid set on it, a square some hundred kilometres across, and nothing else:
not the radio's name, and not the finer square, which would come near to
saying which house. A radio with no grid set is not on the map.

The map is drawn by Leaflet, which is in static/ beside this file as its
authors published it, with its licence, and is served from here; the
pictures of the map itself are OpenStreetMap's, fetched by the visitor's
browser from OpenStreetMap, which is the one thing on this site that comes
from somewhere else. OpenStreetMap's servers are run by volunteers and have
a policy for their use: the page must say whose pictures they are, which it
does, and must say where it is when it asks for them, which is why the
map's page alone lets other sites be told this site's name. The map's own script is map.js, below.

MAIL

Mail is sent through the SMTP server named in the settings, over TLS. If
the settings name a user, the service signs in to it with the password in
the file they name. With no user it does not sign in: that is for a relay
that takes mail from this machine's address, as Google Workspace's SMTP
relay can be set to, so that no mailbox's password is kept here at all. The
service introduces itself by the site's own name. With no server named,
each mail is written to the outbox file instead, which is how the service
is tried before any of that exists.

ADMIN

Customer service works on the droplet, with this same program:

    list                      every account
    show CALLSIGN             one account and its radios
    suspend CALLSIGN          shut it; restore CALLSIGN opens it again
    set-email CALLSIGN EMAIL  after the owner has shown who they are
    rename OLD NEW            a changed callsign, or one given to its owner
    delete CALLSIGN           the account, its radios and its sessions
    note CALLSIGN TEXT        a line kept with the account

There is no admin page on the web: what is not there cannot be broken into.
"""

import argparse
import configparser
import contextlib
import hashlib
import hmac
import html
import json
import os
import re
import secrets
import smtplib
import sqlite3
import sys
import threading
import time
import urllib.parse
from email.message import EmailMessage
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERIFY_LINK_S = 48 * 3600
RESET_LINK_S = 2 * 3600
SESSION_S = 30 * 24 * 3600
WINDOW_S = 15 * 60
SIGNIN_TRIES_A_CALLSIGN = 5
SIGNIN_TRIES_AN_ADDRESS = 20
MAILS_AN_ADDRESS = 6
PASSWORD_SHORTEST = 10
PASSWORD_LONGEST = 200
BODY_LONGEST = 16 * 1024
RADIOS_AN_ACCOUNT = 5
SCRYPT_N = 2 ** 15
SCRYPT_R = 8
SCRYPT_P = 1
SCRYPT_MEMORY = 64 * 1024 * 1024
HASHES_AT_ONCE = 2
POW_BITS = 18
POW_LIFE_S = 10 * 60
# a cookie and a client password are twelve letters, written in fours, of an
# alphabet without the letters that are mistaken for one another or for
# figures: no i, l or o. zbitx_remote.c makes a radio's cookie of the same
SECRET_LETTERS = "abcdefghjkmnpqrstuvwxyz"
SECRET_LENGTH = 12
SECRET_GROUP = 4
# a secret is stretched before it is used, so that twelve letters are slow
# to guess for someone who overheard an answer to a challenge
STRETCH_ROUNDS = 50000

CALLSIGN_FORM = re.compile(r"^[A-Z0-9]{3,10}$")
GRID_FORM = re.compile(r"^[A-R]{2}[0-9]{2}([A-X]{2})?$")
SUFFIX_FORM = re.compile(r"^[a-z0-9]{1,10}$")
EMAIL_FORM = re.compile(r"^[^@\s]{1,64}@[^@\s]{1,190}\.[^@\s.]{2,}$")

SCHEMA = """
create table if not exists accounts (
    id integer primary key,
    callsign text not null unique,
    email text not null,
    password text,
    status text not null default 'pending',
    created integer not null,
    note text not null default ''
);
create table if not exists tokens (
    hash text primary key,
    account integer not null references accounts(id) on delete cascade,
    purpose text not null,
    expires integer not null
);
create table if not exists sessions (
    hash text primary key,
    account integer not null references accounts(id) on delete cascade,
    expires integer not null
);
create table if not exists radios (
    id integer primary key,
    account integer not null references accounts(id) on delete cascade,
    suffix text not null,
    cookie text not null,
    client_password text not null,
    created integer not null,
    last_seen integer,
    online integer not null default 0,
    grid text not null default '',
    unique (account, suffix)
);
create table if not exists snapshots (
    radio integer primary key references radios(id) on delete cascade,
    taken integer not null,
    controls text not null,
    log text not null
);
create table if not exists attempts (
    bucket text not null,
    at integer not null
);
create index if not exists attempts_bucket on attempts(bucket, at);
create table if not exists spent (
    challenge text primary key,
    expires integer not null
);
"""

# the browser's side of the proof of work: for every form that has a
# challenge, numbers are tried, a few thousand at a time so that the page
# stays alive, until one gives a SHA-256 with enough zero bits in front
POW_SCRIPT = """
"use strict";
(function () {
  var K = [];
  (function () {
    var n = 2, count = 0;
    function frac(x) { return ((x - Math.floor(x)) * 4294967296) | 0; }
    while (count < 64) {
      var prime = true;
      for (var d = 2; d * d <= n; d++) { if (n % d === 0) { prime = false; break; } }
      if (prime) { K[count++] = frac(Math.pow(n, 1 / 3)); }
      n++;
    }
  })();
  function zeroBits(text) {
    var bytes = [], i, j;
    for (i = 0; i < text.length; i++) { bytes.push(text.charCodeAt(i) & 255); }
    var bitLength = bytes.length * 8;
    bytes.push(128);
    while (bytes.length % 64 !== 56) { bytes.push(0); }
    for (i = 7; i >= 0; i--) { bytes.push(i >= 4 ? 0 : (bitLength >>> (i * 8)) & 255); }
    var h = [1779033703, -1150833019, 1013904242, -1521486534,
             1359893119, -1694144372, 528734635, 1541459225];
    var w = new Array(64);
    for (var block = 0; block < bytes.length; block += 64) {
      for (i = 0; i < 16; i++) {
        j = block + i * 4;
        w[i] = (bytes[j] << 24) | (bytes[j + 1] << 16) | (bytes[j + 2] << 8) | bytes[j + 3];
      }
      for (i = 16; i < 64; i++) {
        var a15 = w[i - 15], a2 = w[i - 2];
        var s0 = ((a15 >>> 7) | (a15 << 25)) ^ ((a15 >>> 18) | (a15 << 14)) ^ (a15 >>> 3);
        var s1 = ((a2 >>> 17) | (a2 << 15)) ^ ((a2 >>> 19) | (a2 << 13)) ^ (a2 >>> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
      }
      var a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
      for (i = 0; i < 64; i++) {
        var S1 = ((e >>> 6) | (e << 26)) ^ ((e >>> 11) | (e << 21)) ^ ((e >>> 25) | (e << 7));
        var t1 = (hh + S1 + ((e & f) ^ (~e & g)) + K[i] + w[i]) | 0;
        var S0 = ((a >>> 2) | (a << 30)) ^ ((a >>> 13) | (a << 19)) ^ ((a >>> 22) | (a << 10));
        var t2 = (S0 + ((a & b) ^ (a & c) ^ (b & c))) | 0;
        hh = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
      }
      h[0] = (h[0] + a) | 0; h[1] = (h[1] + b) | 0; h[2] = (h[2] + c) | 0; h[3] = (h[3] + d) | 0;
      h[4] = (h[4] + e) | 0; h[5] = (h[5] + f) | 0; h[6] = (h[6] + g) | 0; h[7] = (h[7] + hh) | 0;
    }
    var first = h[0] >>> 0;
    if (first !== 0) { return Math.clz32(first); }
    return 32 + Math.clz32(h[1] >>> 0);
  }
  function work(form) {
    var challenge = form.elements["pow_challenge"].value;
    var bits = parseInt(form.getAttribute("data-pow-bits"), 10);
    var button = form.querySelector("button");
    var label = button.textContent;
    var n = 0;
    button.disabled = true;
    button.textContent = "One moment...";
    function some() {
      for (var i = 0; i < 4000; i++, n++) {
        if (zeroBits(challenge + ":" + n) >= bits) {
          form.elements["pow_answer"].value = String(n);
          button.disabled = false;
          button.textContent = label;
          return;
        }
      }
      setTimeout(some, 0);
    }
    some();
  }
  var forms = document.querySelectorAll("form[data-pow-bits]");
  for (var i = 0; i < forms.length; i++) { work(forms[i]); }
})();
"""

# the player's script. A table turns each byte of mu-law into a sample; the
# samples of each message are a buffer played right after the one before
LISTEN_SCRIPT = """
"use strict";
(function () {
  var player = document.getElementById("player");
  var button = document.getElementById("listen");
  var slider = document.getElementById("volume");
  var state = document.getElementById("state");
  var table = new Float32Array(256);
  for (var i = 0; i < 256; i++) {
    var u = ~i & 255;
    var size = (((u & 15) << 3) + 132) << ((u & 112) >> 4);
    table[i] = ((u & 128) ? (132 - size) : (size - 132)) / 32768;
  }
  var context = null, gain = null, socket = null, playAt = 0;

  function play(bytes) {
    var now = context.currentTime;
    if (playAt > now + 0.5) { return; }
    if (playAt < now + 0.02) { playAt = now + 0.1; }
    var buffer = context.createBuffer(1, bytes.length, 8000);
    var samples = buffer.getChannelData(0);
    for (var i = 0; i < bytes.length; i++) { samples[i] = table[bytes[i]]; }
    var source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(gain);
    source.start(playAt);
    playAt += buffer.duration;
  }

  function stop(said) {
    if (socket) { socket.onclose = null; socket.close(); socket = null; }
    if (context) { context.close(); context = null; }
    button.textContent = "Listen";
    state.textContent = said;
  }

  function start() {
    context = new (window.AudioContext || window.webkitAudioContext)();
    gain = context.createGain();
    gain.gain.value = slider.value / 100;
    gain.connect(context.destination);
    playAt = 0;
    socket = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://")
      + location.host + "/listen?radio=" + player.getAttribute("data-radio"));
    socket.binaryType = "arraybuffer";
    socket.onmessage = function (message) {
      if (typeof message.data === "string") {
        state.textContent = message.data === "online" ? "listening" :
          "the radio is not on line; waiting for it";
      } else {
        play(new Uint8Array(message.data));
      }
    };
    socket.onclose = function () { stop("the connection was lost"); };
    button.textContent = "Stop";
    state.textContent = "connecting";
  }

  button.addEventListener("click", function () {
    if (socket) { stop("stopped"); } else { start(); }
  });
  slider.addEventListener("input", function () {
    if (gain) { gain.gain.value = slider.value / 100; }
  });
})();
"""

# the map's script: the radios on line are asked for, and each is a dot at
# the middle of its square with the callsign beside it
MAP_SCRIPT = """
"use strict";
(function () {
  var map = L.map("map", { worldCopyJump: true }).setView([20, 20], 2);
  L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 9,
    attribution: "&copy; OpenStreetMap contributors"
  }).addTo(map);
  var shown = L.layerGroup().addTo(map);
  var count = document.getElementById("count");
  function show(radios) {
    shown.clearLayers();
    for (var i = 0; i < radios.length; i++) {
      var radio = radios[i];
      L.rectangle([[radio.south, radio.west], [radio.north, radio.east]],
        { color: "#c33", weight: 1, fillOpacity: 0.08, interactive: false }).addTo(shown);
      L.circleMarker([radio.lat, radio.lon],
        { radius: 6, color: "#900", fillColor: "#e33", fillOpacity: 0.9, weight: 1 })
        .bindTooltip(radio.callsign + " " + radio.grid,
          { permanent: true, direction: "right", offset: [8, 0] })
        .addTo(shown);
    }
    count.textContent = radios.length === 1 ? "1 radio on line" :
      radios.length + " radios on line";
  }
  function ask() {
    var request = new XMLHttpRequest();
    request.onload = function () {
      if (request.status === 200) { show(JSON.parse(request.responseText)); }
    };
    request.open("GET", "/map.json");
    request.send();
  }
  ask();
  setInterval(ask, 30000);
})();
"""

MAP_STYLE = """
html, body { height: 100%; margin: 0; font-family: sans-serif; }
#top { height: 2.4em; line-height: 2.4em; padding: 0 0.8em; background: #101820; color: #fff; }
#top a { color: #7cf; text-decoration: none; margin-right: 1.2em; }
#map { position: absolute; top: 2.4em; bottom: 0; left: 0; right: 0; }
"""

MAP_PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Radios on line - my.sbitx.net</title>
<link rel="stylesheet" href="/static/leaflet.css">
<link rel="stylesheet" href="/map.css">
</head><body>
<div id="top"><a href="/">my.sbitx.net</a><span id="count"></span></div>
<div id="map"></div>
<script src="/static/leaflet.js"></script>
<script src="/map.js"></script>
</body></html>
"""

# what is served from static/, by its name, and as what
STATIC = {
    "leaflet.js": "text/javascript; charset=utf-8",
    "leaflet.css": "text/css; charset=utf-8",
}
PAGE_POLICY = ("default-src 'none'; style-src 'self'; script-src 'self'; "
               "form-action 'self'; frame-ancestors 'none'; base-uri 'none'")
# the map's page may also fetch OpenStreetMap's pictures, ask this site for
# the radios, and be styled by Leaflet as it moves things about
MAP_POLICY = ("default-src 'none'; style-src 'self' 'unsafe-inline'; script-src 'self'; "
              "img-src 'self' data: https://tile.openstreetmap.org; connect-src 'self'; "
              "frame-ancestors 'none'; base-uri 'none'")

STYLE = """
body { font-family: sans-serif; max-width: 34em; margin: 6vh auto; padding: 0 1em;
    color: #222; background: #fff; line-height: 1.45; }
h1 { font-size: 1.5em; }
h1 a { color: inherit; text-decoration: none; }
label { display: block; margin-top: 1em; }
input[type=text], input[type=email], input[type=password] {
    width: 100%; box-sizing: border-box; padding: 0.5em; font-size: 1em; }
button { margin-top: 1.2em; padding: 0.5em 1.2em; font-size: 1em; }
table { border-collapse: collapse; width: 100%; }
td, th { text-align: left; padding: 0.4em 0.6em 0.4em 0; border-bottom: 1px solid #ddd; }
td form { margin: 0; display: inline; }
td button { margin: 0; padding: 0.2em 0.8em; }
.said { background: #eef6ee; border-left: 4px solid #3a3; padding: 0.6em 0.8em; }
.wrong { background: #fbeeee; border-left: 4px solid #c33; padding: 0.6em 0.8em; }
.code { font-family: monospace; letter-spacing: 0.1em; text-transform: uppercase; }
.foot { margin-top: 3em; color: #777; font-size: 0.9em; }
"""


class Settings:
    def __init__(self, path=None):
        ini = configparser.ConfigParser()
        if path:
            with open(path) as file:
                ini.read_file(file)
        server = ini["server"] if ini.has_section("server") else {}
        mail = ini["mail"] if ini.has_section("mail") else {}
        remote = ini["remote"] if ini.has_section("remote") else {}
        self.remote_host, _, remote_port = remote.get("listen", "0.0.0.0:8081").rpartition(":")
        self.remote_port = int(remote_port)
        self.listen_host, _, listen_port = remote.get(
            "listen_web", "127.0.0.1:8102").rpartition(":")
        self.listen_port = int(listen_port)
        self.host, _, port = server.get("listen", "127.0.0.1:8100").rpartition(":")
        self.port = int(port)
        self.base_url = server.get("base_url", "http://127.0.0.1:8100").rstrip("/")
        self.database = server.get("database", "accounts.db")
        self.registration = server.get("registration", "invite")
        self.pow_bits = int(server.get("pow_bits", str(POW_BITS)))
        self.invite_code = server.get("invite_code", "")
        self.support = server.get("support", "")
        self.smtp_host = mail.get("host", "")
        self.smtp_port = int(mail.get("port", "587"))
        self.smtp_user = mail.get("user", "")
        self.smtp_password_file = mail.get("password_file", "")
        self.sender = mail.get("sender", "accounts@localhost")
        self.outbox = mail.get("outbox", "outbox.log")


def now():
    return int(time.time())


def digest(secret):
    return hashlib.sha256(secret.encode()).hexdigest()


hashing = threading.BoundedSemaphore(HASHES_AT_ONCE)


def hash_password(password, salt=None):
    """scrypt's hash of a password, with its salt, as text to keep"""
    if salt is None:
        salt = secrets.token_bytes(16)
    with hashing:
        hashed = hashlib.scrypt(password.encode(), salt=salt, n=SCRYPT_N, r=SCRYPT_R,
                                p=SCRYPT_P, maxmem=SCRYPT_MEMORY, dklen=32)
    return "scrypt$%s$%s" % (salt.hex(), hashed.hex())


def is_password(password, kept):
    if not kept or not kept.startswith("scrypt$"):
        return False
    salt = bytes.fromhex(kept.split("$")[1])
    return hmac.compare_digest(hash_password(password, salt), kept)


def zero_bits(data):
    """how many zero bits a hash begins with"""
    bits = 0
    for byte in data:
        if byte == 0:
            bits += 8
            continue
        return bits + 8 - byte.bit_length()
    return bits


def new_secret():
    return "".join(secrets.choice(SECRET_LETTERS) for _ in range(SECRET_LENGTH))


def tidy_secret(typed):
    """a cookie or a client password as it is kept, twelve small letters, or
    '' if what was typed is not one; capitals, strokes and spaces are let by"""
    letters = re.sub(r"[^a-z]", "", typed.lower())
    if len(letters) != SECRET_LENGTH:
        return ""
    return letters


def show_secret(letters):
    return "-".join(letters[at:at + SECRET_GROUP]
                    for at in range(0, len(letters), SECRET_GROUP))


stretched = {}


def stretch(secret, salt):
    """a secret made slow to guess, as zbitx_remote.c and the clients make
    it: PBKDF2 with HMAC-SHA256, in hex. What has been stretched is
    remembered, since a radio that is not yet registered asks again and
    again"""
    known = stretched.get((secret, salt))
    if known is None:
        if len(stretched) > 1000:
            stretched.clear()
        known = hashlib.pbkdf2_hmac("sha256", secret.encode(),
                                    ("zbitx-1 " + salt.lower()).encode(),
                                    STRETCH_ROUNDS).hex()
        stretched[(secret, salt)] = known
    return known


def square_of(grid):
    """the four-character square of a grid locator: its name, its middle and
    its edges, in degrees; None if it is not a locator"""
    grid = grid.upper()
    if not GRID_FORM.match(grid):
        return None
    west = (ord(grid[0]) - ord("A")) * 20 - 180 + int(grid[2]) * 2
    south = (ord(grid[1]) - ord("A")) * 10 - 90 + int(grid[3])
    return {"grid": grid[:4], "lat": south + 0.5, "lon": west + 1.0,
            "south": south, "north": south + 1, "west": west, "east": west + 2}


def radio_id(callsign, suffix):
    return "%s-%s" % (callsign.lower(), suffix)


def tidy_callsign(typed):
    """a callsign as it is kept, or '' if it is not one: letters and figures,
    at least one of each, with no stroke and no suffix"""
    callsign = typed.strip().upper()
    if not CALLSIGN_FORM.match(callsign):
        return ""
    if not re.search(r"[A-Z]", callsign) or not re.search(r"[0-9]", callsign):
        return ""
    return callsign


class Store:
    """the SQLite file. Each request opens it for itself and closes it"""

    def __init__(self, path):
        self.path = path
        with self.open() as db:
            # radios were first given a key by this site, through a code;
            # now a radio makes its own cookie. The few of the first kind
            # are let go, to be added again with their cookies
            columns = [row["name"] for row in db.execute("pragma table_info(radios)")]
            if columns and "cookie" not in columns:
                db.execute("drop table if exists snapshots")
                db.execute("drop table radios")
                db.execute("drop table if exists links")
            db.executescript(SCHEMA)
            # the grid came after the first radios were added
            columns = [row["name"] for row in db.execute("pragma table_info(radios)")]
            if "grid" not in columns:
                db.execute("alter table radios add column grid text not null default ''")

    @contextlib.contextmanager
    def open(self):
        db = sqlite3.connect(self.path, timeout=10)
        db.row_factory = sqlite3.Row
        db.execute("pragma foreign_keys = on")
        db.execute("pragma journal_mode = wal")
        try:
            with db:
                yield db
        finally:
            db.close()


def tried_too_often(db, bucket, most):
    since = now() - WINDOW_S
    db.execute("delete from attempts where at < ?", (since,))
    count = db.execute("select count(*) from attempts where bucket = ? and at >= ?",
                       (bucket, since)).fetchone()[0]
    return count >= most


def note_attempt(db, bucket):
    db.execute("insert into attempts (bucket, at) values (?, ?)", (bucket, now()))


class Mailer:
    def __init__(self, settings):
        self.settings = settings

    def send(self, to, subject, text):
        s = self.settings
        if not s.smtp_host:
            with open(s.outbox, "a") as file:
                file.write("To: %s\nSubject: %s\n\n%s\n%s\n" % (to, subject, text, "-" * 60))
            return
        message = EmailMessage()
        message["From"] = s.sender
        message["To"] = to
        message["Subject"] = subject
        message.set_content(text)
        site = urllib.parse.urlsplit(s.base_url).hostname
        with smtplib.SMTP(s.smtp_host, s.smtp_port, local_hostname=site, timeout=20) as smtp:
            smtp.starttls()
            smtp.ehlo(site)
            if s.smtp_user:
                with open(s.smtp_password_file) as file:
                    smtp.login(s.smtp_user, file.read().strip())
            smtp.send_message(message)


class Service:
    """what the service does, apart from HTTP: the handler below calls this,
    and so do the tests and customer service's commands"""

    def __init__(self, settings, mailer=None):
        self.settings = settings
        self.store = Store(settings.database)
        self.mailer = mailer or Mailer(settings)
        # the key the challenges are signed with is made afresh at each
        # start: a form left open across a restart has to be loaded again
        self.pow_key = secrets.token_bytes(32)

    # proof of work

    def pow_challenge(self):
        """a challenge and its signature, for a form"""
        challenge = "%d.%s" % (now() + POW_LIFE_S, secrets.token_hex(8))
        return challenge, self.pow_sign(challenge)

    def pow_sign(self, challenge):
        return hmac.new(self.pow_key, challenge.encode(), hashlib.sha256).hexdigest()

    def is_pow_done(self, challenge, signature, answer):
        """whether a form's work was done: on a challenge of ours that has not
        run out, with enough zero bits, and not handed in before"""
        if self.settings.pow_bits <= 0:
            return True
        if not hmac.compare_digest(signature, self.pow_sign(challenge)):
            return False
        expires = int(challenge.split(".")[0])
        if expires < now() or not answer.isdigit() or len(answer) > 12:
            return False
        hashed = hashlib.sha256(("%s:%s" % (challenge, answer)).encode()).digest()
        if zero_bits(hashed) < self.settings.pow_bits:
            return False
        with self.store.open() as db:
            db.execute("delete from spent where expires < ?", (now(),))
            try:
                db.execute("insert into spent (challenge, expires) values (?, ?)",
                           (challenge, expires))
            except sqlite3.IntegrityError:
                return False
        return True

    # links mailed out

    def mail_link(self, db, account, purpose):
        secret = secrets.token_urlsafe(32)
        life = VERIFY_LINK_S if purpose == "verify" else RESET_LINK_S
        db.execute("delete from tokens where account = ? and purpose = ?",
                   (account["id"], purpose))
        db.execute("insert into tokens (hash, account, purpose, expires) values (?, ?, ?, ?)",
                   (digest(secret), account["id"], purpose, now() + life))
        link = "%s/password?t=%s" % (self.settings.base_url, secret)
        if purpose == "verify":
            subject = "Confirm your address for %s" % account["callsign"]
            text = ("Someone, we hope you, registered the callsign %s at %s with this "
                    "address.\n\nTo confirm the address and choose a password, open:\n\n"
                    "%s\n\nThe link works once, for two days. If this was not you, do "
                    "nothing: without the link no account is made.\n"
                    % (account["callsign"], self.settings.base_url, link))
        else:
            subject = "A new password for %s" % account["callsign"]
            text = ("A new password was asked for the callsign %s at %s.\n\nTo choose "
                    "one, open:\n\n%s\n\nThe link works once, for two hours. If this was "
                    "not you, do nothing: the password stays as it is.\n"
                    % (account["callsign"], self.settings.base_url, link))
        self.mailer.send(account["email"], subject, text)

    def register(self, callsign_typed, email_typed, invite, address):
        """returns (ok, what to tell the user)"""
        s = self.settings
        if s.registration == "closed":
            return False, "Registration is not open yet."
        if s.registration == "invite" and not (
                s.invite_code and hmac.compare_digest(invite.strip(), s.invite_code)):
            return False, "That is not the invitation code."
        callsign = tidy_callsign(callsign_typed)
        if not callsign:
            return False, ("That does not look like a callsign. Give it plain, with no "
                           "stroke or suffix: VU2ESE, not VU2ESE/P.")
        email = email_typed.strip()
        if not EMAIL_FORM.match(email):
            return False, "That does not look like an email address."
        with self.store.open() as db:
            if tried_too_often(db, "mail " + address, MAILS_AN_ADDRESS):
                return False, "Too many tries from here. Try again in a quarter of an hour."
            note_attempt(db, "mail " + address)
            account = db.execute("select * from accounts where callsign = ?",
                                 (callsign,)).fetchone()
            if account is not None and account["status"] != "pending":
                return False, ("%s is already registered. If it is your callsign, "
                               "write to %s." % (callsign, s.support or "support"))
            if account is not None:
                waiting = db.execute(
                    "select 1 from tokens where account = ? and purpose = 'verify' "
                    "and expires > ?", (account["id"], now())).fetchone()
                if waiting and account["email"].lower() != email.lower():
                    return False, ("%s is being registered with another address. If it "
                                   "is your callsign, try again in two days or write to "
                                   "%s." % (callsign, s.support or "support"))
                db.execute("update accounts set email = ? where id = ?",
                           (email, account["id"]))
            else:
                db.execute("insert into accounts (callsign, email, created) values (?, ?, ?)",
                           (callsign, email, now()))
            account = db.execute("select * from accounts where callsign = ?",
                                 (callsign,)).fetchone()
            self.mail_link(db, account, "verify")
        return True, ("We have mailed a link to %s. Open it to confirm the address and "
                      "choose a password." % email)

    def token_account(self, db, secret):
        row = db.execute(
            "select tokens.purpose, accounts.* from tokens join accounts "
            "on accounts.id = tokens.account where tokens.hash = ? and tokens.expires > ?",
            (digest(secret), now())).fetchone()
        return row

    def set_password(self, secret, password, again):
        """returns (the session's secret or None, what to tell the user)"""
        if password != again:
            return None, "The two passwords are not the same."
        if len(password) < PASSWORD_SHORTEST:
            return None, "A password needs at least %d characters." % PASSWORD_SHORTEST
        if len(password) > PASSWORD_LONGEST:
            return None, "That password is too long."
        kept = hash_password(password)
        with self.store.open() as db:
            account = self.token_account(db, secret)
            if account is None:
                return None, "This link has been used or has run out. Ask for another."
            if account["status"] == "suspended":
                return None, "This account is suspended."
            db.execute("update accounts set password = ?, status = 'active' where id = ?",
                       (kept, account["id"]))
            db.execute("delete from tokens where account = ?", (account["id"],))
            db.execute("delete from sessions where account = ?", (account["id"],))
            return self.new_session(db, account["id"]), ""

    def new_session(self, db, account_id):
        secret = secrets.token_urlsafe(32)
        db.execute("delete from sessions where expires < ?", (now(),))
        db.execute("insert into sessions (hash, account, expires) values (?, ?, ?)",
                   (digest(secret), account_id, now() + SESSION_S))
        return secret

    def sign_in(self, callsign_typed, password, address):
        """returns (the session's secret or None, what to tell the user)"""
        callsign = callsign_typed.strip().upper()
        wrong = "The callsign or the password is wrong."
        with self.store.open() as db:
            if (tried_too_often(db, "signin " + callsign, SIGNIN_TRIES_A_CALLSIGN)
                    or tried_too_often(db, "signin-from " + address, SIGNIN_TRIES_AN_ADDRESS)):
                return None, "Too many wrong tries. Try again in a quarter of an hour."
            account = db.execute("select * from accounts where callsign = ?",
                                 (callsign,)).fetchone()
        kept = account["password"] if account is not None else None
        # a hash is worked out even for no account, so that the time taken
        # does not tell whether a callsign is registered
        is_right = is_password(password, kept or hash_password("nobody", b"0" * 16))
        with self.store.open() as db:
            if account is None or not kept or not is_right:
                note_attempt(db, "signin " + callsign)
                note_attempt(db, "signin-from " + address)
                return None, wrong
            if account["status"] == "suspended":
                return None, "This account is suspended. Write to %s." % (
                    self.settings.support or "support")
            return self.new_session(db, account["id"]), ""

    def session_account(self, secret):
        if not secret:
            return None
        with self.store.open() as db:
            return db.execute(
                "select accounts.* from sessions join accounts on accounts.id = "
                "sessions.account where sessions.hash = ? and sessions.expires > ? "
                "and accounts.status = 'active'", (digest(secret), now())).fetchone()

    def sign_out(self, secret):
        with self.store.open() as db:
            db.execute("delete from sessions where hash = ?", (digest(secret),))

    def forgot(self, callsign_typed, address):
        """a link is mailed if there is such an account; the user is told the
        same either way"""
        callsign = callsign_typed.strip().upper()
        with self.store.open() as db:
            if tried_too_often(db, "mail " + address, MAILS_AN_ADDRESS):
                return
            note_attempt(db, "mail " + address)
            account = db.execute(
                "select * from accounts where callsign = ? and status = 'active'",
                (callsign,)).fetchone()
            if account is not None:
                self.mail_link(db, account, "reset")

    # radios

    def radios(self, account_id):
        with self.store.open() as db:
            return db.execute("select * from radios where account = ? order by suffix",
                              (account_id,)).fetchall()

    def radio_add(self, account, suffix_typed, cookie_typed):
        """the owner adds a radio: a name for it and the cookie it shows.
        Returns (ok, what to tell the owner)"""
        suffix = suffix_typed.strip().lower()
        if not SUFFIX_FORM.match(suffix):
            return False, ("A radio's name is up to ten letters and figures, with nothing "
                           "else: home, zbitx, shack2.")
        cookie = tidy_secret(cookie_typed)
        if not cookie:
            return False, ("That is not a cookie. It is the twelve letters the radio "
                           "shows in its settings, like abcd-jklm-wxyz.")
        with self.store.open() as db:
            had = db.execute("select suffix from radios where account = ?",
                             (account["id"],)).fetchall()
            if len(had) >= RADIOS_AN_ACCOUNT:
                return False, "An account has room for %d radios. Remove one first." % (
                    RADIOS_AN_ACCOUNT)
            if suffix in [row["suffix"] for row in had]:
                return False, "You have a radio called %s already." % suffix
            db.execute("insert into radios (account, suffix, cookie, client_password, created) "
                       "values (?, ?, ?, ?, ?)",
                       (account["id"], suffix, cookie, new_secret(), now()))
        return True, ("%s is added. It will come on line within a quarter of a minute, "
                      "if it is switched on and its remote access is on."
                      % radio_id(account["callsign"], suffix))

    def radio_new_password(self, account, radio):
        with self.store.open() as db:
            db.execute("update radios set client_password = ? where id = ? and account = ?",
                       (new_secret(), radio, account["id"]))

    def radio_remove(self, account, radio):
        with self.store.open() as db:
            db.execute("delete from radios where id = ? and account = ?",
                       (radio, account["id"]))

    def radios_of(self, callsign):
        """for remote.py: the radios of a callsign whose account is active"""
        with self.store.open() as db:
            return db.execute(
                "select radios.*, accounts.callsign from radios join accounts "
                "on accounts.id = radios.account where accounts.callsign = ? "
                "and accounts.status = 'active'", (callsign.upper(),)).fetchall()

    def radio_called(self, name):
        """for remote.py: the radio of a name like vu2ese-home, or None"""
        callsign, _, suffix = name.lower().partition("-")
        for radio in self.radios_of(callsign):
            if radio["suffix"] == suffix:
                return radio
        return None

    def on_the_map(self):
        """the radios on line that have a grid, for anyone to see: each
        owner's callsign and four-character square, once, and no more"""
        seen = {}
        with self.store.open() as db:
            for row in db.execute(
                    "select accounts.callsign, radios.grid from radios join accounts "
                    "on accounts.id = radios.account where radios.online = 1 "
                    "and accounts.status = 'active' order by accounts.callsign"):
                square = square_of(row["grid"])
                if square is not None:
                    square["callsign"] = row["callsign"]
                    seen[(row["callsign"], square["grid"])] = square
        return list(seen.values())

    def snapshot(self, account, radio):
        """the newest copy of one of the account's radios' settings and logbook"""
        with self.store.open() as db:
            return db.execute(
                "select snapshots.*, radios.suffix from snapshots join radios "
                "on radios.id = snapshots.radio where radios.id = ? and radios.account = ?",
                (radio, account["id"])).fetchone()


def page(title, body, account=None):
    signed = ""
    if account is not None:
        signed = "<p>Signed in as <strong>%s</strong>.</p>" % html.escape(account["callsign"])
    return ("<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<title>%s - my.sbitx.net</title><link rel=\"stylesheet\" href=\"/style.css\">"
            "</head><body><h1><a href=\"/\">my.sbitx.net</a></h1>%s%s"
            "<p class=\"foot\">Accounts for sBitx and zBitx owners.</p></body></html>\n"
            % (html.escape(title), signed, body))


def said(text, is_wrong=False):
    if not text:
        return ""
    return "<p class=\"%s\">%s</p>" % ("wrong" if is_wrong else "said", html.escape(text))


def when(at):
    if not at:
        return "never"
    return time.strftime("%Y-%m-%d", time.gmtime(at))


class Handler(BaseHTTPRequestHandler):
    service = None
    server_version = "accounts"
    sys_version = ""
    protocol_version = "HTTP/1.1"
    timeout = 20

    def log_message(self, form, *args):
        sys.stderr.write("%s %s\n" % (self.address(), form % args))

    # what a request carries

    def address(self):
        return self.headers.get("X-Real-IP") or self.client_address[0]

    def cookie_name(self):
        if self.service.settings.base_url.startswith("https://"):
            return "__Host-sid"
        return "sid"

    def session_secret(self):
        for part in self.headers.get("Cookie", "").split(";"):
            name, _, value = part.strip().partition("=")
            if name == self.cookie_name():
                return value
        return ""

    def form_guard(self, secret):
        """the value a form posted while signed in must carry"""
        return hmac.new(secret.encode(), b"form", hashlib.sha256).hexdigest()

    def read_body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length > BODY_LONGEST:
            return None
        return self.rfile.read(length).decode("utf-8", "replace")

    def is_from_this_site(self):
        origin = self.headers.get("Origin")
        if origin is None:
            referer = self.headers.get("Referer", "")
            return referer.startswith(self.service.settings.base_url + "/")
        return origin == self.service.settings.base_url

    # what is sent back

    def send(self, status, content, kind="text/html; charset=utf-8", headers=(),
             policy=PAGE_POLICY, keep="no-store"):
        data = content
        if isinstance(content, str):
            data = content.encode()
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", keep)
        self.send_header("Content-Security-Policy", policy)
        self.send_header("X-Content-Type-Options", "nosniff")
        # OpenStreetMap gives its pictures only to a page that says where
        # it is, so the map's page tells other sites its site's name, and
        # no more than that; the other pages tell them nothing
        if policy == MAP_POLICY:
            self.send_header("Referrer-Policy", "strict-origin-when-cross-origin")
        else:
            self.send_header("Referrer-Policy", "same-origin")
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def redirect(self, where, headers=()):
        self.send(303, "", headers=(("Location", where),) + tuple(headers))

    def session_cookie(self, secret, life=SESSION_S):
        secure = "; Secure" if self.cookie_name().startswith("__Host-") else ""
        return ("Set-Cookie", "%s=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Lax%s"
                % (self.cookie_name(), secret, life, secure))

    # the pages

    def home(self, account, told="", is_wrong=False):
        if account is None:
            self.send(200, page("Welcome", said(told, is_wrong)
                                + "<p><a href=\"/signin\">Sign in</a> with your callsign, or "
                                "<a href=\"/register\">register</a> one.</p>"
                                "<p><a href=\"/map\">The radios on line now</a>, on a "
                                "map.</p>"))
            return
        guard = self.form_guard(self.session_secret())
        rows = ""
        for radio in self.service.radios(account["id"]):
            state = "on line" if radio["online"] else "off line"
            state += " &middot; <a href=\"/radios/listen?radio=%d\">listen</a>" % radio["id"]
            kept = ""
            if self.service.snapshot(account, radio["id"]) is not None:
                kept = ("<a href=\"/radios/settings?radio=%d\">settings</a> &middot; "
                        "<a href=\"/radios/log?radio=%d\">logbook</a>"
                        % (radio["id"], radio["id"]))
            buttons = ""
            for action, label in (("password", "New password"), ("remove", "Remove")):
                buttons += ("<form method=\"post\" action=\"/radios/%s\">"
                            "<input type=\"hidden\" name=\"guard\" value=\"%s\">"
                            "<input type=\"hidden\" name=\"radio\" value=\"%d\">"
                            "<button>%s</button></form> " % (action, guard, radio["id"], label))
            rows += ("<tr><td><strong>%s</strong></td><td>%s</td><td>%s</td>"
                     "<td class=\"code\">%s</td><td>%s</td><td>%s</td></tr>"
                     % (html.escape(radio_id(account["callsign"], radio["suffix"])), state,
                        html.escape(radio["grid"]), show_secret(radio["client_password"]),
                        kept, buttons))
        if rows:
            radios = ("<table><tr><th>Radio</th><th></th><th>Grid</th><th>Client password</th>"
                      "<th>Kept here</th><th></th></tr>%s</table>"
                      "<p>To reach a radio from another computer, give the client its "
                      "name and its client password.</p>" % rows)
        else:
            radios = "<p>You have added no radio yet.</p>"
        body = (said(told, is_wrong)
                + "<p>Address on record: %s &middot; <a href=\"/map\">the radios on line "
                "now, on a map</a></p><h2>Your radios</h2>%s"
                "<h2>Add a radio</h2>"
                "<p>On the radio, turn REMOTE on in its settings. It shows a cookie of "
                "twelve letters. Give the radio a name and type the cookie here.</p>"
                "<form method=\"post\" action=\"/radios/add\">"
                "<input type=\"hidden\" name=\"guard\" value=\"%s\">"
                "<label>A name for it, such as home <input type=\"text\" name=\"suffix\" "
                "maxlength=\"10\" autocomplete=\"off\" required></label>"
                "<label>The radio's cookie <input type=\"text\" name=\"cookie\" "
                "class=\"code\" maxlength=\"20\" autocomplete=\"off\" required></label>"
                "<button>Add the radio</button></form>"
                "<form method=\"post\" action=\"/signout\">"
                "<input type=\"hidden\" name=\"guard\" value=\"%s\">"
                "<button>Sign out</button></form>"
                % (html.escape(account["email"]), radios, guard, guard))
        self.send(200, page(account["callsign"], body, account))

    def register_form(self, told="", is_wrong=False, callsign="", email=""):
        s = self.service.settings
        if s.registration == "closed":
            self.send(200, page("Register", "<p>Registration is not open yet.</p>"))
            return
        invite = ""
        if s.registration == "invite":
            invite = ("<label>Invitation code <input type=\"text\" name=\"invite\" "
                      "autocomplete=\"off\" required></label>")
        challenge, signature = self.service.pow_challenge()
        self.send(200, page("Register", said(told, is_wrong)
                            + "<form method=\"post\" action=\"/register\" "
                            "data-pow-bits=\"%d\">"
                            "<input type=\"hidden\" name=\"pow_challenge\" value=\"%s\">"
                            "<input type=\"hidden\" name=\"pow_signature\" value=\"%s\">"
                            "<input type=\"hidden\" name=\"pow_answer\" value=\"\">"
                            % (s.pow_bits, challenge, signature) +
                            "<noscript><p class=\"wrong\">This form needs scripts turned on: "
                            "the browser does a little work before it is sent, to keep "
                            "spammers out.</p></noscript>"
                            "<label>Callsign <input type=\"text\" name=\"callsign\" value=\"%s\" "
                            "maxlength=\"10\" autocapitalize=\"characters\" required></label>"
                            "<label>Email address <input type=\"email\" name=\"email\" "
                            "value=\"%s\" required></label>%s"
                            "<button>Register</button></form>"
                            "<p>We mail a link to the address; there you choose a password.</p>"
                            "<script src=\"/pow.js\"></script>"
                            % (html.escape(callsign), html.escape(email), invite)))

    def signin_form(self, told="", is_wrong=False, callsign=""):
        self.send(200, page("Sign in", said(told, is_wrong)
                            + "<form method=\"post\" action=\"/signin\">"
                            "<label>Callsign <input type=\"text\" name=\"callsign\" value=\"%s\" "
                            "maxlength=\"10\" autocapitalize=\"characters\" "
                            "autocomplete=\"username\" required></label>"
                            "<label>Password <input type=\"password\" name=\"password\" "
                            "autocomplete=\"current-password\" required></label>"
                            "<button>Sign in</button></form>"
                            "<p><a href=\"/forgot\">Forgot the password?</a> &middot; "
                            "<a href=\"/register\">Register</a></p>" % html.escape(callsign)))

    def forgot_form(self, told=""):
        self.send(200, page("Forgot the password", said(told)
                            + "<form method=\"post\" action=\"/forgot\">"
                            "<label>Callsign <input type=\"text\" name=\"callsign\" "
                            "maxlength=\"10\" autocapitalize=\"characters\" required></label>"
                            "<button>Mail me a link</button></form>"))

    def password_form(self, secret, told="", is_wrong=False):
        with self.service.store.open() as db:
            account = self.service.token_account(db, secret)
        if account is None:
            self.send(200, page("Choose a password", said(
                "This link has been used or has run out. Ask for another.", True)
                + "<p><a href=\"/forgot\">Forgot the password?</a> &middot; "
                "<a href=\"/register\">Register</a></p>"))
            return
        self.send(200, page("Choose a password", said(told, is_wrong)
                            + "<p>A password for <strong>%s</strong>, of at least %d "
                            "characters.</p><form method=\"post\" action=\"/password\">"
                            "<input type=\"hidden\" name=\"t\" value=\"%s\">"
                            "<input type=\"hidden\" name=\"username\" value=\"%s\" "
                            "autocomplete=\"username\">"
                            "<label>Password <input type=\"password\" name=\"password\" "
                            "autocomplete=\"new-password\" required></label>"
                            "<label>The same again <input type=\"password\" name=\"again\" "
                            "autocomplete=\"new-password\" required></label>"
                            "<button>Set the password</button></form>"
                            % (html.escape(account["callsign"]), PASSWORD_SHORTEST,
                               html.escape(secret), html.escape(account["callsign"]))))

    def listen_page(self, account, number):
        """the player for one of the owner's radios"""
        mine = [radio for radio in self.service.radios(account["id"])
                if str(radio["id"]) == number]
        if not mine:
            self.send(404, page("Not found", "<p>You have no such radio.</p>", account))
            return
        name = radio_id(account["callsign"], mine[0]["suffix"])
        site = urllib.parse.urlsplit(self.service.settings.base_url)
        sockets = ("wss://" if site.scheme == "https" else "ws://") + site.netloc
        self.send(200, page("Listening to " + name,
                            "<h2>%s</h2><div id=\"player\" data-radio=\"%d\">"
                            "<p><button id=\"listen\">Listen</button> "
                            "<span id=\"state\"></span></p>"
                            "<label>Volume, here <input type=\"range\" id=\"volume\" "
                            "min=\"0\" max=\"100\" value=\"60\"></label></div>"
                            "<p>The radio's own volume is not changed by this.</p>"
                            "<p><a href=\"/\">Back</a></p>"
                            "<script src=\"/listen.js\"></script>"
                            % (html.escape(name), mine[0]["id"]), account),
                  policy=PAGE_POLICY + "; connect-src 'self' " + sockets)

    def kept_page(self, account, number, what):
        """a radio's settings or its logbook, as they were last copied"""
        kept = None
        if number.isdigit():
            kept = self.service.snapshot(account, int(number))
        if kept is None:
            self.send(404, page("Not found", "<p>Nothing is kept for that radio.</p>", account))
            return
        rows = ""
        if what == "settings":
            title = "Settings of %s" % radio_id(account["callsign"], kept["suffix"])
            for line in kept["controls"].splitlines():
                name, _, value = line.partition(" ")
                rows += "<tr><td>%s</td><td>%s</td></tr>" % (html.escape(name),
                                                             html.escape(value))
            table = "<table><tr><th>Control</th><th>Value</th></tr>%s</table>" % rows
        else:
            title = "Logbook of %s" % radio_id(account["callsign"], kept["suffix"])
            for line in kept["log"].splitlines():
                fields = (line.split("|") + [""] * 8)[:8]
                rows += "<tr>%s</tr>" % "".join(
                    "<td>%s</td>" % html.escape(field) for field in fields[1:])
            table = ("<table><tr><th>Date</th><th>UTC</th><th>kHz</th><th>Mode</th>"
                     "<th>Station</th><th>Sent</th><th>Received</th></tr>%s</table>" % rows)
            if not rows:
                table = "<p>The logbook had no entries.</p>"
        self.send(200, page(title, "<h2>%s</h2><p>As copied from the radio on %s UTC.</p>%s"
                            "<p><a href=\"/\">Back</a></p>"
                            % (html.escape(title),
                               time.strftime("%Y-%m-%d %H:%M", time.gmtime(kept["taken"])),
                               table), account))

    # requests

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        account = self.service.session_account(self.session_secret())
        if url.path == "/style.css":
            self.send(200, STYLE, "text/css; charset=utf-8")
        elif url.path == "/pow.js":
            self.send(200, POW_SCRIPT, "text/javascript; charset=utf-8")
        elif url.path == "/map":
            self.send(200, MAP_PAGE, policy=MAP_POLICY)
        elif url.path == "/listen.js":
            self.send(200, LISTEN_SCRIPT, "text/javascript; charset=utf-8")
        elif url.path == "/radios/listen":
            if account is None:
                self.signin_form("Sign in first.")
            else:
                self.listen_page(account, query.get("radio", [""])[0])
        elif url.path == "/map.js":
            self.send(200, MAP_SCRIPT, "text/javascript; charset=utf-8")
        elif url.path == "/map.css":
            self.send(200, MAP_STYLE, "text/css; charset=utf-8")
        elif url.path == "/map.json":
            self.send(200, json.dumps(self.service.on_the_map()), "application/json")
        elif url.path.startswith("/static/") and url.path[len("/static/"):] in STATIC:
            name = url.path[len("/static/"):]
            with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "static", name), "rb") as file:
                self.send(200, file.read(), STATIC[name], keep="public, max-age=86400")
        elif url.path == "/":
            self.home(account)
        elif url.path == "/register":
            self.register_form()
        elif url.path == "/signin":
            self.signin_form()
        elif url.path == "/forgot":
            self.forgot_form()
        elif url.path == "/password":
            self.password_form(query.get("t", [""])[0])
        elif url.path in ("/radios/settings", "/radios/log"):
            if account is None:
                self.signin_form("Sign in first.")
            else:
                self.kept_page(account, query.get("radio", [""])[0],
                               url.path.rpartition("/")[2])
        else:
            self.send(404, page("Not found", "<p>There is no such page.</p>"))


    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        body = self.read_body()
        if body is None:
            self.send(413, "too long\n", "text/plain")
            return
        if not self.is_from_this_site():
            self.send(403, page("Refused", "<p>That form did not come from this site.</p>"))
            return
        form = {name: values[0] for name, values in urllib.parse.parse_qs(body).items()}
        secret = self.session_secret()
        account = self.service.session_account(secret)

        if url.path == "/register":
            if self.service.is_pow_done(form.get("pow_challenge", ""),
                                        form.get("pow_signature", ""),
                                        form.get("pow_answer", "")):
                ok, told = self.service.register(form.get("callsign", ""),
                                                 form.get("email", ""),
                                                 form.get("invite", ""), self.address())
            else:
                ok, told = False, ("The form sat too long or was sent before the browser "
                                   "had finished its work. Wait for the button and send "
                                   "it again.")
            if ok:
                self.send(200, page("Register", said(told)))
            else:
                self.register_form(told, True, form.get("callsign", ""), form.get("email", ""))
        elif url.path == "/signin":
            secret, told = self.service.sign_in(form.get("callsign", ""),
                                                form.get("password", ""), self.address())
            if secret is None:
                self.signin_form(told, True, form.get("callsign", ""))
            else:
                self.redirect("/", [self.session_cookie(secret)])
        elif url.path == "/forgot":
            self.service.forgot(form.get("callsign", ""), self.address())
            self.send(200, page("Forgot the password", said(
                "If that callsign has an account, a link has been mailed to its address.")))
        elif url.path == "/password":
            secret, told = self.service.set_password(form.get("t", ""), form.get("password", ""),
                                                     form.get("again", ""))
            if secret is None:
                self.password_form(form.get("t", ""), told, True)
            else:
                self.redirect("/", [self.session_cookie(secret)])
        elif account is None:
            self.signin_form("Sign in first.")
        elif not hmac.compare_digest(form.get("guard", ""), self.form_guard(secret)):
            self.send(403, page("Refused", "<p>That form did not come from this site.</p>"))
        elif url.path == "/signout":
            self.service.sign_out(secret)
            self.redirect("/", [self.session_cookie("", 0)])
        elif url.path == "/radios/add":
            ok, told = self.service.radio_add(account, form.get("suffix", ""),
                                              form.get("cookie", ""))
            self.home(account, told, not ok)
        elif url.path in ("/radios/password", "/radios/remove"):
            if form.get("radio", "").isdigit():
                if url.path == "/radios/remove":
                    self.service.radio_remove(account, int(form["radio"]))
                else:
                    self.service.radio_new_password(account, int(form["radio"]))
            self.redirect("/")
        else:
            self.send(404, page("Not found", "<p>There is no such page.</p>"))



def serve(settings):
    Handler.service = Service(settings)
    server = ThreadingHTTPServer((settings.host, settings.port), Handler)
    server.daemon_threads = True
    print("accounts: listening on %s:%d for %s, registration %s" % (
        settings.host, settings.port, settings.base_url, settings.registration), flush=True)
    server.serve_forever()


def admin(settings, args):
    service = Service(settings)
    with service.store.open() as db:
        if args.command == "list":
            for a in db.execute("select * from accounts order by callsign"):
                radios = db.execute("select count(*) from radios where account = ?",
                                    (a["id"],)).fetchone()[0]
                print("%-10s %-9s %s  %-32s radios %d  %s" % (
                    a["callsign"], a["status"], when(a["created"]), a["email"], radios,
                    a["note"]))
            return 0
        account = db.execute("select * from accounts where callsign = ?",
                             (args.callsign.upper(),)).fetchone()
        if account is None:
            print("no account for %s" % args.callsign.upper())
            return 1
        if args.command == "show":
            for name in ("callsign", "email", "status", "note"):
                print("%-9s %s" % (name, account[name]))
            print("%-9s %s" % ("created", when(account["created"])))
            for r in db.execute("select * from radios where account = ?", (account["id"],)):
                print("radio     %-22s added %s, last seen %s" % (
                    radio_id(account["callsign"], r["suffix"]), when(r["created"]),
                    when(r["last_seen"])))
        elif args.command == "suspend":
            db.execute("update accounts set status = 'suspended' where id = ?", (account["id"],))
            db.execute("delete from sessions where account = ?", (account["id"],))
        elif args.command == "restore":
            status = "active" if account["password"] else "pending"
            db.execute("update accounts set status = ? where id = ?", (status, account["id"]))
        elif args.command == "set-email":
            if not EMAIL_FORM.match(args.value):
                print("that does not look like an email address")
                return 1
            db.execute("update accounts set email = ? where id = ?", (args.value, account["id"]))
        elif args.command == "rename":
            new = tidy_callsign(args.value)
            if not new:
                print("that does not look like a callsign")
                return 1
            if db.execute("select 1 from accounts where callsign = ?", (new,)).fetchone():
                print("%s has an account already" % new)
                return 1
            db.execute("update accounts set callsign = ? where id = ?", (new, account["id"]))
        elif args.command == "delete":
            db.execute("delete from accounts where id = ?", (account["id"],))
        elif args.command == "note":
            db.execute("update accounts set note = ? where id = ?", (args.value, account["id"]))
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description="accounts of sbitx and zbitx owners")
    parser.add_argument("--settings", default=os.environ.get("ACCOUNTS_SETTINGS"))
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("serve")
    commands.add_parser("list")
    for name in ("show", "suspend", "restore", "delete"):
        commands.add_parser(name).add_argument("callsign")
    for name in ("set-email", "rename", "note"):
        one = commands.add_parser(name)
        one.add_argument("callsign")
        one.add_argument("value")
    args = parser.parse_args(argv)
    settings = Settings(args.settings)
    if args.command == "serve":
        serve(settings)
        return 0
    return admin(settings, args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
