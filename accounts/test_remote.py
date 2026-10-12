#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
test_remote.py — a radio reached through the service, from end to end.

    make remote && python3 accounts/test_remote.py

Everything is the real thing but the radio: the service's TCP side
(remote.py), the accounts it shares with the website, and the radio's
daemon, the C program zbitx_remote, which must have been built. In the
radio program's place is a few lines here that speak its protocol: it tells
each client its controls as they connect, takes what clients set, answers
"log", and like a real radio is never quiet. The client is a few lines here
too, answering the challenge as PROTOCOL.md says. All of it runs on this
computer, on ports of its own, with nothing sent anywhere else.

The daemon asks the website again every quarter of a minute while its radio
has not been added there, so the tests that add one take that long each.
"""

import asyncio
import hashlib
import hmac
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
    sets is kept and told to the others, and log is answered. Like a real
    radio it is never quiet: its meter's reading is told to every client
    five times a second, which is what first caught the service out, its
    copy of the settings waiting for a pause that a radio never makes"""

    def __init__(self):
        self.controls = {"FREQ": "7074000", "MODE": "USB", "MYCALLSIGN": "VU2ESE",
                         "MYGRID": "MK97",
                         "REMOTE": "OFF", "REMOTE_STATUS": ""}
        self.clients = []
        self.lock = threading.Lock()
        self.listener = socket.create_server(("127.0.0.1", 0))
        self.port = self.listener.getsockname()[1]
        self.is_open = True
        threading.Thread(target=self.accept, daemon=True).start()
        threading.Thread(target=self.meter, daemon=True).start()

    def meter(self):
        reading = 500
        while self.is_open:
            reading = 500 + (reading + 1) % 6
            self.set("SMETER", str(reading))
            time.sleep(0.2)

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
        self.is_open = False
        self.listener.close()


