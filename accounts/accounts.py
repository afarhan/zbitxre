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

A RADIO AND ITS COOKIE

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

A radio's cookie can be changed, the author's way: the owner makes a new
one on the radio, NEW COOKIE among its CONNECTIONS, and types it here
on the radio's Keys page. The radio keeps its name, so the owner's
programs reach it as before; the old cookie is
forgotten, and remote.py lets go of the radio if it is on line with it,
within twenty seconds. That is what to do when a radio, or a copy of its
card, has gone where it should not. A radio sold is removed by the one who
sold it and added by the one who bought it, after a new cookie.

A program on another computer, the window started as a remote one, signs
in to one of the owner's radios with the owner's own callsign and
password, as the website is signed in to. Each radio had a client password
of its own for that until 2026-10-16, made here and shown beside the
radio; the author found it, beside the cookie, confusing, and had it taken
out. A laptop that is lost is shut out by changing the account's password.

Neither the cookie nor the password is ever sent to remote.py, the part of
this service that is reached over plain TCP: each is the secret in the
answer to a challenge, stretched first. PROTOCOL.md says how. To check an
answer the service has to work out the same answer, so the cookie is kept
here as it is, and of the password, besides its scrypt hash for the
website, what it stretches to, client_key_of(). That is not the password,
but it is what a program signs in with, as a cookie is; the database is
readable by the service's user alone.

WHAT IS KEPT, AND HOW

    accounts   callsign, email, the password's hash and its client key,
               status, a note
    tokens     the links mailed out, to verify an address or reset a password
    sessions   who is signed in, in which browser
    radios     each radio's name and its cookie (and an empty column where
               its client password was); and
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

WORKING A RADIO FROM THE BROWSER

Beside each of the owner's radios is "operate": /operate, a page that
works the radio as the window on the radio's own screen does, for a phone
held on its side. The author asked for it. The page is nothing but a
script and a stylesheet, static/operate.js and static/operate.css, which
are served from here; operate.js says what it is and how it reaches the
radio. It is for the radio's owner, signed in, and remote.py asks the same
of the page's connection.

THE GLOBE, AND LISTENING TO ANY RADIO

/map is a page anyone may see and hear, signed in or not: the world as a
globe, after radio.garden's, with the radios that are on line now, each a
dot at the middle of its grid square with its owner's callsign. The globe
is turned by dragging it and brought nearer with the wheel, or on a
touch screen by two fingers drawn apart. Radios in one grid square are all
at its middle, so those that would cover each other are drawn one above
another, each with its callsign. A dot that is
touched is chosen, and Listen plays what that radio is hearing, with the
frequency it is tuned to and its mode shown above the button, as the
author asked, and kept up as the radio is tuned. The author
asked for listening to be open to everyone: what a radio transmits and
receives is public anyway.

The page asks /map.json for the radios every half minute. What is told of
a radio there is a number to ask for its sound by, the callsign, and the
first four characters of the grid set on it, a square some hundred
kilometres across: not the radio's name, and not the finer square, which
would come near to saying which house. A radio with no grid set is not on
the globe.

The author wanted the flat map as well as the globe, so the page has both:
a word at the top goes from the one to the other, and the radio chosen and
what is being listened to stay as they are. The flat map is the same
coastlines laid out with longitude across and latitude up, drawn three
times side by side so that it can be dragged round the world without
coming to an edge. The first flat map was made of OpenStreetMap's pictures,
fetched from OpenStreetMap; this one is drawn here like the globe.

Both are drawn by map.js, below, on a canvas, with nothing but the
browser. For the globe each point of the coastlines is put where an eye far
above the globe's middle would see it, and a point round the far side is
put on the edge, which keeps the shape of a land that goes over the edge. The
coastlines are Natural Earth's, at its coarsest scale, which are in the
public domain; static/land.json has them, a list of rings of longitude and
latitude. Nothing on the page comes from another site.

The sound comes over a WebSocket to /listen, which nginx hands to
remote.py: twenty milliseconds of mu-law to a message; remote.py says how
the sound reaches it. The script turns each message into samples and plays
them one after another through the browser's Web Audio, a tenth of a
second behind their arrival so that an uneven network does not break the
sound up, and lets go of sound that has fallen more than half a second
behind. How loud it is is the listening device's own affair, its volume
keys or its mixer, and nothing at the radio: the author's rule is that
whoever listens from afar sets their own volume. There was a slider for
it on the page, and he had it taken out with the box it was in, which on
a telephone covered the globe: the radio that is chosen is named in the
bar along the top, with a loudspeaker to turn its sound off and on and a
cross to let go of it, and a radio is listened to as soon as it is
touched. A browser has no UDP, so this last stretch is TCP, inside the
site's HTTPS.

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

import decodes

VERIFY_LINK_S = 48 * 3600
RESET_LINK_S = 2 * 3600
# a sign-in that is to be remembered on the device lasts a month; one that
# is not ends when the browser is closed, and in any case within a day
SESSION_S = 30 * 24 * 3600
SESSION_UNREMEMBERED_S = 24 * 3600
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
# a cookie is twelve letters, written in fours, of an
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

