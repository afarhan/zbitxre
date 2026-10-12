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
challenge and prove who they are with the key the website gave them when
they were linked; neither is asked for a password, and nothing here is
inside TLS: the author's ruling, since these are amateur radio
communications and all that is wanted is to be sure who is on the line.

    CHALLENGE <32 hex digits>                  from here, first
    AUTH <key id> <nonce> <proof>              from whoever connected
    WELCOME <callsign> <proof>                 from here

The proof is HMAC-SHA256, with the key, of "zbitx-auth-1 client <challenge>
<nonce>", in hex; the one sent back is of the same with "server" for
"client", and shows that this end has the key too. A wrong proof is answered
"ERROR AUTH" and the connection closed, and an address that gets it wrong
ten times in a quarter of an hour is turned away for the rest of it.

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

WHAT A CLIENT SAYS NEXT

    radios                which radios the owner has, and which can be
                          reached now:
        RADIO <key id> online shack sbitx
        RADIO <key id> offline portable
        RADIOS END
    connect <key id>      the daemon of that radio is told, and when its
                          session connection arrives the client is sent
        CONNECTED shack sbitx
                          and is from then on talking to its radio, in the
                          radio's own protocol.

A client may only have the radios of the account it is linked to.

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
log and no more, so that is what is kept for now.

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
SETTLE_S = 1.5
LOG_WAIT_S = 5
LINE_LONGEST = 512
SESSIONS_A_RADIO = 4
WRONG_PROOFS = 10
WINDOW_S = 15 * 60
PIPE_BYTES = 4096
PIPE_WAITING_MOST = 32 * 1024
CONTROL_LINE = re.compile(r"^[A-Z][A-Z0-9_]* ?")
# what a radio sends that is not a control's value
NOT_CONTROLS = ("SPECTRUM", "MODULATION", "MESSAGE", "CW", "SAY", "WARN", "QSO", "LOG",
                "LOG_LIST", "TIME", "ERROR", "CONTROL")


def proof(key, side, challenge, nonce):
    text = "zbitx-auth-1 %s %s %s" % (side, challenge, nonce)
    return hmac.new(key.encode(), text.encode(), hashlib.sha256).hexdigest()