class Client:
    """the owner's client: it names its radio and answers the challenge with
    that radio's client password"""

    def __init__(self, port, name, password):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=15)
        self.file = self.socket.makefile("rw", newline="\n")
        challenge = self.read().split()[1]
        nonce = secrets.token_hex(16)
        self.say("AUTH client %s %s %s" % (
            name, nonce, remote.proof(password, name, "client", name, challenge, nonce)))
        self.welcome = self.read()
        words = self.welcome.split()
        self.is_server_proven = (len(words) == 3 and hmac.compare_digest(
            words[2], remote.proof(password, name, "server", name, challenge, nonce)))

    def say(self, line):
        self.file.write(line + "\n")
        self.file.flush()

    def read(self):
        """the next line that is not the meter's"""
        line = self.file.readline().strip()
        while line.startswith("SMETER "):
            line = self.file.readline().strip()
        return line

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

        self.loop = asyncio.new_event_loop()
        self.remote = remote.Remote(settings)
        self.service = self.remote.service
        self.service.mailer = self.mail
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
             "--radio-port", str(self.radio.port), "--key", self.key_file],
            stdout=subprocess.DEVNULL)
        self.account = self.make_account("VU2ESE", "farhan@example.org")

    def tearDown(self):
        self.daemon.kill()
        self.daemon.wait()
        self.radio.close()
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

    def wait_for(self, what, said, seconds=30):
        ends = time.time() + seconds
        while time.time() < ends:
            if what():
                return
            time.sleep(0.05)
        self.fail("%s; the radio's status was %r" % (said, self.status()))

    def switch_on(self):
        """REMOTE is turned on, and the radio shows its cookie, the website
        not knowing it; the cookie is returned as it was shown"""
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status().startswith("COOKIE "), "no cookie was shown")
        return self.status()[len("COOKIE "):]

    def switch_on_and_add(self):
        ok, told = self.service.radio_add(self.account, "home", self.switch_on())
        self.assertTrue(ok, told)
        self.wait_for(lambda: self.status() == "ONLINE vu2ese-home",
                      "the radio did not come on line")
        return self.service.radio_called("vu2ese-home")

    def test_off_until_the_owner_turns_it_on(self):
        time.sleep(1.5)
        self.assertIn(self.status(), ("", "OFF"))
        self.assertFalse(os.path.exists(self.key_file))

    def test_turned_on_it_makes_a_cookie_and_shows_it(self):
        shown = self.switch_on()
        self.assertRegex(shown, r"^[a-hjkmnp-z]{4}-[a-hjkmnp-z]{4}-[a-hjkmnp-z]{4}$")
        self.assertEqual(os.stat(self.key_file).st_mode & 0o777, 0o600)
        with open(self.key_file) as file:
            self.assertEqual(file.read().strip(), shown.replace("-", ""))

    def test_added_on_the_website_it_comes_on_line_and_hides_the_cookie(self):
        radio = self.switch_on_and_add()
        self.assertNotIn(radio["cookie"][:4], self.status())
        self.assertEqual(self.service.radio_called("vu2ese-home")["online"], 1)

    def test_the_grid_set_on_the_radio_reaches_the_service_and_follows_it(self):
        self.switch_on_and_add()
        grid = lambda: self.service.radio_called("vu2ese-home")["grid"]
        self.wait_for(lambda: grid() == "MK97", "the grid did not arrive")
        self.radio.set("MYGRID", "mk97FK")
        self.wait_for(lambda: grid() == "MK97fk", "the new grid did not arrive")
        self.radio.set("MYGRID", "nowhere")
        time.sleep(1.5)
        self.assertEqual(grid(), "MK97fk")
        self.radio.set("MYGRID", "")
        self.wait_for(lambda: grid() == "", "the grid was not cleared")

    def test_with_no_callsign_it_asks_for_one_and_makes_no_connection(self):
        self.radio.set("MYCALLSIGN", "")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status() == "SET MYCALLSIGN FIRST", "it did not ask")
        self.radio.set("MYCALLSIGN", "VU2ESE")
        self.wait_for(lambda: self.status().startswith("COOKIE "), "no cookie was shown")

    def test_the_settings_and_the_logbook_are_copied_once_on_line(self):
        radio = self.switch_on_and_add()["id"]
        self.wait_for(lambda: self.service.snapshot(self.account, radio) is not None,
                      "no copy was kept")
        kept = self.service.snapshot(self.account, radio)
        self.assertIn("FREQ 7074000", kept["controls"].splitlines())
        self.assertIn("MYCALLSIGN VU2ESE", kept["controls"].splitlines())
        self.assertEqual(len(kept["log"].splitlines()), 2)
        self.assertIn("VU2XZ", kept["log"])
        self.assertNotIn(self.service.radio_called("vu2ese-home")["cookie"][:8],
                         kept["controls"].replace("-", ""))

    def test_a_client_reaches_its_radio_and_they_talk(self):
        radio = self.switch_on_and_add()
        client = Client(self.port, "VU2ESE-Home", radio["client_password"])
        self.assertTrue(client.welcome.startswith("WELCOME vu2ese-home "))
        self.assertTrue(client.is_server_proven)
        self.assertEqual(client.read(), "CONNECTED vu2ese-home")
        told = client.read_until("REMOTE_STATUS")
        self.assertIn("FREQ 7074000", told)
        client.say("FREQ 14074000")
        self.wait_for(lambda: self.radio.controls["FREQ"] == "14074000",
                      "what the client set did not reach the radio")
        self.radio.set("MODE", "CW")
        self.assertEqual(client.read(), "MODE CW")
        client.close()

    def test_turned_off_the_radio_goes_off_line_and_a_client_is_cut_off(self):
        radio = self.switch_on_and_add()
        client = Client(self.port, "vu2ese-home", radio["client_password"])
        self.assertEqual(client.read(), "CONNECTED vu2ese-home")
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.wait_for(lambda: self.service.radio_called("vu2ese-home")["online"] == 0,
                      "the radio stayed on line")
        client.socket.settimeout(5)
        while client.read():
            pass
        client.close()
        again = Client(self.port, "vu2ese-home", radio["client_password"])
        self.assertEqual(again.read(), "ERROR connect that radio is not on line")
        again.close()

    def test_turned_on_again_it_is_the_same_radio(self):
        self.switch_on_and_add()
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status() == "ONLINE vu2ese-home",
                      "it did not come back on line")

    def test_the_wrong_password_and_another_radios_are_refused(self):
        radio = self.switch_on_and_add()
        wrong = Client(self.port, "vu2ese-home", radio["cookie"])
        self.assertTrue(wrong.welcome.startswith("ERROR AUTH"), wrong.welcome)
        wrong.close()
        other = self.make_account("VU2XYZ", "xyz@example.org")
        self.service.radio_add(other, "home", "abcd-efgh-jkmn")
        theirs = self.service.radio_called("vu2xyz-home")["client_password"]
        stranger = Client(self.port, "vu2ese-home", theirs)
        self.assertTrue(stranger.welcome.startswith("ERROR AUTH"), stranger.welcome)
        stranger.close()
        nobody = Client(self.port, "vu2ese-attic", radio["client_password"])
        self.assertTrue(nobody.welcome.startswith("ERROR AUTH"), nobody.welcome)
        nobody.close()

    def test_a_new_client_password_shuts_out_the_old(self):
        radio = self.switch_on_and_add()
        self.service.radio_new_password(self.account, radio["id"])
        old = Client(self.port, "vu2ese-home", radio["client_password"])
        self.assertTrue(old.welcome.startswith("ERROR AUTH"), old.welcome)
        old.close()
        new = Client(self.port, "vu2ese-home",
                     self.service.radio_called("vu2ese-home")["client_password"])
        self.assertEqual(new.read(), "CONNECTED vu2ese-home")
        new.close()
        self.assertEqual(self.status(), "ONLINE vu2ese-home")

    def test_a_radio_removed_on_the_website_shows_its_cookie_again(self):
        radio = self.switch_on_and_add()
        self.service.radio_remove(self.account, radio["id"])
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status() == "COOKIE " + accounts.show_secret(radio["cookie"]),
                      "the cookie was not shown again")


if __name__ == "__main__":
    unittest.main(verbosity=1, warnings="ignore")