# the globe's script: the coastlines and the radios are fetched; the globe is
# drawn, turned and brought nearer; a radio is chosen and listened to
MAP_SCRIPT = """
"use strict";
(function () {
  var canvas = document.getElementById("globe");
  var pen = canvas.getContext("2d");
  var count = document.getElementById("count");
  var who = document.getElementById("who");
  var button = document.getElementById("listen");
  var leave = document.getElementById("leave");
  var state = document.getElementById("state");
  var tuned = document.getElementById("tuned");
  var land = [], radios = [], chosen = null;
  var view = document.getElementById("view");
  var lon0 = 20, lat0 = 20, zoom = 1, isPlaced = false;
  var wide = 0, high = 0, radius = 0;
  var isFlat = false, shift = 0, degree = 1;
  var RAD = Math.PI / 180;

  // where a place on the globe is on the canvas, and whether it is on the
  // side that is seen
  function place(lon, lat) {
    if (isFlat) {
      return { x: wide / 2 + (lon + shift - lon0) * degree,
        y: high / 2 - (lat - lat0) * degree, seen: true };
    }
    var dl = (lon - lon0) * RAD, p = lat * RAD, p0 = lat0 * RAD;
    var x = Math.cos(p) * Math.sin(dl);
    var y = Math.cos(p0) * Math.sin(p) - Math.sin(p0) * Math.cos(p) * Math.cos(dl);
    var front = Math.sin(p0) * Math.sin(p) + Math.cos(p0) * Math.cos(p) * Math.cos(dl);
    if (front < 0) {
      var far = Math.sqrt(x * x + y * y) || 1;
      x /= far; y /= far;
    }
    return { x: wide / 2 + radius * x, y: high / 2 - radius * y, seen: front >= 0 };
  }

  function line(points) {
    var seen = false, i, at;
    for (i = 0; i < points.length; i++) {
      points[i] = place(points[i][0], points[i][1]);
      seen = seen || points[i].seen;
    }
    if (!seen) { return; }
    for (i = 0; i < points.length; i++) {
      at = points[i];
      if (i === 0) { pen.moveTo(at.x, at.y); } else { pen.lineTo(at.x, at.y); }
    }
  }

  function draw() {
    var scale = window.devicePixelRatio || 1, i, j, at, ring;
    wide = canvas.clientWidth; high = canvas.clientHeight;
    if (canvas.width !== wide * scale || canvas.height !== high * scale) {
      canvas.width = wide * scale; canvas.height = high * scale;
    }
    pen.setTransform(scale, 0, 0, scale, 0, 0);
    radius = Math.min(wide, high) * 0.42 * zoom;
    degree = wide / 360 * zoom;
    pen.fillStyle = "#05080c";
    pen.fillRect(0, 0, wide, high);
    if (isFlat) { drawFlat(); drawRadios(); return; }
    var sea = pen.createRadialGradient(wide / 2 - radius * 0.3, high / 2 - radius * 0.3,
      radius * 0.1, wide / 2, high / 2, radius);
    sea.addColorStop(0, "#16354f"); sea.addColorStop(1, "#0a1a2a");
    pen.beginPath(); pen.arc(wide / 2, high / 2, radius, 0, 2 * Math.PI);
    pen.fillStyle = sea; pen.fill();

    pen.save();
    pen.beginPath(); pen.arc(wide / 2, high / 2, radius, 0, 2 * Math.PI); pen.clip();
    pen.beginPath();
    for (i = 0; i < land.length; i++) {
      ring = [];
      for (j = 0; j < land[i].length; j++) { ring.push(land[i][j]); }
      line(ring);
      pen.closePath();
    }
    pen.fillStyle = "#2c5a3a"; pen.fill();
    pen.strokeStyle = "#3f7a50"; pen.lineWidth = 0.6; pen.stroke();

    pen.strokeStyle = "rgba(255,255,255,0.07)"; pen.lineWidth = 0.5;
    for (i = -150; i <= 180; i += 30) {
      ring = []; for (j = -90; j <= 90; j += 5) { ring.push([i, j]); }
      pen.beginPath(); lineSeen(ring); pen.stroke();
    }
    for (i = -60; i <= 60; i += 30) {
      ring = []; for (j = -180; j <= 180; j += 5) { ring.push([j, i]); }
      pen.beginPath(); lineSeen(ring); pen.stroke();
    }
    pen.restore();
    drawRadios();
  }

  // the world laid flat, three times side by side, so that it has no edge
  // to come to when it is dragged along
  function drawFlat() {
    var i, j, ring, top = place(0, 90).y, foot = place(0, -90).y;
    pen.fillStyle = "#0e2438";
    pen.fillRect(0, top, wide, foot - top);
    for (shift = -360; shift <= 360; shift += 360) {
      pen.beginPath();
      for (i = 0; i < land.length; i++) {
        ring = [];
        for (j = 0; j < land[i].length; j++) { ring.push(land[i][j]); }
        line(ring);
        pen.closePath();
      }
      pen.fillStyle = "#2c5a3a"; pen.fill();
      pen.strokeStyle = "#3f7a50"; pen.lineWidth = 0.6; pen.stroke();
      pen.strokeStyle = "rgba(255,255,255,0.07)"; pen.lineWidth = 0.5;
      pen.beginPath();
      for (i = -180; i < 180; i += 30) { lineSeen([[i, -90], [i, 90]]); }
      for (i = -60; i <= 60; i += 30) { lineSeen([[-180, i], [180, i]]); }
      pen.stroke();
    }
    shift = 0;
  }

  // those listening, a small dot for each grid square any are in, larger
  // for more of them. No name is put to a dot: the square is all that is
  // known, and all that should be shown
  function drawListeners() {
    var i, j, heard, at;
    for (i = 0; i < radios.length; i++) {
      heard = radios[i].listeners || [];
      for (j = 0; j < heard.length; j++) {
        shift = isFlat ? Math.round((lon0 - heard[j].lon) / 360) * 360 : 0;
        at = place(heard[j].lon, heard[j].lat);
        shift = 0;
        if (!at.seen) { continue; }
        pen.beginPath(); pen.arc(at.x, at.y, 2.5 + Math.min(4, heard[j].count), 0, 2 * Math.PI);
        pen.fillStyle = "rgba(120,200,255,0.3)"; pen.fill();
        pen.beginPath(); pen.arc(at.x, at.y, 2.5, 0, 2 * Math.PI);
        pen.fillStyle = "#7cf"; pen.fill();
      }
    }
  }

  // radios in one grid square are all at its middle, and no zoom parts
  // them: the author found three on one spot and could choose none. Those
  // that would cover each other are stood one above another, ROW apart,
  // each with its callsign, so that each can be touched
  var STACKED = 16, ROW = 20;
  function stackRadios() {
    var i, j, k, at, spots = [], spot;
    for (i = 0; i < radios.length; i++) {
      // on the flat map a radio is drawn in the copy of the world nearest
      // the middle of the page
      shift = isFlat ? Math.round((lon0 - radios[i].lon) / 360) * 360 : 0;
      at = place(radios[i].lon, radios[i].lat);
      shift = 0;
      radios[i].at = at;
      if (!at.seen) { continue; }
      for (j = 0; j < spots.length; j++) {
        if (Math.hypot(spots[j].x - at.x, spots[j].y - at.y) < STACKED) { break; }
      }
      if (j === spots.length) { spots.push({ x: at.x, y: at.y, on: [] }); }
      spots[j].on.push(radios[i]);
    }
    for (j = 0; j < spots.length; j++) {
      spot = spots[j];
      spot.on.sort(function (a, b) { return a.callsign < b.callsign ? -1 : 1; });
      for (k = 0; k < spot.on.length; k++) {
        spot.on[k].at = { x: spot.x, y: spot.y + (k - (spot.on.length - 1) / 2) * ROW, seen: true };
      }
    }
  }

  function drawRadios() {
    var i, at;
    drawListeners();
    stackRadios();
    pen.font = "13px sans-serif"; pen.textBaseline = "middle";
    for (i = 0; i < radios.length; i++) {
      at = radios[i].at;
      if (!at.seen) { continue; }
      var isChosen = chosen && chosen.radio === radios[i].radio;
      // a radio whose owner has streaming switched off is there, grey, and
      // is not to be listened to
      var isOpen = radios[i].streaming !== false;
      pen.beginPath(); pen.arc(at.x, at.y, isChosen ? 11 : 8, 0, 2 * Math.PI);
      pen.fillStyle = !isOpen ? "rgba(150,160,170,0.2)" : isChosen ? "rgba(255,210,60,0.3)"
        : "rgba(80,255,140,0.25)";
      pen.fill();
      pen.beginPath(); pen.arc(at.x, at.y, isChosen ? 5 : 4, 0, 2 * Math.PI);
      pen.fillStyle = !isOpen ? "#8a949e" : isChosen ? "#ffd23c" : "#50ff8c"; pen.fill();
      pen.fillStyle = isOpen ? "#ffffff" : "#8a949e";
      // how many are listening to it is beside its callsign, when any are
      pen.fillText(radios[i].callsign + (radios[i].listening ? "  " + heads(radios[i].listening) : ""),
        at.x + 12, at.y);
    }
  }

  // a line of the grid, of which only the part on the near side is drawn
  function lineSeen(points) {
    var down = false, i, at;
    for (i = 0; i < points.length; i++) {
      at = place(points[i][0], points[i][1]);
      if (!at.seen) { down = false; continue; }
      if (down) { pen.lineTo(at.x, at.y); } else { pen.moveTo(at.x, at.y); }
      down = true;
    }
  }

  /* turning it, bringing it nearer, choosing a radio */

  // one finger turns it; two, drawn apart or together, bring it nearer or
  // send it back, as the wheel does
  var fingers = {}, held = null, moved = 0, spread = 0;
  function fingersApart() {
    var ids = Object.keys(fingers);
    if (ids.length < 2) { return 0; }
    return Math.hypot(fingers[ids[0]].x - fingers[ids[1]].x,
      fingers[ids[0]].y - fingers[ids[1]].y);
  }
  function setZoom(to) {
    zoom = Math.max(0.5, Math.min(8, to));
    draw();
  }
  // the radio nearest a point on the canvas, within a touch's reach
  function radioAt(e) {
    var box = canvas.getBoundingClientRect(), best = null, near = 18, i, d;
    for (i = 0; i < radios.length; i++) {
      if (!radios[i].at || !radios[i].at.seen) { continue; }
      d = Math.hypot(radios[i].at.x - (e.clientX - box.left),
        radios[i].at.y - (e.clientY - box.top));
      if (d < near) { near = d; best = radios[i]; }
    }
    return best;
  }
  // a mouse over a radio shows the hand that points, as a link does
  canvas.addEventListener("pointermove", function (e) {
    if (e.pointerType === "mouse" && e.buttons === 0) {
      canvas.style.cursor = radioAt(e) ? "pointer" : "grab";
    }
  });
  canvas.addEventListener("pointerdown", function (e) {
    if (Object.keys(fingers).length === 0) { moved = 0; }
    fingers[e.pointerId] = { x: e.clientX, y: e.clientY };
    held = { x: e.clientX, y: e.clientY };
    spread = fingersApart();
    if (spread) { moved = 100; }  // two fingers never choose a radio
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener("pointermove", function (e) {
    if (!fingers[e.pointerId]) { return; }
    fingers[e.pointerId] = { x: e.clientX, y: e.clientY };
    var apart = fingersApart();
    if (apart) {
      if (spread > 10) { setZoom(zoom * apart / spread); }
      spread = apart;
      return;
    }
    if (!held) { return; }
    var dx = e.clientX - held.x, dy = e.clientY - held.y;
    moved += Math.abs(dx) + Math.abs(dy);
    if (isFlat) {
      lon0 -= dx / degree; lat0 += dy / degree;
    } else {
      lon0 -= dx * 90 / radius; lat0 += dy * 90 / radius;
    }
    if (lon0 > 180) { lon0 -= 360; }
    if (lon0 < -180) { lon0 += 360; }
    if (lat0 > 85) { lat0 = 85; }
    if (lat0 < -85) { lat0 = -85; }
    held = { x: e.clientX, y: e.clientY };
    draw();
  });
  function lifted(e) {
    var ids;
    delete fingers[e.pointerId];
    ids = Object.keys(fingers);
    spread = 0;
    // the finger that stays goes on turning from where it is
    held = ids.length === 1 ? { x: fingers[ids[0]].x, y: fingers[ids[0]].y } : null;
    return ids.length === 0;
  }
  canvas.addEventListener("pointercancel", lifted);
  // a finger whose lifting was never told of would be counted for ever,
  // and every touch after it taken for the second of two
  canvas.addEventListener("lostpointercapture", lifted);
  canvas.addEventListener("pointerup", function (e) {
    var best;
    if (!lifted(e) || moved > 6) { return; }
    best = radioAt(e);
    // a radio that is touched is listened to at once, with no button to
    // press for it, which the author asked for
    if (best) {
      choose(best);
      if (best.streaming === false) {
        state.textContent = "not streaming";
      } else if (!isWanted) {
        start();
      }
    }
  });
  canvas.addEventListener("wheel", function (e) {
    e.preventDefault();
    setZoom(zoom * (e.deltaY < 0 ? 1.15 : 1 / 1.15));
  }, { passive: false });
  window.addEventListener("resize", draw);
  view.addEventListener("click", function () {
    isFlat = !isFlat;
    view.textContent = isFlat ? "globe" : "flat map";
    zoom = 1;
    if (isFlat) { lat0 = 15; }
    draw();
  });

  // "3 listening"
  function heads(listening) { return listening + " listening"; }

  function showWho() {
    who.textContent = chosen.callsign + "  " + chosen.grid
      + (chosen.listening ? "  \u00b7  " + heads(chosen.listening) : "");
  }

  /* what the chosen radio is copying in FT8, in a box at the side: the
     author asked that a radio on FT8 could be touched and what it is
     hearing read, as the page that works a radio shows it to its owner.
     The service keeps every message a radio copies; this asks for those
     of the last quarter of an hour and then, every few seconds, for what
     has come since. With none, there is no box: the radio is not on FT8. */

  var heard = document.getElementById("heard");
  var heardTitle = document.getElementById("heard-title");
  var heardList = document.getElementById("heard-list");
  var heardOf = 0, heardAfter = 0;

  function askForCopied() {
    if (!chosen) { return; }
    var asked = chosen.radio;
    if (asked !== heardOf) {
      heardOf = asked; heardAfter = 0;
      heardList.textContent = ""; heard.className = ""; heardTab.className = "";
    }
    get("/decodes.json?radio=" + asked + "&after=" + heardAfter, function (rows) {
      var i, row, line, isAtFoot;
      if (!chosen || chosen.radio !== asked || rows.length === 0) { return; }
      isAtFoot = heardList.scrollTop + heardList.clientHeight >= heardList.scrollHeight - 4;
      for (i = 0; i < rows.length; i++) {
        row = rows[i];
        if (row.id <= heardAfter) { continue; }
        heardAfter = row.id;
        line = document.createElement("div");
        // each line has the frequency the radio was on when it copied it:
        // the owner may move the radio while it is being watched
        line.textContent = ("  " + (row.hz / 1e6).toFixed(3)).slice(-7) + "  "
          + row.sent.slice(0, 2) + ":" + row.sent.slice(2, 4) + ":" + row.sent.slice(4)
          + ("    " + (row.strength === null ? "TX" : row.strength)).slice(-4)
          + ("     " + row.pitch).slice(-5) + "  " + row.text;
        line.className = row.strength === null ? "sent" : row.text.indexOf("CQ ") === 0 ? "cq" : "";
        heardList.appendChild(line);
      }
      while (heardList.childNodes.length > 300) { heardList.removeChild(heardList.firstChild); }
      heardTitle.textContent = "FT8 copied by " + chosen.callsign;
      // the list is a strip down the right that can be put away to a tab
      // at the edge and brought back; on a narrow screen, a telephone's,
      // it begins put away, since open it leaves little of the globe
      heard.className = isHeardOpen ? "shown" : "";
      heardTab.className = isHeardOpen ? "" : "shown";
      if (isAtFoot) { heardList.scrollTop = heardList.scrollHeight; }
    });
  }
  setInterval(askForCopied, 5000);

  var heardTab = document.getElementById("heard-tab");
  var isHeardOpen = window.innerWidth >= 800;
  function openHeard(on) {
    isHeardOpen = on;
    heard.className = on ? "shown" : "";
    heardTab.className = on ? "" : "shown";
    if (on) { heardList.scrollTop = heardList.scrollHeight; }
  }
  heardTab.addEventListener("click", function () { openHeard(true); });
  document.getElementById("heard-close").addEventListener("click", function () {
    openHeard(false);
  });

  function choose(radio) {
    if (chosen && chosen.radio !== radio.radio) { stop(""); }
    chosen = radio;
    askForCopied();
    showWho();
    document.body.className = "chosen";
    draw();
  }

  // the radio is let go of: nothing is chosen, and the globe has the page
  leave.addEventListener("click", function () {
    stop("");
    chosen = null;
    heardOf = 0;
    heard.className = ""; heardTab.className = "";
    document.body.className = "";
    draw();
  });

  /* listening */

  var player = null, socket = null;

  // Listening goes on until Stop: if the connection is lost, as it is when
  // the service is started again or a telephone changes its network, the
  // page waits a little and listens again by itself. It used to stop and
  // wait to be asked.
  var isWanted = false, isFull = false, again = null;

  function stop(said) {
    isWanted = false;
    if (again) { clearTimeout(again); again = null; }
    if (socket) { socket.onclose = null; socket.close(); socket = null; }
    if (player) { player.close(); player = null; }
    button.textContent = "🔇";
    state.textContent = said;
    tuned.textContent = "";
  }

  function start() {
    player = radioSound();
    isWanted = true;
    button.textContent = "🔊";
    state.textContent = "connecting";
    listen();
  }

  function listen() {
    again = null;
    isFull = false;
    socket = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://")
      + location.host + "/listen?radio=" + chosen.radio);
    socket.binaryType = "arraybuffer";
    socket.onmessage = function (message) {
      if (typeof message.data === "string") {
        var words = message.data.split(" ");
        if (words[0] === "lost") { player.lost(Number(words[1])); return; }
        // the service says how many are listening whenever that changes
        if (words[0] === "listening" && chosen) {
          chosen.listening = Number(words[1]);
          showWho();
          draw();
          return;
        }
        if (words[0] === "tuned") {
          var hz = ("000000" + words[1]).slice(-Math.max(7, words[1].length));
          tuned.textContent = hz.slice(0, -6).replace(/^0+(?=\\d)/, "") + "."
            + hz.slice(-6, -3) + "." + hz.slice(-3) + " MHz   " + words[2];
          return;
        }
        // a word this script does not know is let pass: the service may
        // come to say more than this page, loaded before it did, knows of
        if (message.data === "online") {
          state.textContent = "";
        } else if (message.data === "offline") {
          state.textContent = "not on line; waiting";
          tuned.textContent = "";
        } else if (message.data === "closed") {
          // its owner has switched streaming off: there is nothing to wait for
          stop("not streaming");
        } else if (message.data === "full") {
          isFull = true;
          state.textContent = "full; waiting for room";
        }
      } else {
        player.play(new Uint8Array(message.data));
      }
    };
    socket.onclose = function () {
      socket = null;
      if (!isWanted) { return; }
      if (!isFull) { state.textContent = "connection lost; trying again"; }
      again = setTimeout(listen, isFull ? 30000 : 3000);
    };
  }

  // the loudspeaker in the top bar turns the sound off and on. There is no
  // volume here: the telephone's own, or the computer's, does that
  button.addEventListener("click", function () {
    if (isWanted) { stop(""); } else if (chosen && chosen.streaming !== false) { start(); }
  });

  /* what there is to show */

  function get(path, then) {
    var request = new XMLHttpRequest();
    request.onload = function () {
      if (request.status === 200) { then(JSON.parse(request.responseText)); }
    };
    request.open("GET", path);
    request.send();
  }

  function askForRadios() {
    get("/map.json", function (found) {
      var wanted = parseInt(location.hash.slice(1), 10), i;
      radios = found;
      count.textContent = radios.length === 1 ? "1 radio on line" :
        radios.length + " radios on line";
      // the radio that is chosen is the one in what has just come
      for (i = 0; chosen && i < radios.length; i++) {
        if (radios[i].radio === chosen.radio) { chosen = radios[i]; showWho(); }
      }
      if (!isPlaced && radios.length > 0) {
        lon0 = radios[0].lon; lat0 = radios[0].lat; isPlaced = true;
        for (i = 0; i < radios.length; i++) {
          if (radios[i].radio === wanted) {
            lon0 = radios[i].lon; lat0 = radios[i].lat; choose(radios[i]);
          }
        }
      }
      draw();
    });
  }

  get("/static/land.json", function (rings) { land = rings; draw(); });
  askForRadios();
  setInterval(askForRadios, 30000);
  draw();
})();
"""

