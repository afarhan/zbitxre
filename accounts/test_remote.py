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
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import accounts
import ip_places
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
        self.file.close()
        self.socket.close()


class Speaker:
    """in place of the radio program's sound: RTP packets to the group on
    loopback, as rtp_audio.c sends them, each with sound that says which
    packet it is"""

    def __init__(self):
        self.out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.out.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                            socket.inet_aton("127.0.0.1"))
        self.out.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 0)
        self.sequence = 1000

    def play(self):
        """one packet; returns its 160 bytes of sound"""
        self.sequence += 1
        sound = bytes([self.sequence % 251]) * 160
        header = struct.pack(">BBHII", 0x80, 0, self.sequence & 0xffff,
                             self.sequence * 160, 0x5b17)
        self.out.sendto(header + sound, ("239.255.73.88", 5004))
        return sound


class Browser:
    """a browser's WebSocket to the service, as nginx hands it on; nobody is
    signed in"""

    def __init__(self, port, origin, radio, address="203.0.113.7", path="/listen", cookie=""):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.socket.sendall((
            "GET %s?radio=%d HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\nOrigin: %s\r\nX-Real-IP: %s\r\n"
            "Cookie: sid=%s\r\n\r\n" % (path, radio, origin, address, cookie)).encode())
        self.waiting = b""
        answer = b""
        while b"\r\n\r\n" not in answer:
            more = self.socket.recv(1)
            if not more:
                break
            answer += more
        self.status = answer.split(b"\r\n")[0].decode()
        self.is_accepted = b"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" in answer

    def take(self, count):
        while len(self.waiting) < count:
            more = self.socket.recv(4096)
            if not more:
                raise ConnectionError("closed")
            self.waiting += more
        taken, self.waiting = self.waiting[:count], self.waiting[count:]
        return taken

    def read(self, with_the_count=False):
        """the next message: a text as str, sound as bytes. How many are
        listening, which is said whenever it changes, is passed over
        unless it is asked for"""
        while True:
            head = self.take(2)
            length = head[1]
            if length == 126:
                length = struct.unpack(">H", self.take(2))[0]
            body = self.take(length)
            if head[0] & 0x0f != 1:
                return body
            if with_the_count or not body.startswith(b"listening "):
                return body.decode()

    def speak(self, sound):
        """a binary message of under 65536 bytes, masked as a browser's
        must be"""
        self.socket.sendall(bytes([0x82, 0x80 | 126]) + struct.pack(">H", len(sound))
                            + bytes([1, 2, 3, 4])
                            + bytes(byte ^ (1, 2, 3, 4)[at % 4] for at, byte in enumerate(sound)))

    def say(self, text):
        """a text message, masked as a browser's must be"""
        body = text.encode()
        self.socket.sendall(bytes([0x81, 0x80 | len(body), 1, 2, 3, 4])
                            + bytes(byte ^ (1, 2, 3, 4)[at % 4] for at, byte in enumerate(body)))

    def lines(self, seconds=2):
        """the lines the radio's side sends in a while, the meter's left out"""
        self.socket.settimeout(0.3)
        got = []
        ends = time.time() + seconds
        while time.time() < ends:
            try:
                message = self.read()
            except socket.timeout:
                continue
            except ConnectionError:
                break
            if isinstance(message, str):
                got += [line for line in message.split("\n") if not line.startswith("SMETER ")]
        return got

    def close(self):
        self.socket.sendall(bytes([0x88, 0x80, 0, 0, 0, 0]))
        self.socket.close()


