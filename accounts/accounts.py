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

A RADIO'S KEY, AND A CLIENT'S

A radio does not sign in with its owner's password, and neither does a
client, the program on another computer that works the radio from afar.
Each is given a key of its own, which it saves, and which its owner can take
away again. A radio and a client are linked in the same way, and what is
said of the radio below is so of both; a client says "kind": "client" when
it asks. It is linked as a television is:

    the radio      POST /api/radio/link          is given a device code, kept
                                                 to itself, and a short user
                                                 code, which it shows
    the owner      signs in here, "Link a radio", types the user code
    the radio      POST /api/radio/link/poll     with its device code, every
                                                 few seconds: "pending", and
                                                 once the owner has typed the
                                                 code, the key, once only

    the radio      GET /api/radio/whoami         Authorization: Bearer <key>
                                                 says whose radio the key is

The key is made at the moment the radio collects it, and comes with a key
id, a short name for it that is not secret.

The key is kept here as it is, and not as a hash of it. The radio proves
who it is to remote.py, the part of this service that is reached over plain
TCP, by answering a challenge with a keyed hash, and to check the answer
the service has to work out the same hash, for which it needs the key;
PROTOCOL.md says how and why. The cost is that whoever read this database
would have every radio's key. The file is readable by the service's user
alone.

WHAT IS KEPT, AND HOW

    accounts   callsign, email, the password's hash, status, a note
    tokens     the links mailed out, to verify an address or reset a password
    sessions   who is signed in, in which browser
    radios     each radio's or client's name, its key and its key id, and
               whether it is on line now, which remote.py says
    snapshots  the newest copy of each radio's settings and logbook, which
               remote.py takes when the radio comes on line
    links      a radio's linking under way
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
LINK_CODE_S = 10 * 60
LINK_POLL_S = 5
WINDOW_S = 15 * 60
SIGNIN_TRIES_A_CALLSIGN = 5
SIGNIN_TRIES_AN_ADDRESS = 20
MAILS_AN_ADDRESS = 6
LINKS_AN_ADDRESS = 20
CODE_TRIES_AN_ACCOUNT = 10
PASSWORD_SHORTEST = 10
PASSWORD_LONGEST = 200
BODY_LONGEST = 16 * 1024
RADIO_NAME_LONGEST = 40
RADIOS_AN_ACCOUNT = 20
SCRYPT_N = 2 ** 15
SCRYPT_R = 8
SCRYPT_P = 1
SCRYPT_MEMORY = 64 * 1024 * 1024
HASHES_AT_ONCE = 2
POW_BITS = 18
POW_LIFE_S = 10 * 60
KEY_PREFIX = "sbx_"
KINDS = ("radio", "client")
# a user code is of letters that are not mistaken for one another or for
# figures, and spell no words
CODE_LETTERS = "BCDFGHJKLMNPQRSTVWXZ"
CODE_LENGTH = 8