# the bar along the top of every page, nav_bar()'s, alike on all of them
BAR_STYLE = """
.bar { display: flex; align-items: center; flex-wrap: wrap; gap: 0.3em 1.2em;
    padding: 0.6em 1em; background: #10181f; color: #ccd; font-family: sans-serif;
    font-size: 16px; line-height: 1.45; }
.bar a { color: #9cf; text-decoration: none; }
.bar a:hover { text-decoration: underline; }
.bar .site { color: #fff; font-weight: bold; margin-right: 0.6em; }
.bar .who { margin-left: auto; }
.bar .who strong { color: #fff; }
.bar form { display: inline; margin: 0 0 0 0.8em; }
.bar button { margin: 0; padding: 0; border: 0; background: none; color: #9cf;
    font-size: 1em; font-family: inherit; cursor: pointer; }
.bar button:hover { text-decoration: underline; }
"""

MAP_STYLE = BAR_STYLE + """
html, body { height: 100%; margin: 0; font-family: sans-serif; background: #05080c;
    color: #fff; overflow: hidden; }
.bar { position: absolute; top: 0; left: 0; right: 0; height: 2.6em; box-sizing: border-box;
    flex-wrap: nowrap; white-space: nowrap; overflow: hidden; padding: 0 1em; z-index: 4; }
@media (max-width: 480px) { .bar .site { display: none; } }
#top { position: absolute; top: 2.6em; left: 0; right: 0; height: 2.6em; padding: 0 0.6em;
    background: rgba(16,24,32,0.9); z-index: 3; display: flex; align-items: center;
    gap: 0.8em; white-space: nowrap; overflow: hidden; }
#top a { color: #7cf; text-decoration: none; }
#count { color: #9ab; }
#view { margin-left: auto; font-size: 0.9em; padding: 0.25em 0.8em; flex: none; }
#chosen { display: none; align-items: center; gap: 0.6em; min-width: 0; flex: 1; }
body.chosen #chosen { display: flex; }
#who { font-weight: bold; overflow: hidden; text-overflow: ellipsis; }
#tuned { font-family: monospace; color: #ffd23c; flex: none; }
#state { color: #9ab; overflow: hidden; text-overflow: ellipsis; }
#chosen button { font-size: 1.15em; line-height: 1; padding: 0.25em 0.5em; background: #1c2834;
    color: #fff; border: 1px solid #405060; border-radius: 4px; flex: none; }
@media (max-width: 760px) {
    body.chosen #count { display: none; }
    #hint { display: none; }
}
#globe { position: absolute; top: 0; left: 0; width: 100%; height: 100%; touch-action: none;
    cursor: grab; }
#heard { display: none; position: absolute; right: 0; top: 5.2em; bottom: 0;
    width: 28em; max-width: 88vw; background: rgba(16,24,32,0.93);
    border-left: 1px solid #405060; z-index: 2; flex-direction: column; overflow: hidden; }
#heard.shown { display: flex; }
#heard-head { display: flex; align-items: center; border-bottom: 1px solid #405060; }
#heard-title { flex: 1; padding: 0.5em 0.8em; color: #7cf; font-size: 0.9em; overflow: hidden;
    text-overflow: ellipsis; white-space: nowrap; }
#heard-close, #heard-tab { font-size: 1.1em; background: #1c2834; color: #7cf;
    border: 1px solid #405060; padding: 0.35em 0.8em; }
#heard-close { border-width: 0 0 0 1px; align-self: stretch; }
#heard-tab { display: none; position: absolute; right: 0; top: 5.8em; z-index: 2;
    border-right: 0; border-radius: 6px 0 0 6px; }
#heard-tab.shown { display: block; }
#heard-list { flex: 1; overflow-y: auto; padding: 0.3em 0.8em; font: 12px/1.5 monospace;
    white-space: pre; color: #dde; -webkit-overflow-scrolling: touch; }
#heard-list .cq { color: #50ff8c; }
#heard-list .sent { color: #ff6060; }
#hint { position: absolute; right: 1em; bottom: 1em; color: #678; font-size: 0.85em; z-index: 2;
    text-align: right; line-height: 1.5; }
#hint a { color: #789; }
"""

