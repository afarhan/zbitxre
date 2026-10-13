#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
remote.py — the part of my.sbitx.net that is reached over plain TCP: where
an owner's radio and the owner's client meet.

    remote.py [--settings settings.ini]

PROTOCOL.md, at the top of the repository, is the account of what is said
on these connections and why. This is the service's end of it. It shares
its database and its settings with accounts.py, which is the website, and
like it needs nothing but Python's standard library.

WHO CONNECTS

A radio at home cannot be connected to from outside, so its daemon,
clients/remote/zbitx_remote.c, connects out to here and stays. A client, the
window on another computer, connects to here as well. Both are met with a
challenge. Neither is asked for the account's password, and nothing here
is inside TLS: the author's ruling, since these are amateur radio
communications and all that is wanted is to be sure who is on the line.

    CHALLENGE <32 hex digits>                      from here, first
    AUTH radio <callsign> <nonce> <proof>          from a radio's daemon, or
    AUTH client <radio's name> <nonce> <proof>     from a client
    WELCOME <radio's name> <proof>                 from here

A radio answers with its cookie, the twelve letters it made for itself and
its owner typed into the website when adding it. It says only whose it is,
by the callsign set on it; which of the owner's radios it is, this end
finds by trying the cookie of each, five at most. A client answers with
the client password of the radio it names, vu2ese-home.

The proof is HMAC-SHA256 of "zbitx-auth-1 <radio or client> <the name as
sent> <challenge> <nonce>", in hex, and its key is not the twelve letters
themselves but what PBKDF2 makes of them, so that someone who overhears an
answer cannot find the letters by trying them all: accounts.stretch() is
that, with the callsign as the salt for a cookie and the radio's name for a
client password. The proof sent back is of the same text with "server"
first, and shows that this end has the secret too.

A wrong proof is answered "ERROR AUTH" and the connection closed. A radio
that has not been added on the website yet is refused in just this way,
every quarter of a minute, while it shows its cookie and waits; so an
address is turned away only after a hundred wrong proofs in a quarter of an
hour, which no radio waiting to be added comes near and which makes
guessing at twelve letters over the network hopeless.

WHAT A RADIO'S DAEMON SAYS NEXT

    radio                 this is the connection the daemon keeps while the
                          owner has remote access on. Nothing is sent down
                          it but PING from the daemon each minute and PONG
                          back; the grid locator set on the radio, when the
                          connection is made and whenever it is changed,
        GRID MK97fk
                          which is kept with the radio, the author having
                          asked for the grid to reach the service along
                          with the callsign; and from here, when somebody
                          wants the radio:
        SESSION <token>
    session <token>       this is a connection the daemon made because it
                          was sent that token. Its other end, on the radio,
                          is a connection to the radio program, and from
                          here on every byte is passed between that and
                          whoever asked.

WHAT HAPPENS NEXT FOR A CLIENT

It has named its radio already. The radio's daemon is told, and when its
session connection arrives the client is sent

    CONNECTED vu2ese-home

and is from then on talking to its radio, in the radio's own protocol. If
the radio is not on line, or does not answer, it is sent "ERROR connect"
and the reason.

ONE CLIENT WORKS A RADIO, AND MANY MAY LISTEN

The author's rule. A radio has one client at a time through here: a second
that asks for it while the first is on is told the radio is in use. Its
sound is another matter and may go to as many as want it.

THE RADIO'S SOUND

The radio program sends what its speaker plays out on the radio as RTP, and
its daemon hears that; rtp_audio.c and zbitx_remote.c say how. While
somebody is listening, and only then, this service tells the daemon, on the
connection it keeps,

    AUDIO ON <16 hex digits, the stream's id> <32 hex digits, a nonce>

and the daemon sends each RTP packet here by UDP, to the same port number,
as it hears it:

    8 bytes     the stream's id
    172 bytes   the RTP packet: 12 of header, 160 of mu-law, 20 ms of sound
    8 bytes     a tag

UDP has no connection to have answered a challenge on, so each packet
carries its proof. The tag is the first 8 bytes of HMAC-SHA256 of the id
and the RTP packet, with a key made for this stream alone and never sent:
HMAC-SHA256 of "zbitx-audio-1 <nonce>" with the radio's stretched cookie as
its key, in hex. A packet with a tag that is wrong, or an id that is not a
stream's, or an RTP sequence number at or behind one already taken, is
dropped. When the last listener goes the daemon is told "AUDIO OFF". The
stream's key and its listeners are kept in this program's memory and
nowhere else; the author asked that nothing be looked up in the database
for each packet, and nothing is.

