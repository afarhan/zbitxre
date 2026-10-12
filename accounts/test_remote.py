#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
test_remote.py — a radio reached through the service, from end to end.

    make remote && python3 accounts/test_remote.py

Everything is the real thing but the radio: the website (accounts.py), the
service's TCP side (remote.py) and the radio's daemon, the C program
zbitx_remote, which must have been built. In the radio program's place is
a few lines here that speak its protocol: it tells each client its
controls as they connect, takes what clients set, and answers "log". The
client is a few lines here too, answering the challenge as PROTOCOL.md
says. All of it runs on this computer, on ports of its own, with nothing
sent anywhere else; the daemon asks the website over plain HTTP here, which
curl does as readily as HTTPS.
"""

import asyncio
import hashlib
import hmac
import http.client
import os
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import accounts
import remote

DAEMON = os.path.join(HERE, "..", "zbitx_remote")
PASSWORD = "correct horse battery"


class Mail:
    def __init__(self):
        self.sent = []

    def send(self, to, subject, text):
        self.sent.append(text)


class RadioProgram:
    """in place of sdr: every client is told the controls, what a client
    sets is kept and told to the others, and log is answered"""

    def __init__(self):
        self.controls = {"FREQ": "7074000", "MODE": "USB", "MYCALLSIGN": "VU2ESE",
                         "REMOTE": "OFF", "REMOTE_STATUS": ""}
        self.clients = []
        self.lock = threading.Lock()
        self.listener = socket.create_server(("127.0.0.1", 0))
        self.port = self.listener.getsockname()[1]
        threading.Thread(target=self.accept, daemon=True).start()

    def accept(self):
        while True:
            try:
                client, _ = self.listener.accept()
            except OSError:
                return
            with self.lock:
                self.clients.append(client)
                for name, value in self.controls.items():
                    try:
                        client.sendall(("%s %s\n" % (name, value)).encode())
                    except OSError:
                        pass
            threading.Thread(target=self.serve, args=(client,), daemon=True).start()

    def serve(self, client):
        waiting = b""
        while True:
            try:
                data = client.recv(4096)
            except OSError:
                data = b""
            if not data:
                break
            waiting += data
            while b"\n" in waiting:
                line, waiting = waiting.split(b"\n", 1)
                self.take(client, line.decode())
        with self.lock:
            if client in self.clients:
                self.clients.remove(client)
        client.close()

    def take(self, client, line):
        if line == "log":
            client.sendall(b"LOG_LIST 2 \nLOG 972|2026-04-08|04:45|7130|LSB|VU2XZ|59|59\n"
                           b"LOG 961|2026-04-02|14:10|14074|FT8|VU2ESE|-14 MK97|-09 MK81\n")
            return
        name, _, value = line.partition(" ")
        self.set(name, value, client)

    def set(self, name, value, but=None):
        with self.lock:
            self.controls[name] = value
            for client in self.clients:
                if client is not but:
                    try:
                        client.sendall(("%s %s\n" % (name, value)).encode())
                    except OSError:
                        pass

    def close(self):
        self.listener.close()


class Client:
    """the owner's client: it answers the challenge with its own key"""

    def __init__(self, port, key_id, key):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.file = self.socket.makefile("rw", newline="\n")
        challenge = self.read().split()[1]
        nonce = secrets.token_hex(16)
        self.say("AUTH %s %s %s" % (key_id, nonce, remote.proof(key, "client", challenge, nonce)))
        self.welcome = self.read()
        words = self.welcome.split()
        self.is_server_proven = (len(words) == 3 and hmac.compare_digest(
            words[2], remote.proof(key, "server", challenge, nonce)))

    def say(self, line):
        self.file.write(line + "\n")
        self.file.flush()

    def read(self):
        return self.file.readline().strip()

    def read_until(self, last):
        lines = []
        while True:
            line = self.read()
            if not line:
                return lines
            lines.append(line)
            if line.startswith(last):
                return lines

    def close(self):
        self.socket.close()