MAP_PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Radios on line - my.sbitx.net</title>
<link rel="stylesheet" href="/map.css">
</head><body>
<!--bar-->
<div id="top"><span id="count"></span>
<span id="chosen"><span id="who"></span><span id="tuned"></span><span id="state"></span>
<button id="listen" title="sound on or off">🔇</button>
<button id="leave" title="leave this radio">✕</button></span>
<button id="view">flat map</button></div>
<canvas id="globe"></canvas>
<button id="heard-tab">FT8 ‹</button>
<div id="heard"><div id="heard-head"><span id="heard-title"></span>
<button id="heard-close" title="put the list away">›</button></div>
<div id="heard-list"></div></div>
<div id="hint">drag to turn &middot; pinch or wheel to come nearer &middot; touch a radio to listen
<br>blue dots are listeners, placed to the nearest grid square by their network address
&middot; <a href="https://db-ip.com" rel="noopener">IP Geolocation by DB-IP</a></div>
<script src="/static/sound.js"></script>
<script src="/map.js"></script>
</body></html>
"""

# what is served from static/, by its name, and as what
# the files an owner fetches to upgrade a radio, by their address
RELEASED = {
    "/install": ("install.sh", "text/plain; charset=utf-8"),
    "/zbitx/front_panel.uf2": ("front_panel.uf2", "application/octet-stream"),
}

STATIC = {
    "land.json": "application/json",
    "operate.js": "text/javascript; charset=utf-8",
    "operate.css": "text/css; charset=utf-8",
    "sound.js": "text/javascript; charset=utf-8",
    "sound_worklet.js": "text/javascript; charset=utf-8",
    "upgrade.js": "text/javascript; charset=utf-8",
    "icon-192.png": "image/png",
    "icon-512.png": "image/png",
}



def static_address(name):
    """where a page finds a file of static/: its path, and after it a mark
    that changes when the file does.

    The mark is there because of a day lost on 2026-10-12. These files were
    first served to be kept for a day, and the author's laptop and phone
    kept the first operate.js: its player was mended three times that
    afternoon, and neither browser asked for it again, so he went on
    hearing the first one's buzz. Telling browsers afterwards not to keep
    the file does nothing for a copy they already hold. A page that names
    the file by what is in it is given the new one the moment there is
    one."""
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "static", name), "rb") as file:
        return "/static/%s?v=%s" % (name, hashlib.sha256(file.read()).hexdigest()[:12])


# The service worker that makes the page that works a radio an application
# a telephone or a computer offers to install, as the author asked on
# 2026-10-16: Chrome offers "Install app" only to a page with a manifest and
# a service worker. It keeps nothing: every request goes to the network as
# it would without it, so the page can never be held at an old operate.js,
# which cost a day on 2026-10-12 (static_address() says how). The radio
# cannot be worked without the internet anyway, so when a page cannot be
# fetched it says so in a few words instead of the browser's error.
SERVICE_WORKER = """
"use strict";
var NO_NETWORK = "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
  + "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  + "<title>No internet</title></head><body style='background:#101820;color:#dde;"
  + "font-family:sans-serif;padding:2em'><p>The radio is reached over the internet, "
  + "and this device has none just now.</p><p>Try again once it has.</p></body></html>";
self.addEventListener("install", function () { self.skipWaiting(); });
self.addEventListener("activate", function (e) { e.waitUntil(self.clients.claim()); });
self.addEventListener("fetch", function (e) {
  if (e.request.mode !== "navigate") { return; }
  e.respondWith(fetch(e.request).catch(function () {
    return new Response(NO_NETWORK, { headers: { "Content-Type": "text/html; charset=utf-8" } });
  }));
});
"""


def operate_manifest(radio):
    """what a telephone needs to put the page that works a radio on its
    home screen as an application: Chrome's menu, "Add to Home screen".

    Opened from there the page has the whole screen and is held on its
    side by the telephone itself, and stays so. In a browser's tab it has
    them only for as long as the browser cares to leave them: the author's
    telephone took the whole screen back whenever it showed a question of
    its own, the one about the microphone among them, and at a finger
    drawn in from the edge of the screen, which in a browser means "back";
    and a page cannot have them again without a touch. The manifest says
    nothing of whose radio it is, and anyone may read it; the page it
    opens asks who is signed in, as ever. The icon is the tuning wheel."""
    number = radio if radio.isdigit() else ""
    return {
        "name": "sBitx radio", "short_name": "sBitx",
        "id": "/operate?radio=" + number,
        "start_url": "/operate?radio=" + number, "scope": "/",
        "display": "fullscreen", "orientation": "landscape",
        "background_color": "#000000", "theme_color": "#101820",
        "icons": [{"src": "/static/icon-%d.png" % size, "sizes": "%dx%d" % (size, size),
                   "type": "image/png", "purpose": "any"} for size in (192, 512)],
    }


OPERATE_PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1,
 user-scalable=no, viewport-fit=cover">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="theme-color" content="#101820">
<link rel="manifest" href="/operate.webmanifest?radio=%d">
<link rel="apple-touch-icon" href="/static/icon-192.png">
<title>%s</title>
<link rel="stylesheet" href="%s">
</head><body data-radio="%d" data-name="%s" data-sockets="%s">
<script src="%s"></script>
<script src="%s"></script>
</body></html>
"""
PAGE_POLICY = ("default-src 'none'; style-src 'self'; script-src 'self'; "
               "form-action 'self'; frame-ancestors 'none'; base-uri 'none'")

