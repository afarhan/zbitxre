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
                          back, and from here, when somebody wants the
                          radio:
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
import hmac
import hashlib
import os
import re
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
WRONG_PROOFS = 100
WINDOW_S = 15 * 60
PIPE_BYTES = 4096
PIPE_WAITING_MOST = 32 * 1024
CONTROL_LINE = re.compile(r"^[A-Z][A-Z0-9_]* ?")
# what a radio sends that is not a control's value
NOT_CONTROLS = ("SPECTRUM", "MODULATION", "MESSAGE", "CW", "SAY", "WARN", "QSO", "LOG",
                "LOG_LIST", "TIME", "ERROR", "CONTROL")


def proof(secret, salt, side, name, challenge, nonce):
    """the answer to a challenge; side is radio, client or server, and name
    is what the one connecting called itself"""
    text = "zbitx-auth-1 %s %s %s %s" % (side, name, challenge, nonce)
    return hmac.new(accounts.stretch(secret, salt).encode(), text.encode(),
                    hashlib.sha256).hexdigest()


class Remote:
    def __init__(self, settings):
        self.settings = settings
        self.service = accounts.Service(settings)
        self.store = self.service.store
        self.lines = {}      # a radio's id -> the writer of its daemon's kept connection
        self.sessions = {}   # a radio's id -> how many sessions it has open
        self.awaited = {}    # a session's token -> (the radio's id, the future it fulfils)
        self.wrong = {}      # an address -> when it gave wrong proofs
        with self.store.open() as db:
            db.execute("update radios set online = 0")

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
        copying = asyncio.ensure_future(self.keep_a_copy(radio))
        try:
            while True:
                line = await self.read_line(reader, RADIO_QUIET_S)
                if line != "PING":
                    break
                writer.write(b"PONG\n")
                await writer.drain()
        finally:
            copying.cancel()
            if self.lines.get(radio) is writer:
                del self.lines[radio]
                with self.store.open() as db:
                    db.execute("update radios set online = 0, last_seen = ? where id = ?",
                               (accounts.now(), radio))
                self.say("%s is off line" % name)

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

    async def serve(self, host, port):
        server = await asyncio.start_server(self.meet, host, port, limit=LINE_LONGEST * 4)
        self.say("listening on %s:%d" % (host, server.sockets[0].getsockname()[1]))
        return server


def main(argv):
    parser = argparse.ArgumentParser(description="where radios and their owners' clients meet")
    parser.add_argument("--settings", default=os.environ.get("ACCOUNTS_SETTINGS"))
    args = parser.parse_args(argv)
    settings = accounts.Settings(args.settings)

    async def run():
        server = await Remote(settings).serve(settings.remote_host, settings.remote_port)
        async with server:
            await server.serve_forever()

    asyncio.run(run())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