@unittest.skipUnless(os.path.exists(DAEMON), "zbitx_remote is not built: make remote")
class RemoteTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        settings = accounts.Settings()
        settings.database = os.path.join(self.folder, "accounts.db")
        settings.registration = "open"
        settings.pow_bits = 0
        self.mail = Mail()
        self.service = accounts.Service(settings, self.mail)

        handler = type("TestHandler", (accounts.Handler,), {"service": self.service,
                                                           "log_message": lambda *a: None})
        self.web = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.web.daemon_threads = True
        settings.base_url = "http://127.0.0.1:%d" % self.web.server_address[1]
        threading.Thread(target=self.web.serve_forever, daemon=True).start()

        self.loop = asyncio.new_event_loop()
        self.remote = remote.Remote(settings)
        self.remote.say = lambda text: None
        started = threading.Event()

        def run():
            asyncio.set_event_loop(self.loop)
            self.tcp = self.loop.run_until_complete(self.remote.serve("127.0.0.1", 0))
            started.set()
            self.loop.run_forever()

        threading.Thread(target=run, daemon=True).start()
        started.wait(5)
        self.port = self.tcp.sockets[0].getsockname()[1]

        self.radio = RadioProgram()
        self.key_file = os.path.join(self.folder, "remote.key")
        self.daemon = subprocess.Popen(
            [DAEMON, "--server", "127.0.0.1", "--port", str(self.port),
             "--web", settings.base_url, "--radio-port", str(self.radio.port),
             "--key", self.key_file, "--name", "shack sbitx"],
            stdout=subprocess.DEVNULL)
        self.account = self.make_account("VU2ESE", "farhan@example.org")

    def tearDown(self):
        self.daemon.kill()
        self.daemon.wait()
        self.radio.close()
        self.web.shutdown()
        self.web.server_close()
        self.loop.call_soon_threadsafe(self.loop.stop)
        shutil.rmtree(self.folder, ignore_errors=True)

    def make_account(self, callsign, email):
        self.service.register(callsign, email, "", "127.0.0.1")
        link = self.mail.sent[-1].split("/password?t=")[1].split()[0]
        self.service.set_password(link, PASSWORD, PASSWORD)
        with self.service.store.open() as db:
            return db.execute("select * from accounts where callsign = ?",
                              (callsign,)).fetchone()

    def status(self):
        return self.radio.controls["REMOTE_STATUS"]

    def wait_for(self, what, said, seconds=20):
        ends = time.time() + seconds
        while time.time() < ends:
            if what():
                return
            time.sleep(0.05)
        self.fail("%s; the radio's status was %r" % (said, self.status()))

    def switch_on_and_link(self):
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status().startswith("CODE "), "no code was shown")
        ok, told = self.service.link_approve(self.account, self.status()[5:])
        self.assertTrue(ok, told)
        self.wait_for(lambda: self.status() == "ONLINE VU2ESE", "the radio did not come on line")

    def client_key(self, account):
        started = self.service.link_start("laptop", "127.0.0.1", "client")
        self.service.link_approve(account, started["user_code"])
        linked = self.service.link_poll(started["device_code"])
        return linked["key_id"], linked["key"]

    def radios_row(self):
        with self.service.store.open() as db:
            return db.execute("select * from radios where kind = 'radio'").fetchone()

    def test_off_until_the_owner_turns_it_on(self):
        time.sleep(1.5)
        self.assertIn(self.status(), ("", "OFF"))
        self.assertIsNone(self.radios_row())
        self.assertFalse(os.path.exists(self.key_file))

    def test_turned_on_the_radio_links_and_comes_on_line(self):
        self.switch_on_and_link()
        row = self.radios_row()
        self.assertEqual((row["name"], row["online"]), ("shack sbitx", 1))
        self.assertEqual(os.stat(self.key_file).st_mode & 0o777, 0o600)
        with open(self.key_file) as file:
            self.assertEqual(file.read().split(), [row["key_id"], row["key"]])

    def test_the_settings_and_the_logbook_are_copied_once_on_line(self):
        self.switch_on_and_link()
        radio = self.radios_row()["id"]
        self.wait_for(lambda: self.service.snapshot(self.account, radio) is not None,
                      "no copy was kept")
        kept = self.service.snapshot(self.account, radio)
        self.assertIn("FREQ 7074000", kept["controls"].splitlines())
        self.assertIn("MYCALLSIGN VU2ESE", kept["controls"].splitlines())
        self.assertEqual(len(kept["log"].splitlines()), 2)
        self.assertIn("VU2XZ", kept["log"])

    def test_a_client_reaches_its_radio_and_they_talk(self):
        self.switch_on_and_link()
        client = Client(self.port, *self.client_key(self.account))
        self.assertTrue(client.welcome.startswith("WELCOME VU2ESE "))
        self.assertTrue(client.is_server_proven)
        client.say("radios")
        listed = client.read_until("RADIOS END")
        radio = self.radios_row()
        self.assertEqual(listed, ["RADIO %s online shack sbitx" % radio["key_id"], "RADIOS END"])
        client.say("connect " + radio["key_id"])
        self.assertEqual(client.read(), "CONNECTED shack sbitx")
        told = client.read_until("REMOTE_STATUS")
        self.assertIn("FREQ 7074000", told)
        client.say("FREQ 14074000")
        self.wait_for(lambda: self.radio.controls["FREQ"] == "14074000",
                      "what the client set did not reach the radio")
        self.radio.set("MODE", "CW")
        self.assertEqual(client.read(), "MODE CW")
        client.close()

    def test_turned_off_the_radio_goes_off_line_and_a_client_is_cut_off(self):
        self.switch_on_and_link()
        client = Client(self.port, *self.client_key(self.account))
        client.say("connect " + self.radios_row()["key_id"])
        self.assertEqual(client.read(), "CONNECTED shack sbitx")
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.wait_for(lambda: self.radios_row()["online"] == 0, "the radio stayed on line")
        client.socket.settimeout(5)
        while client.read():
            pass
        client.close()

    def test_turned_on_again_it_uses_the_key_it_has(self):
        self.switch_on_and_link()
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status() == "ONLINE VU2ESE", "it did not come back on line")
        with self.service.store.open() as db:
            self.assertEqual(db.execute("select count(*) from radios").fetchone()[0], 1)

    def test_a_client_of_another_account_cannot_have_the_radio(self):
        self.switch_on_and_link()
        other = self.make_account("VU2XYZ", "xyz@example.org")
        client = Client(self.port, *self.client_key(other))
        client.say("radios")
        self.assertEqual(client.read_until("RADIOS END"), ["RADIOS END"])
        client.say("connect " + self.radios_row()["key_id"])
        self.assertEqual(client.read(), "ERROR connect you have no such radio")
        client.close()

    def test_a_wrong_key_is_refused(self):
        key_id, key = self.client_key(self.account)
        client = Client(self.port, key_id, key[:-1] + "x")
        self.assertTrue(client.welcome.startswith("ERROR AUTH"))
        client.close()

    def test_a_radio_removed_on_the_website_is_refused_and_says_so(self):
        self.switch_on_and_link()
        self.service.radio_remove(self.account, self.radios_row()["id"])
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status() == "KEY REFUSED", "the key was not refused")
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status().startswith("CODE "), "it did not link afresh")


if __name__ == "__main__":
    unittest.main(verbosity=1, warnings="ignore")