STYLE = BAR_STYLE + """
body { font-family: sans-serif; margin: 0; color: #222; background: #fff; line-height: 1.45; }
main { max-width: 52em; margin: 4vh auto; padding: 0 1em; }
form, main > p, ol, ul { max-width: 34em; }
.heading { display: flex; align-items: center; justify-content: space-between; gap: 1em; }
a.action { background: #10181f; color: #fff; padding: 0.45em 1em; border-radius: 4px;
    text-decoration: none; white-space: nowrap; }
.cards { display: grid; grid-template-columns: repeat(auto-fill, minmax(15em, 1fr)); gap: 1em; }
.card { border: 1px solid #ccd; border-radius: 8px; padding: 0.9em 1em; }
.card .name { font-weight: bold; font-size: 1.1em; word-break: break-all; }
.card .state { color: #777; margin: 0.2em 0 0.7em; }
.card .state.on { color: #2a8a3a; }
.card .state::before { content: "\\25CF  "; }
.card .links { display: flex; flex-wrap: wrap; gap: 0.3em 1em; margin-top: 0.8em;
    font-size: 0.95em; }
.card .waiting { display: block; color: #777; font-size: 0.9em; }
.journey { padding-left: 1.4em; }
.journey li { margin-bottom: 1.1em; color: #777; }
.journey li strong { color: #222; }
.journey li.now { color: #222; }
.journey .action { display: inline-block; margin-top: 0.6em; }
.small { color: #777; font-size: 0.9em; margin-top: 2em; }
.secret { font-size: 1.3em; background: #f4f6f8; padding: 0.5em 0.8em; display: inline-block; }
.steps li { margin-bottom: 0.5em; }
.command { font-family: monospace; }
.tick { display: flex; align-items: center; gap: 0.5em; }
button.danger { background: #fbeeee; border: 1px solid #c33; color: #a22; }
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
/* keys are shown as the radio shows them, in small letters; the author found
   them in capitals here and small on the radio. Either is taken when typed */
.code { font-family: monospace; letter-spacing: 0.1em; }
/* the one line that upgrades a radio, on /upgrade: large, whole, and not
   broken across lines, since the author's tester typed only a part of it */
.oneline { display: flex; align-items: center; gap: 0.8em; max-width: 40em;
    background: #10181f; color: #fff; border-radius: 6px; padding: 0.8em 1em; }
.oneline code { font-size: 1.25em; white-space: nowrap; overflow-x: auto; flex: 1; }
.oneline button { margin: 0; }
.check { background: #fff8e6; border-left: 4px solid #e0a000; padding: 0.6em 0.8em;
    max-width: 38em; }
.spelled { font-family: monospace; }
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
        # the most packets a second the VoIP repeater sends, all channels
        # together; voip.py says why there is a ceiling
        self.voip_packets_a_second = int(remote.get("voip_packets_a_second", "2500"))
        self.host, _, port = server.get("listen", "127.0.0.1:8100").rpartition(":")
        self.port = int(port)
        self.base_url = server.get("base_url", "http://127.0.0.1:8100").rstrip("/")
        self.database = server.get("database", "accounts.db")
        # ip_places.py's file, for putting listeners on the globe; none, no dots
        self.ip_places = server.get("ip_places", "")
        # decodes.py's file, of what the radios copy in FT8
        self.decodes = server.get("decodes", "decodes.db")
        self.registration = server.get("registration", "invite")
        self.pow_bits = int(server.get("pow_bits", str(POW_BITS)))
        # where a page's WebSockets go: this site, unless they are said to
        # go elsewhere, as when the site is tried with no nginx before it
        site = urllib.parse.urlsplit(self.base_url)
        self.sockets = server.get("sockets", (
            "wss://" if site.scheme == "https" else "ws://") + site.netloc)
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
    """a cookie as it is kept, twelve small letters, or
    '' if what was typed is not one; capitals, strokes and spaces are let by"""
    letters = re.sub(r"[^a-z]", "", typed.lower())
    if len(letters) != SECRET_LENGTH:
        return ""
    return letters


def show_secret(letters):
    return "-".join(letters[at:at + SECRET_GROUP]
                    for at in range(0, len(letters), SECRET_GROUP))


stretched = {}


def client_key_of(password, callsign):
    """the account's password, stretched as stretch() stretches a cookie,
    with the callsign as salt: what a program on another computer, the zbitx
    window started as a remote one, proves it has when it signs in to one
    of the owner's radios. Until 2026-10-16 each radio had a client password
    of its own for that; the author found it and the cookie together
    confusing, and had it taken out: the owner's own callsign and password
    are what sign in, at the website and from any program. Only this is
    kept, not the password, and it is not remembered in stretch()'s store"""
    return hashlib.pbkdf2_hmac("sha256", password.encode(),
                               ("zbitx-1 " + callsign.lower()).encode(),
                               STRETCH_ROUNDS).hex()


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