Each packet's 160 bytes of sound are passed to everyone listening to that
radio. Listeners today are web browsers, which cannot take UDP: a browser
opens a WebSocket here, through nginx, at /listen?radio=<number>, from the
website's globe, and is sent each packet's sound as a binary message, and
the words "online" and "offline" as the radio comes and goes, and
"listening 5", how many are listening to that radio, whenever that changes;
the number is kept with the radio too, for the globe to show beside its
callsign to those who are not listening. When packets
did not come, which the RTP sequence numbers show, the listeners are told
how many before the next that did, "lost 2": UDP loses some, and a player
that knows where can fill the gap, where one that does not joins two
pieces of sound that do not belong together and clicks.

With the sound, the daemon says where the radio is tuned and in what mode,
"TUNED 7074000 USB", when the sound begins and when either changes, and
each listener is sent it, "tuned 7074000 USB", for the player to show: the
author asked for the frequency and the mode there. It is what the radio is
doing on the air, and as public as the sound.

Anyone may listen, signed in or not: the author's ruling, a radio's
transmissions and receptions being public anyway. What is asked is that
the page be the website's own, so that another site cannot use this one's
radios as its own; that the radio be one that is shown on the globe; and
that there be not too many: a hundred listeners to a radio, and six from
one address, beyond which a browser is sent the word "full" and no sound. A listener that falls behind is not queued for: it misses
packets and stays up to date. Clients that take UDP, the window among
them, are to come.

SPEECH TO TRANSMIT

The owner working a radio from a browser speaks into the browser's
microphone, and the radio is to send it. The author's design: the path of
the radio's sound, in reverse. The page sends twenty milliseconds of
mu-law at a time, 160 bytes, as binary messages on the WebSocket it works
the radio by, which only the owner has. Each is sent on by UDP to the
address the radio's sound is coming from, the one way back in through a
home's router, in a packet made as the daemon's are:

    8 bytes     the stream's id
    172 bytes   an RTP packet: 12 of header, 160 of mu-law
    8 bytes     a tag

The tag's key is made as the sound's is, of other words: HMAC-SHA256 of
"zbitx-speech-1 <nonce>" with the stretched cookie as its key, in hex. So
a packet of the radio's sound, which anyone on its way can copy, cannot be
sent to the radio as speech. The packets are numbered one after another
for as long as the stream lasts, and the daemon drops one that is not
ahead of the last, so that one copied on the way cannot be sent again.
There is a stream only while somebody listens; the owner's page always
does, and speech with no stream to go by is dropped. Nothing here says
when the radio is to send: that is TX, a line like any other.

HOW MUCH IT IS USED

The author asked for this when he made the globe's address public and his
own sound stopped, to know whether the service had given way under those
who came. It had not: three or four were listening, and his telephone's
browser had been put behind another program. But nothing here said so.

Now each listener's coming and going is said, with the address it came
from, how long it stayed and how many are listening; so is one turned away
for want of room. And every five minutes in which anything happened, one
line:

    remote: usage, last 5 min: radios on line 1, listening now 4, most at
    once 6, came 9, turned away 0, packets from radios 15000, to listeners
    52000, kept from slow listeners 12, speech packets 0, working a radio
    now 1

It goes where everything this program says goes, the system's journal:

    journalctl -u sbitx-remote | grep usage

On the droplet, a 2 GB machine with one processor, ninety listeners to one
radio took an eighth of the processor, and every one of them was sent
every packet (2026-10-13). The limit of a hundred to a radio is this
program's own, LISTENERS_A_RADIO, not the machine's.

WORKING A RADIO FROM A BROWSER

The website has a client of its own, a page that works the radio as the
window does; accounts/static/operate.js is it. A browser cannot make the
plain connection the window makes, so the page opens a WebSocket here,
through nginx, at /work?radio=<number>, and this service is the go-between.
It is for the radio's owner, signed in to the website: the browser's
session says who it is, and no client password is asked for. It is then a
client like the window: the one client the radio may have, with a session
of its own to the radio program by way of the daemon. Each text message
from the page is lines of the radio's protocol, passed to the radio as
they are, and what the radio says goes back as text messages of whole
lines. The page hears the radio as any listener does, at /listen.

A page that works a radio can put it on the air, and a phone goes out of
reach of its network without saying so. So the page sends a full stop
every few seconds, which is not passed on, and if nothing has come from it
for twenty seconds, or its connection closes, the radio is told "TX OFF"
before the session is let go.

NOTHING IS SENT THAT NOBODY ASKED FOR

The author's rule. A radio that is on line and not in use costs a line a
minute each way. The radio's spectrum, its controls and its log pass
through here only inside a session that a client asked for.

THE SETTINGS AND THE LOGBOOK KEPT FOR THE OWNER

When a radio comes on line, this service asks its daemon for a session of
its own, and in it does what any client may: it is sent every control's
value as it connects, which is the copy of the owner's settings, and it
asks for the log, which is the copy of the logbook. It keeps both in the
database, in place of the copy before, for the website to show, and closes
the session. The radio's protocol gives the newest hundred entries of the
log and no more, so that is what is kept for now. keep_a_copy() says how
the end of the values is found in what a radio, which is never quiet, goes
on to say.

HOW IT IS SERVED