CALLSIGN_FORM = re.compile(r"^[A-Z0-9]{3,10}$")
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
    name text not null,
    kind text not null,
    key_id text not null unique,
    key text not null unique,
    created integer not null,
    last_seen integer,
    online integer not null default 0
);
create table if not exists snapshots (
    radio integer primary key references radios(id) on delete cascade,
    taken integer not null,
    controls text not null,
    log text not null
);
create table if not exists links (
    device_hash text primary key,
    user_code text not null unique,
    name text not null,
    kind text not null,
    account integer references accounts(id) on delete cascade,
    expires integer not null
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
td form { margin: 0; }
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


def new_user_code():
    letters = "".join(secrets.choice(CODE_LETTERS) for _ in range(CODE_LENGTH))
    return letters[:CODE_LENGTH // 2] + "-" + letters[CODE_LENGTH // 2:]


def tidy_user_code(typed):
    letters = re.sub(r"[^A-Za-z]", "", typed).upper()
    if len(letters) != CODE_LENGTH:
        return ""
    return letters[:CODE_LENGTH // 2] + "-" + letters[CODE_LENGTH // 2:]


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
            # the radios were first kept with a hash of the key and no key;
            # none of those can answer a challenge, and they are let go
            columns = [row["name"] for row in db.execute("pragma table_info(radios)")]
            if columns and "key" not in columns:
                db.execute("drop table radios")
                db.execute("drop table if exists links")
            db.executescript(SCHEMA)

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
            return db.execute("select * from radios where account = ? order by created",
                              (account_id,)).fetchall()

    def link_start(self, name, address, kind="radio"):
        """for the radio: a device code to keep and a user code to show"""
        if kind not in KINDS:
            kind = "radio"
        name = re.sub(r"[^\w .-]", "", name or "")[:RADIO_NAME_LONGEST].strip() or kind
        with self.store.open() as db:
            if tried_too_often(db, "link " + address, LINKS_AN_ADDRESS):
                return None
            note_attempt(db, "link " + address)
            db.execute("delete from links where expires < ?", (now(),))
            device = secrets.token_urlsafe(32)
            user_code = new_user_code()
            db.execute("insert into links (device_hash, user_code, name, kind, expires) "
                       "values (?, ?, ?, ?, ?)",
                       (digest(device), user_code, name, kind, now() + LINK_CODE_S))
        return {
            "device_code": device,
            "user_code": user_code,
            "verify_url": self.settings.base_url + "/radios/link",
            "expires_in": LINK_CODE_S,
            "interval": LINK_POLL_S,
        }

    def link_approve(self, account, typed):
        """for the owner, signed in: the code the radio shows. Returns (ok, text)"""
        user_code = tidy_user_code(typed)
        with self.store.open() as db:
            bucket = "code %d" % account["id"]
            if tried_too_often(db, bucket, CODE_TRIES_AN_ACCOUNT):
                return False, "Too many wrong codes. Try again in a quarter of an hour."
            link = db.execute(
                "select * from links where user_code = ? and expires > ? and account is null",
                (user_code, now())).fetchone()
            if link is None:
                note_attempt(db, bucket)
                return False, ("No radio is showing that code just now. Codes last ten "
                               "minutes; ask the radio for a new one.")
            count = db.execute("select count(*) from radios where account = ?",
                               (account["id"],)).fetchone()[0]
            if count >= RADIOS_AN_ACCOUNT:
                return False, "This account has as many radios as it can. Remove one first."
            db.execute("update links set account = ? where device_hash = ?",
                       (account["id"], link["device_hash"]))
            return True, "\"%s\" is linked. It will say so in a few seconds." % (
                link["name"])

    def link_poll(self, device):
        """for the radio: pending, or once the owner has typed the code, the key"""
        with self.store.open() as db:
            link = db.execute("select * from links where device_hash = ? and expires > ?",
                              (digest(device), now())).fetchone()
            if link is None:
                return {"status": "expired"}
            if link["account"] is None:
                return {"status": "pending"}
            account = db.execute("select * from accounts where id = ?",
                                 (link["account"],)).fetchone()
            key = KEY_PREFIX + secrets.token_urlsafe(32)
            key_id = link["kind"][0] + secrets.token_hex(6)
            db.execute("insert into radios (account, name, kind, key_id, key, created) "
                       "values (?, ?, ?, ?, ?, ?)",
                       (account["id"], link["name"], link["kind"], key_id, key, now()))
            db.execute("delete from links where device_hash = ?", (link["device_hash"],))
            return {"status": "linked", "key_id": key_id, "key": key,
                    "callsign": account["callsign"]}

    def radio_of_key(self, key):
        """whose radio a key is, or None; the services ask this of every request"""
        if not key or not key.startswith(KEY_PREFIX):
            return None
        with self.store.open() as db:
            radio = db.execute(
                "select radios.id, radios.name, accounts.callsign from radios join accounts "
                "on accounts.id = radios.account where radios.key = ? "
                "and accounts.status = 'active'", (key,)).fetchone()
            if radio is None:
                return None
            db.execute("update radios set last_seen = ? where id = ?", (now(), radio["id"]))
            return {"callsign": radio["callsign"], "radio": radio["name"]}

    def radio_remove(self, account, radio_id):
        with self.store.open() as db:
            db.execute("delete from radios where id = ? and account = ?",
                       (radio_id, account["id"]))

    def snapshot(self, account, radio_id):
        """the newest copy of one of the account's radios' settings and logbook"""
        with self.store.open() as db:
            return db.execute(
                "select snapshots.*, radios.name from snapshots join radios "
                "on radios.id = snapshots.radio where radios.id = ? and radios.account = ?",
                (radio_id, account["id"])).fetchone()


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

    def send(self, status, content, kind="text/html; charset=utf-8", headers=()):
        data = content.encode()
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy",
                         "default-src 'none'; style-src 'self'; script-src 'self'; "
                         "form-action 'self'; "
                         "frame-ancestors 'none'; base-uri 'none'")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "same-origin")
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def send_json(self, status, answer):
        self.send(status, json.dumps(answer) + "\n", "application/json")

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
                                "<a href=\"/register\">register</a> one.</p>"))
            return
        guard = self.form_guard(self.session_secret())
        rows = ""
        for radio in self.service.radios(account["id"]):
            state = "a client"
            if radio["kind"] == "radio":
                state = "on line" if radio["online"] else "off line"
            kept = ""
            if self.service.snapshot(account, radio["id"]) is not None:
                kept = ("<a href=\"/radios/settings?radio=%d\">settings</a> &middot; "
                        "<a href=\"/radios/log?radio=%d\">logbook</a>"
                        % (radio["id"], radio["id"]))
            rows += ("<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                     "<td><form method=\"post\" action=\"/radios/remove\">"
                     "<input type=\"hidden\" name=\"guard\" value=\"%s\">"
                     "<input type=\"hidden\" name=\"radio\" value=\"%d\">"
                     "<button>Remove</button></form></td></tr>"
                     % (html.escape(radio["name"]), state, when(radio["created"]),
                        when(radio["last_seen"]), kept, guard, radio["id"]))
        if rows:
            radios = ("<table><tr><th>Name</th><th></th><th>Linked</th><th>Last seen</th>"
                      "<th>Kept here</th><th></th></tr>%s</table>" % rows)
        else:
            radios = "<p>Nothing is linked yet.</p>"
        body = (said(told, is_wrong)
                + "<p>Address on record: %s</p><h2>Your radios and clients</h2>%s"
                "<p><a href=\"/radios/link\">Link a radio or a client</a></p>"
                "<form method=\"post\" action=\"/signout\">"
                "<input type=\"hidden\" name=\"guard\" value=\"%s\">"
                "<button>Sign out</button></form>"
                % (html.escape(account["email"]), radios, guard))
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

    def kept_page(self, account, radio_id, what):
        """a radio's settings or its logbook, as they were last copied"""
        kept = None
        if radio_id.isdigit():
            kept = self.service.snapshot(account, int(radio_id))
        if kept is None:
            self.send(404, page("Not found", "<p>Nothing is kept for that radio.</p>", account))
            return
        rows = ""
        if what == "settings":
            title = "Settings of %s" % kept["name"]
            for line in kept["controls"].splitlines():
                name, _, value = line.partition(" ")
                rows += "<tr><td>%s</td><td>%s</td></tr>" % (html.escape(name),
                                                             html.escape(value))
            table = "<table><tr><th>Control</th><th>Value</th></tr>%s</table>" % rows
        else:
            title = "Logbook of %s" % kept["name"]
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

    def link_form(self, account, told="", is_wrong=False, code=""):
        guard = self.form_guard(self.session_secret())
        self.send(200, page("Link a radio or a client", said(told, is_wrong)
                            + "<p>On the radio, or in the client, choose to link it to "
                            "your account. It shows a code of eight letters. Type it "
                            "here.</p>"
                            "<form method=\"post\" action=\"/radios/link\">"
                            "<input type=\"hidden\" name=\"guard\" value=\"%s\">"
                            "<label>The code <input type=\"text\" name=\"code\" "
                            "class=\"code\" value=\"%s\" maxlength=\"12\" autocomplete=\"off\" "
                            "required></label><button>Link it</button></form>"
                            "<p><a href=\"/\">Back</a></p>" % (guard, html.escape(code)),
                            account))

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
        elif url.path == "/radios/link":
            if account is None:
                self.signin_form("Sign in first, then link the radio.")
            else:
                self.link_form(account, code=query.get("code", [""])[0])
        elif url.path in ("/radios/settings", "/radios/log"):
            if account is None:
                self.signin_form("Sign in first.")
            else:
                self.kept_page(account, query.get("radio", [""])[0],
                               url.path.rpartition("/")[2])
        elif url.path == "/api/radio/whoami":
            self.whoami()
        else:
            self.send(404, page("Not found", "<p>There is no such page.</p>"))

    def whoami(self):
        scheme, _, key = self.headers.get("Authorization", "").partition(" ")
        radio = None
        if scheme.lower() == "bearer":
            radio = self.service.radio_of_key(key.strip())
        if radio is None:
            self.send_json(401, {"error": "no such key"})
        else:
            self.send_json(200, radio)

    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        body = self.read_body()
        if body is None:
            self.send(413, "too long\n", "text/plain")
            return
        if url.path.startswith("/api/"):
            self.post_api(url.path, body)
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
        elif url.path == "/radios/link":
            ok, told = self.service.link_approve(account, form.get("code", ""))
            if ok:
                self.home(account, told)
            else:
                self.link_form(account, told, True, form.get("code", ""))
        elif url.path == "/radios/remove":
            if form.get("radio", "").isdigit():
                self.service.radio_remove(account, int(form["radio"]))
            self.redirect("/")
        else:
            self.send(404, page("Not found", "<p>There is no such page.</p>"))

    def post_api(self, path, body):
        try:
            asked = json.loads(body or "{}")
        except ValueError:
            asked = None
        if not isinstance(asked, dict):
            self.send_json(400, {"error": "the body must be a JSON object"})
        elif path == "/api/radio/link":
            answer = self.service.link_start(str(asked.get("name", "")), self.address(),
                                             str(asked.get("kind", "radio")))
            if answer is None:
                self.send_json(429, {"error": "too many tries; wait a quarter of an hour"})
            else:
                self.send_json(200, answer)
        elif path == "/api/radio/link/poll":
            self.send_json(200, self.service.link_poll(str(asked.get("device_code", ""))))
        else:
            self.send_json(404, {"error": "no such request"})


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
                print("%-9s %-20s %s  linked %s, last seen %s" % (
                    r["kind"], r["name"], r["key_id"], when(r["created"]),
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