def listeners_of(places):
    """the squares a radio's listeners are in, as kept, "MK97:2,FN31:1", for
    the globe: each square's name and middle and how many are in it"""
    found = []
    for part in places.split(","):
        grid, _, count = part.partition(":")
        square = square_of(grid)
        if square is not None and count.isdigit():
            found.append({"grid": square["grid"], "lat": square["lat"], "lon": square["lon"],
                          "count": int(count)})
    return found


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
            # and how many are listening, for the globe to say
            if "listeners" not in columns:
                db.execute("alter table radios add column listeners integer not null default 0")
            # and roughly where they are, "MK97:2,FN31:1"
            # whether its owner lets anyone listen; when not, the globe has it grey
            if "streaming" not in columns:
                db.execute("alter table radios add column streaming integer not null default 1")
            if "listener_places" not in columns:
                db.execute("alter table radios add column listener_places text not null "
                           "default ''")
            # the account's client key, client_key_of() its password: what a
            # program on another computer signs in to the owner's radios with
            columns = [row["name"] for row in db.execute("pragma table_info(accounts)")]
            if "client_key" not in columns:
                db.execute("alter table accounts add column client_key text not null "
                           "default ''")

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
            db.execute("update accounts set password = ?, client_key = ?, status = 'active' "
                       "where id = ?", (kept, client_key_of(password, account["callsign"]),
                                        account["id"]))
            db.execute("delete from tokens where account = ?", (account["id"],))
            db.execute("delete from sessions where account = ?", (account["id"],))
            return self.new_session(db, account["id"]), ""

    def new_session(self, db, account_id, life=SESSION_S):
        secret = secrets.token_urlsafe(32)
        db.execute("delete from sessions where expires < ?", (now(),))
        db.execute("insert into sessions (hash, account, expires) values (?, ?, ?)",
                   (digest(secret), account_id, now() + life))
        return secret

    def sign_in(self, callsign_typed, password, address, is_remembered=True):
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
            key = client_key_of(password, account["callsign"])
            if account["client_key"] != key:
                db.execute("update accounts set client_key = ? where id = ?",
                           (key, account["id"]))
            return self.new_session(db, account["id"], SESSION_S if is_remembered
                                    else SESSION_UNREMEMBERED_S), ""

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
            return False, ("That is not a radio key. It is the twelve letters the radio "
                           "shows as RADIO_KEY, like abcd-jklm-wxyz.")
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
                       (account["id"], suffix, cookie, "", now()))
        return True, ("%s is added. It will come on line within a quarter of a minute, "
                      "if it is switched on and its remote access is on."
                      % radio_id(account["callsign"], suffix))

    def radio_new_cookie(self, account, radio, cookie_typed):
        """the cookie the owner made anew on the radio takes the old one's
        place; the radio's name stays. Returns (ok,
        what to tell the owner)"""
        cookie = tidy_secret(cookie_typed)
        if not cookie:
            return False, ("That is not a radio key. It is the twelve letters the radio "
                           "shows as RADIO_KEY, like abcd-jklm-wxyz.")
        with self.store.open() as db:
            found = db.execute("select suffix, cookie from radios where id = ? and account = ?",
                               (radio, account["id"])).fetchone()
            if found is None:
                return False, "You have no such radio."
            if found["cookie"] == cookie:
                return False, ("That is the key it has. Tap RADIO_KEY on the radio and choose "
                               "NEW KEY first.")
            db.execute("update radios set cookie = ? where id = ?", (cookie, radio))
        return True, ("%s has its new radio key. It will come on line within a quarter of a "
                      "minute; the old key is no longer taken."
                      % radio_id(account["callsign"], found["suffix"]))

    def radio_remove(self, account, radio):
        with self.store.open() as db:
            db.execute("delete from radios where id = ? and account = ?",
                       (radio, account["id"]))

    def radios_of(self, callsign):
        """for remote.py: the radios of a callsign whose account is active"""
        with self.store.open() as db:
            return db.execute(
                "select radios.*, accounts.callsign, accounts.client_key from radios "
                "join accounts "
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
        """the radios on line that have a grid, for anyone to see and hear:
        each one's number, its owner's callsign, its four-character square,
        whether its owner lets it be listened to, how many are listening to
        it and the squares they are in, as far as their addresses say, and
        no more"""
        found = []
        with self.store.open() as db:
            for row in db.execute(
                    "select radios.id, accounts.callsign, radios.grid, radios.listeners, "
                    "radios.listener_places, radios.streaming from radios "
                    "join accounts on accounts.id = radios.account where radios.online = 1 "
                    "and accounts.status = 'active' order by accounts.callsign, radios.id"):
                square = square_of(row["grid"])
                if square is not None:
                    found.append({"radio": row["id"], "callsign": row["callsign"],
                                  "grid": square["grid"], "lat": square["lat"],
                                  "lon": square["lon"], "streaming": bool(row["streaming"]),
                                  "listening": row["listeners"],
                                  "listeners": listeners_of(row["listener_places"])})
        return found

    def copied_by(self, radio, after):
        """what a radio that is on the globe has copied in FT8 lately, for
        anyone: each message's number, when it was heard, the dial, the
        radio's own time for it, its strength, pitch and text. What it has
        copied since the message of a number, if one is given"""
        if not radio.isdigit() or not after.isdigit():
            return []
        if not any(shown["radio"] == int(radio) for shown in self.on_the_map()):
            return []
        return [{name: row[name] for name in ("id", "heard", "hz", "sent", "strength", "pitch",
                                              "text")}
                for row in decodes.Decodes(self.settings.decodes).recent(int(radio), int(after))]

    def snapshot(self, account, radio):
        """the newest copy of one of the account's radios' settings and logbook"""
        with self.store.open() as db:
            return db.execute(
                "select snapshots.*, radios.suffix from snapshots join radios "
                "on radios.id = snapshots.radio where radios.id = ? and radios.account = ?",
                (radio, account["id"])).fetchone()


def nav_bar(account=None, guard=None):
    """the bar along the top of every page, as the author asked: Home and
    Map on the left and, at the right, Sign in, or whom the page is signed
    in as with Sign out beside it. He has two accounts and goes from one to
    the other; Sign out was once only at the foot of his own page"""
    right = "<a href=\"/signin\">Sign in</a>"
    if account is not None:
        right = "Signed in as <strong>%s</strong>" % html.escape(account["callsign"])
        if guard is not None:
            right += ("<form method=\"post\" action=\"/signout\">"
                      "<input type=\"hidden\" name=\"guard\" value=\"%s\">"
                      "<button>Sign out</button></form>" % guard)
    return ("<nav class=\"bar\"><a class=\"site\" href=\"/\">my.sbitx.net</a>"
            "<a href=\"/\">Home</a><a href=\"/map\">Map</a><a href=\"/upgrade\">Upgrade</a>"
            "<a href=\"/help\">Help</a>"
            "<span class=\"who\">%s</span></nav>" % right)


def page(title, body, account=None, guard=None):
    return ("<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<title>%s - my.sbitx.net</title><link rel=\"stylesheet\" href=\"/style.css\">"
            "</head><body>%s<main>%s"
            "<p class=\"foot\">Accounts for sBitx and zBitx owners.</p></main></body></html>\n"
            % (html.escape(title), nav_bar(account, guard), body))


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

    def page(self, title, body):
        """a page of the site, with the bar along its top saying who is
        signed in, found here for every page alike, so that no page shows
        Sign in to someone who is"""
        secret = self.session_secret()
        account = self.service.session_account(secret)
        return page(title, body, account, self.form_guard(secret) if account else None)

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
        self.send_header("Referrer-Policy", "same-origin")
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def redirect(self, where, headers=()):
        self.send(303, "", headers=(("Location", where),) + tuple(headers))

    def session_cookie(self, secret, life=SESSION_S):
        """with no life the cookie is the browser's to forget when it is
        closed: a sign-in not to be remembered"""
        secure = "; Secure" if self.cookie_name().startswith("__Host-") else ""
        kept = "; Max-Age=%d" % life if life is not None else ""
        return ("Set-Cookie", "%s=%s; Path=/%s; HttpOnly; SameSite=Lax%s"
                % (self.cookie_name(), secret, kept, secure))

    # the pages

    # The owner's pages, as the author asked on 2026-10-16 after finding them
    # confusing: each says what can be done, in the order it is wanted, and
    # not how it works. A new owner is led through three steps; a radio's
    # card leads with Operate; what is seldom wanted, a new cookie and
    # removing a radio, is on the radio's Manage page under the question it
    # answers; and how things work, the cookie and the window on another
    # computer among them, is on Help, apart from the pages used every day.

    def home(self, account, told="", is_wrong=False):
        if account is None:
            self.send(200, self.page("Welcome", said(told, is_wrong)
                + "<h2>Your sBitx or zBitx, from anywhere</h2>"
                "<p>Operate your radio from a browser or a phone, and listen to the "
                "radios that are on line now.</p>"
                "<p><a class=\"action\" href=\"/signin\">Sign in</a> &nbsp; "
                "<a href=\"/register\">Register your callsign</a> &nbsp;&middot;&nbsp; "
                "<a href=\"/map\">Listen on the map</a>, no account needed</p>"))
            return
        radios = self.service.radios(account["id"])
        cards = ""
        for radio in radios:
            number = radio["id"]
            more = ["<a href=\"/map#%d\">Listen</a>" % number]
            if self.service.snapshot(account, number) is not None:
                more += ["<a href=\"/radios/settings?radio=%d\">Radio settings</a>" % number,
                         "<a href=\"/radios/log?radio=%d\">Logbook</a>" % number]
            more.append("<a href=\"/radios/manage?radio=%d\">Manage</a>" % number)
            if radio["online"]:
                go = "<a class=\"action\" href=\"/operate?radio=%d\">Operate</a>" % number
            else:
                go = ("<span class=\"waiting\">Switch the radio on, with REMOTE on in its "
                      "CONNECTIONS, to operate it from here.</span>")
            cards += ("<div class=\"card\"><div class=\"name\">%s</div>"
                      "<div class=\"state %s\">%s%s</div>%s<div class=\"links\">%s</div></div>"
                      % (html.escape(radio_id(account["callsign"], radio["suffix"])),
                         "on" if radio["online"] else "off",
                         "on line" if radio["online"] else "off line",
                         " &middot; " + html.escape(radio["grid"]) if radio["grid"] else "",
                         go, "".join(more)))
        if not radios:
            body = ("<h2>Welcome, %s</h2>"
                    "<p>Three steps to operate your radio from any browser:</p>"
                    "<ol class=\"journey\">"
                    "<li class=\"now\"><strong>Add your radio.</strong> It takes a minute, "
                    "with the radio switched on beside you.<br>"
                    "<a class=\"action\" href=\"/radios/add\">Add Radio</a></li>"
                    "<li><strong>It comes on line.</strong> Your radio signs in here by "
                    "itself whenever it is on.</li>"
                    "<li><strong>Operate it.</strong> From this page, on a computer or a "
                    "phone, where you can also install it as an app.</li></ol>"
                    % html.escape(account["callsign"]))
        else:
            hint = ""
            if not any(radio["online"] for radio in radios):
                hint = ("<p class=\"said\">None of your radios is on line. On the radio, "
                        "open SET, then CONNECTIONS, and see that REMOTE is on and "
                        "REMOTE_STATUS says ONLINE. <a href=\"/help#offline\">More help</a>"
                        "</p>")
            body = ("<div class=\"heading\"><h2>Your radios</h2>"
                    "<a href=\"/radios/add\">+ Add Radio</a></div>%s"
                    "<div class=\"cards\">%s</div>" % (hint, cards))
        self.send(200, self.page(account["callsign"], said(told, is_wrong) + body))

    def add_radio_page(self, account, told="", is_wrong=False, suffix=""):
        """the form that adds a radio, with what to do at the radio first"""
        callsign = html.escape(account["callsign"])
        self.send(200, self.page("Add Radio", said(told, is_wrong)
            + "<p><a href=\"/\">&larr; Your radios</a></p><h2>Add your radio</h2>"
            "<p>Have the radio switched on beside you. On the radio:</p>"
            "<ol class=\"steps\">"
            "<li>Open <strong>SET</strong>. Check that <strong>MYCALLSIGN</strong> is "
            "<strong>%s</strong>, and set <strong>MYGRID</strong>, such as MK97fj, to be on "
            "the map.</li>"
            "<li>Open <strong>CONNECTIONS</strong> and turn <strong>REMOTE</strong> on.</li>"
            "<li><strong>RADIO_KEY</strong>, on the same screen, shows twelve letters. "
            "Type them below.</li>"
            "</ol>"
            "<form method=\"post\" action=\"/radios/add\">"
            "<input type=\"hidden\" name=\"guard\" value=\"%s\">"
            "<label>The radio key, the twelve letters of RADIO_KEY <input type=\"text\" name=\"cookie\" "
            "class=\"code\" maxlength=\"20\" autocomplete=\"off\" "
            "autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\" "
            "placeholder=\"abcd-jklm-wxyz\" required></label>"
            "<label>A name for the radio, such as home or shack <input type=\"text\" "
            "name=\"suffix\" value=\"%s\" maxlength=\"10\" autocomplete=\"off\" "
            "autocapitalize=\"none\" required></label>"
            "<button>Add the radio</button></form>"
            "<p class=\"small\">Within a quarter of a minute REMOTE_STATUS on the radio says "
            "ONLINE, and the radio is on your page, ready to operate. "
            "<a href=\"/help#cookie\">What is the radio key?</a></p>"
            % (callsign, self.form_guard(self.session_secret()), html.escape(suffix))))

    def keys_page(self, account, number, told="", is_wrong=False):
        """a radio's Manage page: the two things seldom wanted, each under
        the question that leads an owner to it"""
        mine = [radio for radio in self.service.radios(account["id"])
                if str(radio["id"]) == str(number)]
        if not mine:
            self.send(404, self.page("Not found", "<p>You have no such radio.</p>"))
            return
        radio = mine[0]
        name = html.escape(radio_id(account["callsign"], radio["suffix"]))
        guard = self.form_guard(self.session_secret())
        hidden = ("<input type=\"hidden\" name=\"guard\" value=\"%s\">"
                  "<input type=\"hidden\" name=\"radio\" value=\"%d\">" % (guard, radio["id"]))
        self.send(200, self.page("Manage %s" % radio_id(account["callsign"], radio["suffix"]),
            said(told, is_wrong)
            + "<p><a href=\"/\">&larr; Your radios</a></p><h2>Manage %s</h2>"
            "<p>%s%s &middot; added %s</p>"
            "<h3>Did you make the radio a new key?</h3>"
            "<p>Only if you tapped <strong>RADIO_KEY</strong> on the radio and chose NEW KEY, "
            "to stop a copy of it signing in. Type the new twelve letters of RADIO_KEY here "
            "and the radio comes back on line.</p>"
            "<form method=\"post\" action=\"/radios/cookie\">%s"
            "<label>The new twelve letters <input type=\"text\" name=\"cookie\" "
            "class=\"code\" maxlength=\"20\" autocomplete=\"off\" "
            "autocapitalize=\"none\" autocorrect=\"off\" spellcheck=\"false\" "
            "placeholder=\"abcd-jklm-wxyz\" required></label>"
            "<button>Save</button></form>"
            "<h3>Remove this radio</h3>"
            "<p>When you sell it or give it away. You can add it again later.</p>"
            "<form method=\"post\" action=\"/radios/remove\">%s"
            "<button class=\"danger\">Remove %s</button></form>"
            % (name, "on line" if radio["online"] else "off line",
               " &middot; " + html.escape(radio["grid"]) if radio["grid"] else "",
               when(radio["created"]), hidden, hidden, name)))

    def help_page(self):
        """how things work, apart from the pages used every day"""
        self.send(200, self.page("Help",
            "<h2>Help</h2>"
            "<h3 id=\"start\">Getting started</h3>"
            "<ol class=\"steps\"><li><a href=\"/register\">Register</a> your callsign "
            "and choose a password.</li>"
            "<li><a href=\"/radios/add\">Add your radio</a>, with it switched on beside "
            "you.</li>"
            "<li>On your page, press <strong>Operate</strong> beside the radio.</li></ol>"
            "<h3 id=\"app\">Install it on your phone</h3>"
            "<p>Open your radio's Operate page, then:</p>"
            "<ul><li><strong>Android, Chrome:</strong> the menu &#8942;, then "
            "<em>Install app</em>.</li>"
            "<li><strong>iPhone, Safari:</strong> Share, then <em>Add to Home "
            "Screen</em>.</li>"
            "<li><strong>A computer, Chrome or Edge:</strong> the install icon at the "
            "right of the address bar.</li></ul>"
            "<p>It opens full screen and on its side, and keeps up to date by itself. "
            "Tick <em>Remember me</em> when you sign in on the phone, so that the app "
            "stays signed in.</p>"
            "<h3 id=\"offline\">My radio is off line</h3>"
            "<p>On the radio, open SET, then CONNECTIONS:</p>"
            "<ul><li><strong>REMOTE</strong> should be on.</li>"
            "<li><strong>REMOTE_STATUS</strong> should say ONLINE. If it says ADD RADIO_KEY "
            "AT MY.SBITX.NET, the radio has not been added here yet, or it was given a new "
            "key: add it, or type RADIO_KEY's letters on its Manage page. If it says SET "
            "MYCALLSIGN FIRST, set MYCALLSIGN to your callsign.</li>"
            "<li>The radio needs the internet; its Wi-Fi must be on.</li></ul>"
            "<h3 id=\"cookie\">What is the radio key?</h3>"
            "<p>Twelve letters your radio makes for itself, shown as RADIO_KEY among its "
            "CONNECTIONS. You type them here once, when you add the radio, and the radio "
            "uses them to sign in from then on. You never need them again, unless you make "
            "the radio a new one, by tapping RADIO_KEY and choosing NEW KEY, to stop a copy "
            "of the radio signing in.</p>"
            "<h3 id=\"window\">The zbitx window on another computer</h3>"
            "<p>The window can work your radio through my.sbitx.net as well as the browser "
            "can: start it as <span class=\"command\">zbitx_gui remote "
            "<em>callsign-name</em></span>, such as "
            "<span class=\"command\">zbitx_gui remote vu2ese-home</span>. The first time, "
            "it asks for the password you sign in here with. Changing that password "
            "signs it out.</p>"))

    def upgrade_page(self):
        """how an owner puts zbitxre on a zbitx or an sbitx v3, by the one
        line that runs setup/install.sh. Written with the author on
        2026-10-17 from what went wrong for the first tester: the radio was
        not on the Wi-Fi, he typed only the address and not the whole line,
        the radio's clock said 2000, so that my.sbitx.net's certificate was
        refused, and the panel needed its firmware by the USB cable, which
        is put on by switching the radio on with the tuning knob held in and
        only then plugging the cable in, the author's order. The
        line that sets the clock is given with the time the page was served,
        in UTC, which is near enough for a certificate"""
        self.send(200, self.page("Upgrade your radio",
            "<h2>Upgrade your radio to zbitxre</h2>"
            "<p>For a <strong>zBitx</strong> or an <strong>sBitx v3</strong>. Your "
            "settings and logbook come across, and the old software is kept: you can "
            "go back to it.</p>"

            "<h3>Before you start</h3>"
            "<ul class=\"steps\">"
            "<li>The radio is switched on, <strong>not transmitting</strong>, and "
            "connected to <strong>your Wi-Fi</strong>, with the internet. Steps 2 "
            "and 3 check this.</li>"
            "<li>Allow about <strong>20 minutes</strong> on a zBitx, less on an "
            "sBitx. Do not switch the radio off while it runs.</li></ul>"

            "<h3>1. Open a terminal on the radio</h3>"
            "<p>On the radio's screen, with a keyboard and mouse, open the "
            "<em>Terminal</em> from the menu.</p>"

            "<h3>2. Check the radio is on the internet</h3>"
            "<p>In the terminal type:</p>"
            "<p class=\"command\">ping -c 3 my.sbitx.net</p>"
            "<p>You should see three lines with <em>bytes from</em>. If it says "
            "<em>unknown host</em>, <em>unreachable</em> or nothing for a while, the "
            "radio is not on the internet: connect it to your Wi-Fi first.</p>"

            "<h3>3. Check the radio's date</h3>"
            "<p>Type:</p>"
            "<p class=\"command\">date</p>"
            "<p>It must show <strong>today's date</strong>. A radio that has not "
            "been on the internet since it was switched on can think it is the year "
            "2000; then my.sbitx.net's security certificate looks not yet valid, and "
            "step 4 fails with <em>SSL certificate problem</em>. If the date is wrong, "
            "set it by typing this line, which has today's date and time in it:</p>"
            "<p class=\"command\">sudo date -u -s \"%s\"</p>"
            "<p>and check with <span class=\"command\">date</span> again.</p>"

            "<h3>4. Type the whole line</h3>"
            "<p>In the terminal, type this <strong>whole line, exactly as it "
            "is</strong>, then press Enter:</p>"
            "<div class=\"oneline\"><code id=\"line\">curl -fsSL "
            "https://my.sbitx.net/install | bash</code>"
            "<button id=\"copy\" type=\"button\">Copy</button></div>"
            "<div class=\"check\"><strong>Check it letter by letter before you press "
            "Enter.</strong> The address alone does nothing: it must be the whole "
            "line.<ul>"
            "<li>All small letters, except a <strong>capital S</strong> and a "
            "<strong>capital L</strong> in <span class=\"spelled\">-fsSL</span>.</li>"
            "<li>A space after <span class=\"spelled\">curl</span>, after "
            "<span class=\"spelled\">-fsSL</span>, and on each side of "
            "<span class=\"spelled\">|</span>.</li>"
            "<li><span class=\"spelled\">|</span> is the upright bar, usually "
            "Shift and the backslash key; not a capital I or a small l.</li>"
            "<li><span class=\"spelled\">my.sbitx.net</span>: s-b-i-t-x, with a "
            "dot on each side.</li></ul>"
            "<p>If you are reading this page on the radio itself, press "
            "<strong>Copy</strong> and paste it into the terminal instead of typing "
            "it.</p></div>"

            "<h3>5. Answer its questions</h3>"
            "<p>It says what it will do and asks before it changes anything. It "
            "builds the new software, which takes most of the time, then asks to "
            "restart the radio. At the end it offers to put the radio on "
            "my.sbitx.net.</p>"

            "<h3>6. zBitx only, if it asks: the front panel's firmware</h3>"
            "<p>Older front panels need their firmware put on once with a USB cable; "
            "the line in step 4 says so if yours does. Later updates reach the "
            "panel by themselves.</p>"
            "<ol class=\"steps\">"
            "<li>Download <a href=\"/zbitx/front_panel.uf2\" download>"
            "front_panel.uf2</a> on your computer.</li>"
            "<li>Switch the radio <strong>off</strong>.</li>"
            "<li>Hold the <strong>tuning knob pressed in</strong>, and while holding "
            "it, switch the radio <strong>on</strong>. Then let go of the knob.</li>"
            "<li>Connect the panel's USB socket to your computer. A drive called "
            "<strong>RPI-RP2</strong> appears.</li>"
            "<li>Copy <span class=\"command\">front_panel.uf2</span> onto that "
            "drive. The drive goes away by itself and the panel restarts.</li>"
            "<li>Unplug the cable.</li></ol>"
            "<p>The order matters: plugged in first, the cable keeps the panel "
            "powered, and it never starts as the drive.</p>"

            "<h3>Afterwards</h3>"
            "<ul class=\"steps\">"
            "<li>To use the radio from your phone or computer, "
            "<a href=\"/radios/add\">add it here</a>.</li>"
            "<li>Later updates: on the radio, <em>SET</em>, then <em>Update</em>.</li>"
            "<li>To go back to the old software, type "
            "<span class=\"command\">~/zbitxre/setup/undo_install.sh</span> in the "
            "radio's terminal and restart it.</li></ul>"
            "<script src=\"%s\"></script>"
            % (time.strftime("%Y-%m-%d %H:%M", time.gmtime()), static_address("upgrade.js"))))

    def register_form(self, told="", is_wrong=False, callsign="", email=""):
        s = self.service.settings
        if s.registration == "closed":
            self.send(200, self.page("Register", "<p>Registration is not open yet.</p>"))
            return
        invite = ""
        if s.registration == "invite":
            invite = ("<label>Invitation code <input type=\"text\" name=\"invite\" "
                      "autocomplete=\"off\" required></label>")
        challenge, signature = self.service.pow_challenge()
        self.send(200, self.page("Register", said(told, is_wrong)
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
        self.send(200, self.page("Sign in", said(told, is_wrong)
                            + "<form method=\"post\" action=\"/signin\">"
                            "<label>Callsign <input type=\"text\" name=\"callsign\" value=\"%s\" "
                            "maxlength=\"10\" autocapitalize=\"characters\" "
                            "autocomplete=\"username\" required></label>"
                            "<label>Password <input type=\"password\" name=\"password\" "
                            "autocomplete=\"current-password\" required></label>"
                            "<label class=\"tick\"><input type=\"checkbox\" name=\"remember\">"
                            " Remember me on this device, for a month</label>"
                            "<button>Sign in</button></form>"
                            "<p><a href=\"/forgot\">Forgot the password?</a> &middot; "
                            "<a href=\"/register\">Register</a></p>" % html.escape(callsign)))

    def forgot_form(self, told=""):
        self.send(200, self.page("Forgot the password", said(told)
                            + "<form method=\"post\" action=\"/forgot\">"
                            "<label>Callsign <input type=\"text\" name=\"callsign\" "
                            "maxlength=\"10\" autocapitalize=\"characters\" required></label>"
                            "<button>Mail me a link</button></form>"))

    def password_form(self, secret, told="", is_wrong=False):
        with self.service.store.open() as db:
            account = self.service.token_account(db, secret)
        if account is None:
            self.send(200, self.page("Choose a password", said(
                "This link has been used or has run out. Ask for another.", True)
                + "<p><a href=\"/forgot\">Forgot the password?</a> &middot; "
                "<a href=\"/register\">Register</a></p>"))
            return
        self.send(200, self.page("Choose a password", said(told, is_wrong)
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

    def operate_page(self, account, number):
        """the page that works one of the owner's radios"""
        mine = [radio for radio in self.service.radios(account["id"])
                if str(radio["id"]) == number]
        if not mine:
            self.send(404, self.page("Not found", "<p>You have no such radio.</p>"))
            return
        name = radio_id(account["callsign"], mine[0]["suffix"])
        sockets = self.service.settings.sockets
        self.send(200, OPERATE_PAGE % (mine[0]["id"], html.escape(name),
                                       static_address("operate.css"),
                                       mine[0]["id"], html.escape(name),
                                       html.escape(sockets), static_address("sound.js"),
                                       static_address("operate.js")),
                  policy=PAGE_POLICY + "; manifest-src 'self'; img-src 'self'; worker-src 'self'; "
                  "connect-src 'self' "
                  + sockets)

    def kept_page(self, account, number, what):
        """a radio's settings or its logbook, as they were last copied"""
        kept = None
        if number.isdigit():
            kept = self.service.snapshot(account, int(number))
        if kept is None:
            self.send(404, self.page("Not found", "<p>Nothing is kept for that radio.</p>"))
            return
        rows = ""
        if what == "settings":
            title = "Radio settings of %s" % radio_id(account["callsign"], kept["suffix"])
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
        self.send(200, self.page(title, "<h2>%s</h2><p>As copied from the radio on %s UTC.</p>%s"
                            "<p><a href=\"/\">Back</a></p>"
                            % (html.escape(title),
                               time.strftime("%Y-%m-%d %H:%M", time.gmtime(kept["taken"])),
                               table)))

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
            secret = self.session_secret()
            self.send(200, MAP_PAGE.replace("<!--bar-->", nav_bar(
                account, self.form_guard(secret) if account else None)), policy=PAGE_POLICY + "; connect-src 'self' "
                      + self.service.settings.sockets)
        elif url.path == "/operate":
            if account is None:
                self.signin_form("Sign in first.")
            else:
                self.operate_page(account, query.get("radio", [""])[0])
        elif url.path == "/map.js":
            self.send(200, MAP_SCRIPT, "text/javascript; charset=utf-8")
        elif url.path == "/map.css":
            self.send(200, MAP_STYLE, "text/css; charset=utf-8")
        elif url.path == "/operate.json":
            # the files the page that works a radio is made of, as it names
            # them, each with the mark of what is in it: the page's UPDATE
            # compares them with its own to say whether there is a newer one
            self.send(200, json.dumps([static_address(name) for name in
                                       ("operate.css", "sound.js", "operate.js")]),
                      "application/json")
        elif url.path in RELEASED:
            # what an owner fetches to upgrade a radio: the one-line
            # installer, curl -fsSL https://my.sbitx.net/install | bash, and
            # the panel's firmware for its one update with the USB cable.
            # They are copies of setup/install.sh and release/panel, put in
            # release/ beside this file when the service is deployed
            name, kind = RELEASED[url.path]
            path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "release", name)
            if not os.path.exists(path):
                self.send(404, self.page("Not found", "<p>There is no such page.</p>"))
            else:
                with open(path, "rb") as file:
                    self.send(200, file.read(), kind, keep="no-cache")
        elif url.path == "/service-worker.js":
            # a worker obeys the policy its own script comes with: this one
            # fetches pages of the site, and nothing else
            self.send(200, SERVICE_WORKER, "text/javascript; charset=utf-8",
                      policy="default-src 'none'; connect-src 'self'")
        elif url.path == "/operate.webmanifest":
            self.send(200, json.dumps(operate_manifest(query.get("radio", [""])[0])),
                      "application/manifest+json")
        elif url.path == "/decodes.json":
            self.send(200, json.dumps(self.service.copied_by(
                query.get("radio", [""])[0], query.get("after", ["0"])[0])), "application/json")
        elif url.path == "/map.json":
            self.send(200, json.dumps(self.service.on_the_map()), "application/json")
        elif url.path.startswith("/static/") and url.path[len("/static/"):] in STATIC:
            name = url.path[len("/static/"):]
            with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "static", name), "rb") as file:
                # the coastlines do not change; the scripts do, and a phone
                # that kept yesterday's would not have today's mending
                self.send(200, file.read(), STATIC[name],
                          keep="public, max-age=86400" if name == "land.json" else "no-cache")
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
        elif url.path in ("/radios/add", "/radios/manage", "/radios/keys"):
            if account is None:
                self.signin_form("Sign in first.")
            elif url.path == "/radios/add":
                self.add_radio_page(account)
            else:
                self.keys_page(account, query.get("radio", [""])[0])
        elif url.path == "/help":
            self.help_page()
        elif url.path == "/upgrade":
            self.upgrade_page()
        elif url.path in ("/radios/settings", "/radios/log"):
            if account is None:
                self.signin_form("Sign in first.")
            else:
                self.kept_page(account, query.get("radio", [""])[0],
                               url.path.rpartition("/")[2])
        else:
            self.send(404, self.page("Not found", "<p>There is no such page.</p>"))


    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        body = self.read_body()
        if body is None:
            self.send(413, "too long\n", "text/plain")
            return
        if not self.is_from_this_site():
            self.send(403, self.page("Refused", "<p>That form did not come from this site.</p>"))
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
                self.send(200, self.page("Register", said(told)))
            else:
                self.register_form(told, True, form.get("callsign", ""), form.get("email", ""))
        elif url.path == "/signin":
            is_remembered = form.get("remember", "") == "on"
            secret, told = self.service.sign_in(form.get("callsign", ""),
                                                form.get("password", ""), self.address(),
                                                is_remembered)
            if secret is None:
                self.signin_form(told, True, form.get("callsign", ""))
            else:
                self.redirect("/", [self.session_cookie(
                    secret, SESSION_S if is_remembered else None)])
        elif url.path == "/forgot":
            self.service.forgot(form.get("callsign", ""), self.address())
            self.send(200, self.page("Forgot the password", said(
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
            self.send(403, self.page("Refused", "<p>That form did not come from this site.</p>"))
        elif url.path == "/signout":
            self.service.sign_out(secret)
            self.redirect("/", [self.session_cookie("", 0)])
        elif url.path == "/radios/add":
            ok, told = self.service.radio_add(account, form.get("suffix", ""),
                                              form.get("cookie", ""))
            if ok:
                self.home(account, told)
            else:
                self.add_radio_page(account, told, True, form.get("suffix", ""))
        elif url.path == "/radios/cookie":
            ok, told = False, "You have no such radio."
            if form.get("radio", "").isdigit():
                ok, told = self.service.radio_new_cookie(account, int(form["radio"]),
                                                         form.get("cookie", ""))
            self.keys_page(account, form.get("radio", ""), told, not ok)
        elif url.path == "/radios/remove":
            if form.get("radio", "").isdigit():
                self.service.radio_remove(account, int(form["radio"]))
            self.redirect("/")
        else:
            self.send(404, self.page("Not found", "<p>There is no such page.</p>"))



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