By asyncio, in one thread: a connection is a coroutine, and a radio that
is on line and idle costs a few kilobytes. The database is small and near,
and is read and written as it comes, without a thread of its own.
"""

import argparse
import asyncio
import base64
import hmac
import hashlib
import os
import re
import struct
import secrets
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import accounts

HELLO_WAIT_S = 10
SESSION_WAIT_S = 10
RADIO_QUIET_S = 150
CLIENT_QUIET_S = 600
FIRST_LINE_WAIT_S = 10
VALUES_S = 2
COPY_MOST_S = 30
LOG_WAIT_S = 5
LINE_LONGEST = 512
SESSIONS_A_RADIO = 4
CLIENTS_A_RADIO = 1
STREAM_ID_BYTES = 8
RTP_BYTES = 172
RTP_HEADER_BYTES = 12
TAG_BYTES = 8
PACKET_BYTES = STREAM_ID_BYTES + RTP_BYTES + TAG_BYTES
SEQUENCE_AHEAD_MOST = 3000
SPEECH_SOURCE = 0x7a627478
LISTENER_WAITING_MOST = 4096
LISTENERS_A_RADIO = 100
LISTENERS_AN_ADDRESS = 6
REQUEST_LONGEST = 8192
PAGE_QUIET_S = 20
USAGE_EVERY_S = 300
MESSAGE_LONGEST = 65536
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
WRONG_PROOFS = 100
WINDOW_S = 15 * 60
PIPE_BYTES = 4096
PIPE_WAITING_MOST = 32 * 1024
CONTROL_LINE = re.compile(r"^[A-Z][A-Z0-9_]* ?")
TUNED_LINE = re.compile(r"^TUNED [0-9]{3,9} [A-Z0-9]{1,8}$")
# what a radio sends that is not a control's value
NOT_CONTROLS = ("SPECTRUM", "MODULATION", "MESSAGE", "CW", "SAY", "WARN", "QSO", "LOG",
                "LOG_LIST", "TIME", "ERROR", "CONTROL")


def proof(secret, salt, side, name, challenge, nonce):
    """the answer to a challenge; side is radio, client or server, and name
    is what the one connecting called itself"""
    text = "zbitx-auth-1 %s %s %s %s" % (side, name, challenge, nonce)
    return hmac.new(accounts.stretch(secret, salt).encode(), text.encode(),
                    hashlib.sha256).hexdigest()


class Stream:
    """a radio's sound on its way to those listening, and the owner's
    speech on its way back: the keys of the two, which differ, the number
    of the last packet of sound taken, where the sound comes from, and the
    number of the last packet of speech sent"""

    def __init__(self, radio, cookie_key, nonce):
        self.radio = radio
        self.key = self.key_for("zbitx-audio-1", cookie_key, nonce)
        self.speech_key = self.key_for("zbitx-speech-1", cookie_key, nonce)
        self.last = None
        self.address = None
        self.said = secrets.randbelow(65536)
        self.said_samples = 0

    @staticmethod
    def key_for(words, cookie_key, nonce):
        return hmac.new(cookie_key.encode(), (words + " " + nonce).encode(),
                        hashlib.sha256).hexdigest().encode()


class Remote:
    def __init__(self, settings):
        self.settings = settings
        self.service = accounts.Service(settings)
        self.store = self.service.store
        self.lines = {}      # a radio's id -> the writer of its daemon's kept connection
        self.sessions = {}   # a radio's id -> how many sessions it has open
        self.awaited = {}    # a session's token -> (the radio's id, the future it fulfils)
        self.wrong = {}      # an address -> when it gave wrong proofs
        self.listeners = {}  # a radio's id -> the browsers' writers listening to it
        self.listener_from = {}  # a browser's writer -> the address it came from
        self.tuned = {}      # a radio's id -> "7074000 USB", while its sound is asked for
        self.streams = {}    # a stream's id -> its Stream
        self.sound_port = None  # the UDP port, for sending speech back from
        self.stream_of = {}  # a radio's id -> its stream's id, while there is one
        self.heard = 0       # packets of sound taken, for the tests
        self.spoken = 0      # and of speech sent
        # how much the service was used since tell_usage() last said
        self.usage = dict.fromkeys(("came", "turned away", "most", "heard", "sent",
                                    "kept back", "spoken"), 0)
        self.heard_at_start = 0
        self.cookie_keys = {}  # a radio's id -> its stretched cookie, while it is on line
        with self.store.open() as db:
            db.execute("update radios set online = 0, listeners = 0")

    def say(self, text):
        print("remote: %s" % text, flush=True)

    # who is this

    def is_turned_away(self, address):
        since = time.time() - WINDOW_S
        times = [at for at in self.wrong.get(address, []) if at > since]
        self.wrong[address] = times
        return len(times) >= WRONG_PROOFS

    async def read_line(self, reader, wait_s):
        """a line without its end, '' at the end of the connection or when
        nothing comes in time"""
        try:
            line = await asyncio.wait_for(reader.readline(), wait_s)
        except (asyncio.TimeoutError, ValueError, ConnectionError):
            return ""
        return line.decode("utf-8", "replace").strip()

    def who_answered(self, side, name, challenge, nonce, answer):
        """the radio whose secret made that answer, with the secret and its
        salt, or None. A radio's daemon names only its callsign, and the
        cookie of each of that callsign's radios is tried"""
        if side == "radio":
            tried = [(radio, radio["cookie"], name) for radio in self.service.radios_of(name)]
        else:
            radio = self.service.radio_called(name)
            tried = [(radio, radio["client_password"], name)] if radio is not None else []
        for radio, secret, salt in tried:
            if hmac.compare_digest(answer, proof(secret, salt, side, name, challenge, nonce)):
                return radio, secret, salt
        return None

    async def greet(self, reader, writer, address):
        """the challenge; returns the side that answered it, radio or
        client, and the radio it is or wants, or (None, None)"""
        if self.is_turned_away(address):
            writer.write(b"ERROR AUTH too many wrong tries from here\n")
            return None, None
        challenge = secrets.token_hex(16)
        writer.write(("CHALLENGE %s\n" % challenge).encode())
        words = (await self.read_line(reader, HELLO_WAIT_S)).split()
        if (len(words) != 5 or words[0] != "AUTH" or words[1] not in ("radio", "client")
                or not re.fullmatch(r"[0-9a-f]{32}", words[3])):
            writer.write(b"ERROR AUTH expected: AUTH radio|client <name> <nonce> <proof>\n")
            return None, None
        side, name, nonce = words[1], words[2][:40], words[3]
        found = await asyncio.get_running_loop().run_in_executor(
            None, self.who_answered, side, name, challenge, nonce, words[4])
        if found is None:
            self.wrong.setdefault(address, []).append(time.time())
            if side == "radio":
                writer.write(b"ERROR AUTH no radio of that callsign has this cookie; "
                             b"add it at the website\n")
            else:
                writer.write(("ERROR AUTH there is no radio %s with that client password: "
                              "check the name and the password at the website\n"
                              % name.lower()).encode())
            return None, None
        radio, secret, salt = found
        writer.write(("WELCOME %s %s\n" % (
            accounts.radio_id(radio["callsign"], radio["suffix"]),
            proof(secret, salt, "server", name, challenge, nonce))).encode())
        if side == "radio":
            with self.store.open() as db:
                db.execute("update radios set last_seen = ? where id = ?",
                           (accounts.now(), radio["id"]))
        return side, radio

    async def meet(self, reader, writer):
        address = writer.get_extra_info("peername")[0]
        writer.transport.set_write_buffer_limits(high=PIPE_WAITING_MOST)
        is_kept = False
        try:
            side, radio = await self.greet(reader, writer, address)
            if side is None:
                await writer.drain()
            elif side == "radio":
                is_kept = await self.radio(radio, reader, writer)
            else:
                await self.join(radio, reader, writer)
        except (ConnectionError, asyncio.IncompleteReadError):
            pass
        finally:
            if not is_kept:
                writer.close()

    # a radio's daemon

    async def radio(self, device, reader, writer):
        """says whether the connection was handed on, to a session, and is
        not to be closed here"""
        words = (await self.read_line(reader, HELLO_WAIT_S)).split()
        if words == ["radio"]:
            await self.radio_line(device, reader, writer)
            return False
        if len(words) == 2 and words[0] == "session":
            awaited = self.awaited.get(words[1])
            if awaited is not None and awaited[0] == device["id"] and not awaited[1].done():
                writer.write(b"OK\n")
                awaited[1].set_result((reader, writer))
                return True
            writer.write(b"ERROR session nobody is waiting for that one\n")
            return False
        writer.write(b"ERROR expected: radio, or session <token>\n")
        return False

    async def radio_line(self, device, reader, writer):
        radio = device["id"]
        name = accounts.radio_id(device["callsign"], device["suffix"])
        before = self.lines.get(radio)
        if before is not None:
            before.close()
        self.lines[radio] = writer
        with self.store.open() as db:
            db.execute("update radios set online = 1 where id = ?", (radio,))
        writer.write(b"OK\n")
        self.say("%s is on line" % name)
        self.cookie_keys[radio] = accounts.stretch(device["cookie"], device["callsign"])
        self.tell_listeners(radio, "online")
        if self.listeners.get(radio):
            self.start_stream(radio)
        copying = asyncio.ensure_future(self.keep_a_copy(radio))
        try:
            while True:
                line = await self.read_line(reader, RADIO_QUIET_S)
                if line == "PING":
                    writer.write(b"PONG\n")
                    await writer.drain()
                elif line.split(" ")[0] == "GRID":
                    self.keep_the_grid(radio, line.partition(" ")[2])
                elif TUNED_LINE.match(line):
                    self.tuned[radio] = line[len("TUNED "):]
                    self.tell_listeners(radio, "tuned " + self.tuned[radio])
                else:
                    break
        finally:
            copying.cancel()
            if self.lines.get(radio) is writer:
                del self.lines[radio]
                self.forget_stream(radio)
                self.tell_listeners(radio, "offline")
                with self.store.open() as db:
                    db.execute("update radios set online = 0, last_seen = ? where id = ?",
                               (accounts.now(), radio))
                self.say("%s is off line" % name)

    # the radio's sound

    def start_stream(self, radio):
        """the radio's daemon is asked for its sound, on a stream with an id
        and a key of its own"""
        line = self.lines.get(radio)
        if line is None or radio in self.stream_of:
            return
        stream = secrets.token_bytes(STREAM_ID_BYTES)
        nonce = secrets.token_hex(16)
        self.streams[stream] = Stream(radio, self.cookie_keys[radio], nonce)
        self.stream_of[radio] = stream
        self.heard_at_start = self.heard
        self.say("the sound of radio %d is asked for" % radio)
        line.write(("AUDIO ON %s %s\n" % (stream.hex(), nonce)).encode())

    def forget_stream(self, radio):
        self.tuned.pop(radio, None)
        stream = self.stream_of.pop(radio, None)
        if stream is not None:
            del self.streams[stream]
            self.say("the sound of radio %d is no longer asked for, after %d packets" % (
                radio, self.heard - self.heard_at_start))

    def stop_stream(self, radio):
        if radio in self.stream_of:
            self.forget_stream(radio)
            line = self.lines.get(radio)
            if line is not None:
                line.write(b"AUDIO OFF\n")

    def take_packet(self, packet, address=None):
        """a packet from a radio's daemon: if it is a stream's, and its tag
        and its place in the order are right, its sound goes to the
        listeners, and where it came from is kept, to send speech back to"""
        if len(packet) != PACKET_BYTES:
            return
        stream = self.streams.get(packet[:STREAM_ID_BYTES])
        if stream is None:
            return
        radio, last = stream.radio, stream.last
        tag = hmac.new(stream.key, packet[:-TAG_BYTES], hashlib.sha256).digest()[:TAG_BYTES]
        if not hmac.compare_digest(tag, packet[-TAG_BYTES:]):
            return
        sequence = struct.unpack(">H", packet[STREAM_ID_BYTES + 2:STREAM_ID_BYTES + 4])[0]
        if last is not None and not 0 < (sequence - last) % 65536 < SEQUENCE_AHEAD_MOST:
            return
        if last is not None and (sequence - last) % 65536 > 1:
            # so that a player may cover the gap and not click at it
            self.tell_listeners(radio, "lost %d" % ((sequence - last) % 65536 - 1))
        stream.last = sequence
        stream.address = address
        self.heard += 1
        self.usage["heard"] += 1
        sound = packet[STREAM_ID_BYTES + RTP_HEADER_BYTES:-TAG_BYTES]
        frame = bytes([0x82, 126]) + struct.pack(">H", len(sound)) + sound
        for listener in self.listeners.get(radio, ()):
            if listener.transport.get_write_buffer_size() < LISTENER_WAITING_MOST:
                listener.write(frame)
                self.usage["sent"] += 1
            else:
                self.usage["kept back"] += 1

    def send_speech(self, radio, sound):
        """twenty milliseconds of the owner's speech, as the page sent it,
        go to the radio's daemon to be transmitted: by UDP, to where the
        radio's sound is coming from, numbered and tagged"""
        stream = self.streams.get(self.stream_of.get(radio))
        if (stream is None or stream.address is None or self.sound_port is None
                or len(sound) != RTP_BYTES - RTP_HEADER_BYTES):
            return
        stream.said = (stream.said + 1) % 65536
        stream.said_samples = (stream.said_samples + len(sound)) % (1 << 32)
        packet = (self.stream_of[radio]
                  + struct.pack(">BBHII", 0x80, 0, stream.said, stream.said_samples, SPEECH_SOURCE)
                  + sound)
        tag = hmac.new(stream.speech_key, packet, hashlib.sha256).digest()[:TAG_BYTES]
        self.sound_port.sendto(packet + tag, stream.address)
        self.spoken += 1
        self.usage["spoken"] += 1

    def tell_listeners(self, radio, word):
        for listener in self.listeners.get(radio, ()):
            listener.write(self.text_message(word))

    def listener_of(self, headers, path):
        """the radio a browser may listen to: the one its address names, if
        the page asking is the website's own and the radio is an active
        account's; None otherwise. Nobody need be signed in"""
        if headers.get("origin") != self.settings.base_url:
            return None
        number = path.partition("?radio=")[2]
        if not number.isdigit():
            return None
        with self.store.open() as db:
            return db.execute(
                "select radios.* from radios join accounts on accounts.id = radios.account "
                "where radios.id = ? and accounts.status = 'active'",
                (int(number),)).fetchone()

    def has_room(self, radio, address):
        from_there = sum(1 for listeners in self.listeners.values()
                         for listener in listeners if self.listener_from.get(listener) == address)
        return (len(self.listeners.get(radio, ())) < LISTENERS_A_RADIO
                and from_there < LISTENERS_AN_ADDRESS)

    def owner_of(self, headers, path):
        """the radio a browser may work: the one its address names, if the
        browser is signed in to the website as that radio's owner and the
        page asking is the website's own; None otherwise"""
        if headers.get("origin") != self.settings.base_url:
            return None
        name = "__Host-sid" if self.settings.base_url.startswith("https://") else "sid"
        secret = ""
        for part in headers.get("cookie", "").split(";"):
            key, _, value = part.strip().partition("=")
            if key == name:
                secret = value
        account = self.service.session_account(secret)
        number = path.partition("?radio=")[2]
        if account is None or not number.isdigit():
            return None
        with self.store.open() as db:
            return db.execute(
                "select radios.*, accounts.callsign from radios join accounts "
                "on accounts.id = radios.account where radios.id = ? and radios.account = ?",
                (int(number), account["id"])).fetchone()

    async def read_message(self, reader, wait_s):
        """the next message of a browser's WebSocket: (True, text) or
        (False, bytes); None when it closes, says nothing in time, or sends
        what is too long"""
        try:
            head = await asyncio.wait_for(reader.readexactly(2), wait_s)
            length = head[1] & 0x7f
            if length == 126:
                length = struct.unpack(">H", await reader.readexactly(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", await reader.readexactly(8))[0]
            if length > MESSAGE_LONGEST:
                return None
            mask = await reader.readexactly(4) if head[1] & 0x80 else bytes(4)
            body = bytes(byte ^ mask[at % 4]
                         for at, byte in enumerate(await reader.readexactly(length)))
        except (asyncio.TimeoutError, asyncio.IncompleteReadError, ConnectionError):
            return None
        if head[0] & 0x0f == 8:
            return None
        if head[0] & 0x0f == 1:
            return True, body.decode("utf-8", "replace")
        return False, body

    @staticmethod
    def text_message(text):
        body = text.encode()
        if len(body) < 126:
            return bytes([0x81, len(body)]) + body
        if len(body) < 65536:
            return bytes([0x81, 126]) + struct.pack(">H", len(body)) + body
        return bytes([0x81, 127]) + struct.pack(">Q", len(body)) + body

    async def meet_browser(self, reader, writer):
        """a browser's WebSocket, RFC 6455: its request is answered, and it
        then listens to a radio or works one, as its address says"""
        try:
            request = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), HELLO_WAIT_S)
            lines = request.decode("latin-1").split("\r\n")
            path = (lines[0].split(" ") + ["", ""])[1]
            headers = {}
            for line in lines[1:]:
                name, _, value = line.partition(":")
                headers[name.strip().lower()] = value.strip()
            found = None
            if path.startswith("/listen?"):
                found = self.listener_of(headers, path)
            elif path.startswith("/work?"):
                found = self.owner_of(headers, path)
            key = headers.get("sec-websocket-key", "")
            if found is None or not key:
                writer.write(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
                await writer.drain()
                return
            accept = base64.b64encode(hashlib.sha1((key + WEBSOCKET_GUID).encode()).digest())
            writer.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                         b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept
                         + b"\r\n\r\n")
            if path.startswith("/work?"):
                await self.work(found, reader, writer)
            else:
                await self.listen(found, headers.get("x-real-ip", ""), reader, writer)
        except (asyncio.TimeoutError, asyncio.IncompleteReadError, asyncio.LimitOverrunError,
                ConnectionError, IndexError):
            pass
        finally:
            writer.close()

    async def listen(self, found, address, reader, writer):
        """a browser listening to a radio, until it closes"""
        if not self.has_room(found["id"], address):
            self.usage["turned away"] += 1
            self.say("a listener from %s is turned away from radio %d: no room" % (
                address, found["id"]))
            writer.write(self.text_message("full"))
            await writer.drain()
            return
        radio = found["id"]
        came = time.time()
        self.listener_from[writer] = address
        self.listeners.setdefault(radio, set()).add(writer)
        self.usage["came"] += 1
        self.usage["most"] = max(self.usage["most"], self.listening())
        self.say("a listener has come to radio %d from %s: %d listening" % (
            radio, address, len(self.listeners[radio])))
        try:
            writer.write(self.text_message("online" if radio in self.lines else "offline"))
            if radio in self.tuned:
                writer.write(self.text_message("tuned " + self.tuned[radio]))
            self.start_stream(radio)
            self.count_listeners(radio)
            # what the browser sends is read only to learn that it has closed
            while await self.read_message(reader, None) is not None:
                pass
        finally:
            self.listener_from.pop(writer, None)
            self.listeners[radio].discard(writer)
            self.say("a listener from %s has left radio %d after %d s: %d listening" % (
                address, radio, time.time() - came, len(self.listeners[radio])))
            self.count_listeners(radio)
            if not self.listeners[radio]:
                self.stop_stream(radio)

    def listening(self):
        return sum(len(listeners) for listeners in self.listeners.values())

    def count_listeners(self, radio):
        """how many are listening to a radio is kept with it, for the globe
        to say beside its callsign, and those listening are told"""
        listening = len(self.listeners.get(radio, ()))
        with self.store.open() as db:
            db.execute("update radios set listeners = ? where id = ?", (listening, radio))
        self.tell_listeners(radio, "listening %d" % listening)

    async def tell_usage(self):
        """every USAGE_EVERY_S, if anything happened, a line of how much the
        service was used: who is listening, how many came and were turned
        away, how many packets of sound came from radios and went to
        listeners, and how many were kept from listeners too slow to take
        them"""
        while True:
            await asyncio.sleep(USAGE_EVERY_S)
            used = self.usage
            if not (used["came"] or used["turned away"] or used["heard"] or self.listening()):
                continue
            self.say("usage, last %d min: radios on line %d, listening now %d, most at once %d, "
                     "came %d, turned away %d, packets from radios %d, to listeners %d, "
                     "kept from slow listeners %d, speech packets %d, working a radio now %d" % (
                         USAGE_EVERY_S // 60, len(self.lines), self.listening(), used["most"],
                         used["came"], used["turned away"], used["heard"], used["sent"],
                         used["kept back"], used["spoken"],
                         sum(self.sessions.values())))
            self.usage = dict.fromkeys(used, 0)
            self.usage["most"] = self.listening()

    async def work(self, radio, reader, writer):
        """the owner's browser working a radio: its lines to the radio and
        the radio's to it, until the page goes; then the radio is told to
        stop transmitting, in case it was"""
        name = accounts.radio_id(radio["callsign"], radio["suffix"])
        refusal = ""
        if radio["id"] not in self.lines:
            refusal = "offline"
        elif self.sessions.get(radio["id"], 0) >= CLIENTS_A_RADIO:
            refusal = "busy"
        streams = None
        if not refusal:
            self.sessions[radio["id"]] = self.sessions.get(radio["id"], 0) + 1
            streams = await self.open_session(radio["id"])
            if streams is None:
                self.sessions[radio["id"]] -= 1
                refusal = "offline"
        if refusal:
            writer.write(self.text_message("! " + refusal))
            await writer.drain()
            return
        radio_reader, radio_writer = streams
        writer.write(self.text_message("! connected " + name))
        self.say("the owner's browser is on %s" % name)

        async def from_the_radio():
            waiting = b""
            while True:
                data = await radio_reader.read(PIPE_BYTES)
                if not data:
                    return
                waiting += data
                whole, _, waiting = waiting.rpartition(b"\n")
                if whole:
                    writer.write(self.text_message(whole.decode("utf-8", "replace")))
                    await writer.drain()

        async def from_the_page():
            while True:
                message = await self.read_message(reader, PAGE_QUIET_S)
                if message is None:
                    return
                is_text, body = message
                if not is_text:
                    self.send_speech(radio["id"], body)
                elif body != ".":
                    radio_writer.write(body.encode() + b"\n")
                    await radio_writer.drain()

        ways = [asyncio.ensure_future(from_the_radio()), asyncio.ensure_future(from_the_page())]
        try:
            await asyncio.wait(ways, return_when=asyncio.FIRST_COMPLETED)
        finally:
            for way in ways:
                way.cancel()
            try:
                radio_writer.write(b"TX OFF\n")
                await asyncio.wait_for(radio_writer.drain(), 2)
            except (ConnectionError, asyncio.TimeoutError):
                pass
            radio_writer.close()
            self.sessions[radio["id"]] -= 1
            self.say("the owner's browser has left %s" % name)

    def keep_the_grid(self, radio, said):
        """a grid locator of four or six characters is kept with the radio,
        written as it is by custom, MK97fk; none at all clears it; anything
        else is let pass and what was kept stays"""
        grid = said.strip().upper()
        if grid and not accounts.GRID_FORM.match(grid):
            return
        with self.store.open() as db:
            db.execute("update radios set grid = ? where id = ?",
                       (grid[:4] + grid[4:].lower(), radio))

    async def open_session(self, radio):
        """a connection to the radio program of a radio that is on line, by
        way of its daemon; None if it does not come"""
        line = self.lines.get(radio)
        if line is None or self.sessions.get(radio, 0) >= SESSIONS_A_RADIO:
            return None
        token = secrets.token_hex(16)
        arrives = asyncio.get_running_loop().create_future()
        self.awaited[token] = (radio, arrives)
        try:
            line.write(("SESSION %s\n" % token).encode())
            return await asyncio.wait_for(arrives, SESSION_WAIT_S)
        except (asyncio.TimeoutError, ConnectionError):
            return None
        finally:
            del self.awaited[token]

    async def keep_a_copy(self, radio):
        """the radio's settings and its logbook, read as a client reads them"""
        streams = await self.open_session(radio)
        if streams is None:
            return
        reader, writer = streams
        controls = {}
        log = []
        try:
            await asyncio.wait_for(self.read_a_copy(reader, writer, controls, log),
                                   COPY_MOST_S)
        except (asyncio.TimeoutError, ConnectionError, ValueError, IndexError):
            pass
        finally:
            writer.close()
        if not controls:
            return
        with self.store.open() as db:
            db.execute("insert or replace into snapshots (radio, taken, controls, log) "
                       "values (?, ?, ?, ?)",
                       (radio, accounts.now(), "\n".join(
                           "%s %s" % pair for pair in controls.items()), "\n".join(log)))

    async def read_a_copy(self, reader, writer, controls, log):
        """A radio is never quiet: its meter's reading changes five times a
        second and each change is told. So the values it tells a client as
        it connects are not known to have ended by a pause. They are taken
        for VALUES_S from the first, which is many times longer than they
        take to come, and of a control told more than once the last value
        is kept. The log is asked for after that, and has its own end: the
        line before it says how many entries are coming."""
        line = await self.read_line(reader, FIRST_LINE_WAIT_S)
        ends = time.monotonic() + VALUES_S
        while line and time.monotonic() < ends:
            name, _, value = line.partition(" ")
            if CONTROL_LINE.match(line) and name not in NOT_CONTROLS:
                controls[name] = value[:LINE_LONGEST]
            line = await self.read_line(reader, VALUES_S)
        writer.write(b"log\n")
        await writer.drain()
        coming = None
        while coming is None or len(log) < coming:
            line = await self.read_line(reader, LOG_WAIT_S)
            if not line:
                return
            if line.startswith("LOG_LIST "):
                coming = int(line.split()[1])
            elif line.startswith("LOG ") and coming is not None:
                log.append(line[4:][:LINE_LONGEST])

    # a client

    async def join(self, radio, reader, writer):
        name = accounts.radio_id(radio["callsign"], radio["suffix"])
        if radio["id"] not in self.lines:
            writer.write(b"ERROR connect that radio is not on line\n")
            return
        if self.sessions.get(radio["id"], 0) >= CLIENTS_A_RADIO:
            writer.write(b"ERROR connect that radio is being worked by another client\n")
            return
        streams = await self.open_session(radio["id"])
        if streams is None:
            writer.write(b"ERROR connect the radio did not answer\n")
            return
        radio_reader, radio_writer = streams
        writer.write(("CONNECTED %s\n" % name).encode())
        self.sessions[radio["id"]] = self.sessions.get(radio["id"], 0) + 1
        self.say("a client is on %s" % name)
        ways = [asyncio.ensure_future(self.pass_on(reader, radio_writer)),
                asyncio.ensure_future(self.pass_on(radio_reader, writer))]
        try:
            await asyncio.wait(ways, return_when=asyncio.FIRST_COMPLETED)
        finally:
            for way in ways:
                way.cancel()
            radio_writer.close()
            self.sessions[radio["id"]] -= 1
            self.say("a client has left %s" % name)

    async def pass_on(self, reader, writer):
        try:
            while True:
                data = await reader.read(PIPE_BYTES)
                if not data:
                    return
                writer.write(data)
                await writer.drain()
        except ConnectionError:
            return

    async def serve(self, host, port, web_host="127.0.0.1", web_port=0):
        """the TCP port radios and clients come to, the UDP port of the same
        number that radios send their sound to, and the port nginx brings
        browsers' WebSockets to"""
        server = await asyncio.start_server(self.meet, host, port, limit=LINE_LONGEST * 4)
        port = server.sockets[0].getsockname()[1]
        remote = self

        class Sound(asyncio.DatagramProtocol):
            def datagram_received(self, data, address):
                remote.take_packet(data, address)

        self.sound_port, _ = await asyncio.get_running_loop().create_datagram_endpoint(
            Sound, local_addr=(host, port))
        self.web = await asyncio.start_server(self.meet_browser, web_host, web_port,
                                              limit=REQUEST_LONGEST)
        self.usage_teller = asyncio.ensure_future(self.tell_usage())
        self.say("listening on %s:%d, and for browsers on %s:%d" % (
            host, port, web_host, self.web.sockets[0].getsockname()[1]))
        return server


def main(argv):
    parser = argparse.ArgumentParser(description="where radios and their owners' clients meet")
    parser.add_argument("--settings", default=os.environ.get("ACCOUNTS_SETTINGS"))
    args = parser.parse_args(argv)
    settings = accounts.Settings(args.settings)

    async def run():
        server = await Remote(settings).serve(settings.remote_host, settings.remote_port,
                                              settings.listen_host, settings.listen_port)
        async with server:
            await server.serve_forever()

    asyncio.run(run())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