@unittest.skipUnless(os.path.exists(DAEMON), "zbitx_remote is not built: make remote")
class RemoteTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        settings = accounts.Settings()
        settings.database = os.path.join(self.folder, "accounts.db")
        # where the listeners' made-up addresses are said to be
        settings.ip_places = os.path.join(self.folder, "places.bin")
        with open(settings.ip_places, "wb") as out:
            ip_places.build(["203.0.113.0,203.0.113.255,AS,IN,Telangana,Hyderabad,17.385,78.4867"],
                            out)
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
        # in place of the radio program's port for speech to transmit
        self.microphone = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.microphone.bind(("127.0.0.1", 0))
        self.daemon = subprocess.Popen(
            [DAEMON, "--server", "127.0.0.1", "--port", str(self.port),
             "--radio-port", str(self.radio.port), "--key", self.key_file,
             "--microphone-port", str(self.microphone.getsockname()[1])],
            stdout=subprocess.DEVNULL)
        self.account = self.make_account("VU2ESE", "farhan@example.org")

    def tearDown(self):
        self.daemon.kill()
        self.daemon.wait()
        self.microphone.close()
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

    def test_a_second_client_is_told_the_radio_is_in_use(self):
        radio = self.switch_on_and_add()
        first = Client(self.port, "vu2ese-home", radio["client_password"])
        self.assertEqual(first.read(), "CONNECTED vu2ese-home")
        second = Client(self.port, "vu2ese-home", radio["client_password"])
        self.assertEqual(second.read(),
                         "ERROR connect that radio is being worked by another client")
        second.close()
        first.close()
        self.wait_for(lambda: self.remote.sessions.get(radio["id"]) == 0,
                      "the first client did not leave")
        third = Client(self.port, "vu2ese-home", radio["client_password"])
        self.assertEqual(third.read(), "CONNECTED vu2ese-home")
        third.close()

    def listener(self, radio, address="203.0.113.7"):
        port = self.remote.web.sockets[0].getsockname()[1]
        return Browser(port, self.service.settings.base_url, radio, address)

    def hear(self, speaker, browser):
        """sound is played until the browser has one packet of it; the
        packet after that is played, and what the browser then has is
        returned with what was played"""
        browser.socket.settimeout(0.3)
        ends = time.time() + 15
        while time.time() < ends:
            speaker.play()
            try:
                if isinstance(browser.read(), bytes):
                    break
            except socket.timeout:
                pass
        else:
            self.fail("no sound reached the browser")
        browser.socket.settimeout(5)
        time.sleep(0.3)
        browser.socket.setblocking(False)
        try:
            browser.socket.recv(65536)
        except BlockingIOError:
            pass
        browser.waiting = b""
        browser.socket.settimeout(5)
        played = speaker.play()
        return played, browser.read()

    def test_the_radios_sound_reaches_every_browser_listening_and_only_while_one_is(self):
        radio = self.switch_on_and_add()
        speaker = Speaker()
        for _ in range(10):
            speaker.play()
        time.sleep(0.5)
        self.assertEqual(self.remote.heard, 0, "sound was sent with nobody listening")

        first = self.listener(radio["id"])
        self.assertTrue(first.is_accepted, first.status)
        self.assertEqual(first.read(), "online")
        played, heard = self.hear(speaker, first)
        self.assertEqual(heard, played)

        second = self.listener(radio["id"])
        self.assertEqual(second.read(), "online")
        played, heard = self.hear(speaker, second)
        self.assertEqual(heard, played)
        # the first listener has had every packet the second was played;
        # the last it has is the last that was played
        first.socket.settimeout(0.5)
        last = None
        try:
            while True:
                last = first.read()
        except socket.timeout:
            pass
        self.assertEqual(last, played)

        # a packet nobody tagged, to the stream that is open, goes nowhere
        stream = list(self.remote.streams)[0]
        forged = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        before = self.remote.heard
        forged.sendto(stream + struct.pack(">BBHII", 0x80, 0, 60000, 0, 0) + bytes(160)
                      + bytes(8), ("127.0.0.1", self.port))
        time.sleep(0.3)
        self.assertEqual(self.remote.heard, before)

        first.close()
        second.close()
        self.wait_for(lambda: not self.remote.streams, "the stream was not ended")
        time.sleep(0.5)
        before = self.remote.heard
        for _ in range(10):
            speaker.play()
        time.sleep(0.5)
        self.assertEqual(self.remote.heard, before, "sound went on with nobody listening")

    def test_how_many_are_listening_is_told_and_kept_for_the_globe(self):
        radio = self.switch_on_and_add()

        def kept():
            with self.service.store.open() as db:
                return db.execute("select listeners from radios where id = ?",
                                  (radio["id"],)).fetchone()[0]

        def told(browser):
            # the next count it is told, whatever else is said meanwhile
            while True:
                said = browser.read(with_the_count=True)
                if isinstance(said, str) and said.startswith("listening "):
                    return said
        first = self.listener(radio["id"])
        self.assertEqual(told(first), "listening 1")
        self.assertEqual(kept(), 1)
        second = self.listener(radio["id"], address="203.0.113.8")
        self.assertEqual(told(first), "listening 2")
        self.assertEqual(kept(), 2)
        # and roughly where they are: both addresses are in the one square
        with self.service.store.open() as db:
            self.assertEqual(db.execute("select listener_places from radios where id = ?",
                                        (radio["id"],)).fetchone()[0], "MK97:2")
        second.close()
        self.assertEqual(told(first), "listening 1")
        first.close()
        self.wait_for(lambda: kept() == 0, "the count was not put back to nothing")

    def test_a_listener_is_told_how_many_packets_did_not_come(self):
        radio = self.switch_on_and_add()
        speaker = Speaker()
        listener = self.listener(radio["id"])
        self.assertEqual(listener.read(), "online")
        self.hear(speaker, listener)
        listener.socket.settimeout(0.5)
        try:
            while True:
                listener.read()
        except socket.timeout:
            pass
        listener.socket.settimeout(5)
        speaker.sequence += 2  # two packets that the network kept
        sound = speaker.play()
        self.assertEqual(listener.read(), "lost 2")
        self.assertEqual(listener.read(), sound)
        sound = speaker.play()
        self.assertEqual(listener.read(), sound, "told of a loss when there was none")
        listener.close()

    def owners_page(self, account, radio):
        with self.service.store.open() as db:
            cookie = self.service.new_session(db, account["id"])
        port = self.remote.web.sockets[0].getsockname()[1]
        return Browser(port, self.service.settings.base_url, radio, path="/work", cookie=cookie)

    def test_the_owners_browser_works_the_radio_and_leaving_stops_it_sending(self):
        radio = self.switch_on_and_add()
        page = self.owners_page(self.account, radio["id"])
        self.assertTrue(page.is_accepted, page.status)
        told = page.lines()
        self.assertEqual(told[0], "! connected vu2ese-home")
        self.assertIn("FREQ 7074000", told)
        page.say("FREQ 14074000")
        self.wait_for(lambda: self.radio.controls["FREQ"] == "14074000",
                      "what the page set did not reach the radio")
        self.radio.set("MODE", "CW")
        self.assertIn("MODE CW", page.lines())
        page.say(".")
        time.sleep(0.5)
        self.assertNotIn(".", self.radio.controls)

        # one client at a time, the window's kind and the browser's alike
        other = Client(self.port, "vu2ese-home", radio["client_password"])
        self.assertEqual(other.read(),
                         "ERROR connect that radio is being worked by another client")
        other.close()
        second = self.owners_page(self.account, radio["id"])
        self.assertEqual(second.lines(1), ["! busy"])

        page.say("TX ON")
        self.wait_for(lambda: self.radio.controls.get("TX") == "ON", "TX ON did not arrive")
        page.close()
        self.wait_for(lambda: self.radio.controls.get("TX") == "OFF",
                      "the radio was left transmitting when the page went")

    def test_what_the_owner_says_reaches_the_radio_to_be_transmitted(self):
        radio = self.switch_on_and_add()
        page = self.owners_page(self.account, radio["id"])
        self.assertTrue(page.is_accepted, page.status)
        self.microphone.settimeout(0.5)

        # with nobody listening there is no way back to the radio, and
        # what is said goes nowhere
        page.speak(bytes([0x55]) * 160)
        with self.assertRaises(socket.timeout):
            self.microphone.recv(2000)

        # the page listens, as it always does, and the sound begins to come
        speaker = Speaker()
        listener = self.listener(radio["id"])
        self.assertEqual(listener.read(), "online")
        self.hear(speaker, listener)

        said = [bytes([number]) * 160 for number in (0x11, 0x22, 0x33)]
        for sound in said:
            page.speak(sound)
        self.microphone.settimeout(5)
        got = [self.microphone.recv(2000) for _ in said]
        for packet, sound in zip(got, said):
            self.assertEqual(len(packet), 172)
            self.assertEqual(packet[:2], bytes([0x80, 0]), "not RTP of mu-law")
            self.assertEqual(packet[12:], sound)
        numbers = [struct.unpack(">H", packet[2:4])[0] for packet in got]
        self.assertEqual([(number - numbers[0]) % 65536 for number in numbers], [0, 1, 2])

        # what is not twenty milliseconds of mu-law is not sent on
        page.speak(bytes(159))
        page.speak(bytes(400))
        # a packet of the radio's own sound, sent back at the daemon as
        # speech, has the sound's tag and not the speech's, and is dropped;
        # so is a true packet of speech sent a second time
        stream_id = list(self.remote.streams)[0]
        stream = self.remote.streams[stream_id]
        body = stream_id + struct.pack(">BBHII", 0x80, 0, (numbers[2] + 1) % 65536, 0, 0) \
            + bytes([0x66]) * 160
        wrong = hmac.new(stream.key, body, hashlib.sha256).digest()[:8]
        self.loop.call_soon_threadsafe(
            self.remote.sound_port.sendto, body + wrong, stream.address)
        again = stream_id + got[1]
        self.loop.call_soon_threadsafe(
            self.remote.sound_port.sendto,
            again + hmac.new(stream.speech_key, again, hashlib.sha256).digest()[:8],
            stream.address)
        self.microphone.settimeout(0.7)
        with self.assertRaises(socket.timeout):
            self.microphone.recv(2000)

        # and the page is still working the radio
        page.say("AUDIO 31")
        self.wait_for(lambda: self.radio.controls.get("AUDIO") == "31",
                      "a line after the speech did not reach the radio")
        page.close()
        listener.close()

    def test_only_the_owner_signed_in_works_a_radio_from_a_browser(self):
        radio = self.switch_on_and_add()
        other = self.make_account("VU2XYZ", "xyz@example.org")
        self.assertIn("403", self.owners_page(other, radio["id"]).status)
        port = self.remote.web.sockets[0].getsockname()[1]
        nobody = Browser(port, self.service.settings.base_url, radio["id"], path="/work")
        self.assertIn("403", nobody.status)
        with self.service.store.open() as db:
            cookie = self.service.new_session(db, self.account["id"])
        elsewhere = Browser(port, "https://elsewhere.example", radio["id"], path="/work",
                            cookie=cookie)
        self.assertIn("403", elsewhere.status)
        self.assertEqual(self.remote.sessions.get(radio["id"], 0), 0)

    def words_heard(self, browser, seconds=4):
        """the words, not the sound, that a browser is sent in a while"""
        browser.socket.settimeout(0.3)
        said = []
        ends = time.time() + seconds
        while time.time() < ends:
            try:
                message = browser.read()
            except socket.timeout:
                continue
            if isinstance(message, str):
                said.append(message)
        return said

    def test_a_listener_is_told_where_the_radio_is_tuned_and_when_it_moves(self):
        radio = self.switch_on_and_add()
        first = self.listener(radio["id"])
        self.assertEqual(self.words_heard(first), ["online", "tuned 7074000 USB"])
        self.radio.set("FREQ", "14074000")
        self.radio.set("MODE", "CW")
        self.assertEqual(self.words_heard(first)[-1], "tuned 14074000 CW")
        second = self.listener(radio["id"])
        self.assertEqual(self.words_heard(second, 1), ["online", "tuned 14074000 CW"])
        first.close()
        second.close()

    def test_anyone_may_listen_from_the_websites_own_page_and_not_too_many(self):
        radio = self.switch_on_and_add()
        port = self.remote.web.sockets[0].getsockname()[1]
        elsewhere = Browser(port, "https://elsewhere.example", radio["id"])
        self.assertIn("403", elsewhere.status)
        no_such = self.listener(radio["id"] + 50)
        self.assertIn("403", no_such.status)
        self.assertFalse(self.remote.streams)

        six = [self.listener(radio["id"]) for _ in range(6)]
        for one in six:
            self.assertEqual(one.read(), "online")
        seventh = self.listener(radio["id"])
        self.assertEqual(seventh.read(), "full")
        from_another_house = self.listener(radio["id"], "198.51.100.9")
        self.assertEqual(from_another_house.read(), "online")
        for one in six + [from_another_house]:
            one.close()
        self.wait_for(lambda: not self.remote.streams, "the stream was not ended")

    def test_a_suspended_owners_radio_is_not_listened_to(self):
        radio = self.switch_on_and_add()
        with self.service.store.open() as db:
            db.execute("update accounts set status = 'suspended'")
        self.assertIn("403", self.listener(radio["id"]).status)

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