class Remote:
    def __init__(self, settings):
        self.settings = settings
        self.store = accounts.Store(settings.database)
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

    def device_of(self, key_id):
        with self.store.open() as db:
            return db.execute(
                "select radios.*, accounts.callsign from radios join accounts "
                "on accounts.id = radios.account where radios.key_id = ? "
                "and accounts.status = 'active'", (key_id,)).fetchone()

    async def read_line(self, reader, wait_s):
        """a line without its end, '' at the end of the connection or when
        nothing comes in time"""
        try:
            line = await asyncio.wait_for(reader.readline(), wait_s)
        except (asyncio.TimeoutError, ValueError, ConnectionError):
            return ""
        return line.decode("utf-8", "replace").strip()

    async def greet(self, reader, writer, address):
        """the challenge; the device that answered it, or None"""
        if self.is_turned_away(address):
            writer.write(b"ERROR AUTH too many wrong tries from here\n")
            return None
        challenge = secrets.token_hex(16)
        writer.write(("CHALLENGE %s\n" % challenge).encode())
        words = (await self.read_line(reader, HELLO_WAIT_S)).split()
        if len(words) != 4 or words[0] != "AUTH" or not re.fullmatch(r"[0-9a-f]{32}", words[2]):
            writer.write(b"ERROR AUTH expected: AUTH <key id> <nonce> <proof>\n")
            return None
        device = self.device_of(words[1])
        key = device["key"] if device is not None else secrets.token_hex(16)
        if device is None or not hmac.compare_digest(
                words[3], proof(key, "client", challenge, words[2])):
            self.wrong.setdefault(address, []).append(time.time())
            writer.write(b"ERROR AUTH that key is not known here\n")
            return None
        writer.write(("WELCOME %s %s\n" % (
            device["callsign"], proof(key, "server", challenge, words[2]))).encode())
        with self.store.open() as db:
            db.execute("update radios set last_seen = ? where id = ?",
                       (accounts.now(), device["id"]))
        return device

    async def meet(self, reader, writer):
        address = writer.get_extra_info("peername")[0]
        writer.transport.set_write_buffer_limits(high=PIPE_WAITING_MOST)
        is_kept = False
        try:
            device = await self.greet(reader, writer, address)
            if device is None:
                await writer.drain()
            elif device["kind"] == "radio":
                is_kept = await self.radio(device, reader, writer)
            else:
                await self.client(device, reader, writer)
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
        before = self.lines.get(radio)
        if before is not None:
            before.close()
        self.lines[radio] = writer
        with self.store.open() as db:
            db.execute("update radios set online = 1 where id = ?", (radio,))
        writer.write(b"OK\n")
        self.say("%s's radio \"%s\" is on line" % (device["callsign"], device["name"]))
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
                self.say("%s's radio \"%s\" is off line" % (device["callsign"],
                                                             device["name"]))

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
        controls = []
        log = []
        try:
            while True:
                line = await self.read_line(reader, SETTLE_S)
                if not line:
                    break
                if CONTROL_LINE.match(line) and line.split(" ")[0] not in NOT_CONTROLS:
                    controls.append(line[:LINE_LONGEST])
            writer.write(b"log\n")
            await writer.drain()
            coming = 0
            while True:
                line = await self.read_line(reader, LOG_WAIT_S)
                if not line:
                    break
                if line.startswith("LOG_LIST "):
                    coming = int(line.split()[1])
                    if coming == 0:
                        break
                elif line.startswith("LOG "):
                    log.append(line[4:][:LINE_LONGEST])
                    if len(log) >= coming:
                        break
        except (ConnectionError, ValueError, IndexError):
            pass
        finally:
            writer.close()
        if not controls:
            return
        with self.store.open() as db:
            db.execute("insert or replace into snapshots (radio, taken, controls, log) "
                       "values (?, ?, ?, ?)",
                       (radio, accounts.now(), "\n".join(controls), "\n".join(log)))

    # a client

    async def client(self, device, reader, writer):
        while True:
            words = (await self.read_line(reader, CLIENT_QUIET_S)).split()
            if not words:
                return
            if words == ["radios"]:
                with self.store.open() as db:
                    for radio in db.execute(
                            "select * from radios where account = ? and kind = 'radio' "
                            "order by name", (device["account"],)):
                        state = "online" if radio["id"] in self.lines else "offline"
                        writer.write(("RADIO %s %s %s\n" % (
                            radio["key_id"], state, radio["name"])).encode())
                writer.write(b"RADIOS END\n")
                await writer.drain()
            elif len(words) == 2 and words[0] == "connect":
                await self.join(device, words[1], reader, writer)
                return
            else:
                writer.write(b"ERROR expected: radios, or connect <key id>\n")
                await writer.drain()

    async def join(self, device, key_id, reader, writer):
        with self.store.open() as db:
            radio = db.execute(
                "select * from radios where key_id = ? and account = ? and kind = 'radio'",
                (key_id, device["account"])).fetchone()
        if radio is None:
            writer.write(b"ERROR connect you have no such radio\n")
            return
        if radio["id"] not in self.lines:
            writer.write(b"ERROR connect that radio is not on line\n")
            return
        streams = await self.open_session(radio["id"])
        if streams is None:
            writer.write(b"ERROR connect the radio did not answer\n")
            return
        radio_reader, radio_writer = streams
        writer.write(("CONNECTED %s\n" % radio["name"]).encode())
        self.sessions[radio["id"]] = self.sessions.get(radio["id"], 0) + 1
        self.say("%s's client \"%s\" is on \"%s\"" % (device["callsign"], device["name"],
                                                    radio["name"]))
        ways = [asyncio.ensure_future(self.pass_on(reader, radio_writer)),
                asyncio.ensure_future(self.pass_on(radio_reader, writer))]
        try:
            await asyncio.wait(ways, return_when=asyncio.FIRST_COMPLETED)
        finally:
            for way in ways:
                way.cancel()
            radio_writer.close()
            self.sessions[radio["id"]] -= 1
            self.say("%s's client \"%s\" has left \"%s\"" % (
                device["callsign"], device["name"], radio["name"]))

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
