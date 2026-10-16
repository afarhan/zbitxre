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
import decodes
import ip_places
import remote

DAEMON = os.path.join(HERE, "..", "zbitx_remote")
# REMOTE_STATUS while the website does not know the radio
NOT_ADDED = "ADD RADIO_KEY AT MY.SBITX.NET"
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
                         "REMOTE": "OFF", "STREAMING": "OFF", "REMOTE_STATUS": ""}
        self.clients = []
        self.messages = []
        self.wanting_messages = []
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
        # a client that asks for the messages copied in FT8 is told those
        # there are, and is told each one after as it is copied
        if line == "messages on":
            with self.lock:
                self.wanting_messages.append(client)
                for message in self.messages:
                    client.sendall(("MESSAGE %s\n" % message).encode())
            return
        if line == "log":
            client.sendall(b"LOG_LIST 2 \nLOG 972|2026-04-08|04:45|7130|LSB|VU2XZ|59|59\n"
                           b"LOG 961|2026-04-02|14:10|14074|FT8|VU2ESE|-14 MK97|-09 MK81\n")
            return
        name, _, value = line.partition(" ")
        self.set(name, value, client)

    def copy(self, message):
        """a message is copied in FT8: those who asked are told"""
        with self.lock:
            self.messages.append(message)
            for client in self.wanting_messages:
                try:
                    client.sendall(("MESSAGE %s\n" % message).encode())
                except OSError:
                    pass

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
    the password of that radio's account, stretched with the callsign"""

    def __init__(self, port, name, password=None):
        password = PASSWORD if password is None else password
        key = accounts.client_key_of(password, name.partition("-")[0])
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=15)
        self.file = self.socket.makefile("rw", newline="\n")
        challenge = self.read().split()[1]
        nonce = secrets.token_hex(16)
        self.say("AUTH client %s %s %s" % (
            name, nonce, remote.proof(key, "client", name, challenge, nonce)))
        self.welcome = self.read()
        words = self.welcome.split()
        self.is_server_proven = (len(words) == 3 and hmac.compare_digest(
            words[2], remote.proof(key, "server", name, challenge, nonce)))

    def say(self, line):
        self.file.write(line + "\n")
        self.file.flush()

    def read(self):
        """the next line that is not the meter's, nor the gateway's word on
        one of its connections, which comes when it comes"""
        line = self.file.readline().strip()
        while line.startswith(("SMETER ", "BLUETOOTH_STATUS ", "SOUND_CARD_STATUS ",
                               "CAT_STATUS ", "VOIP_STATUS ", "RADIO_KEY ")):
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


# the gateway's filter runs on from one packet into the next, so the first
# of a packet's 160 bytes have some of the packet before in them; the tests
# look at the rest
SETTLING = 16


def linear_of(ulaw):
    """the 16-bit sample a byte of mu-law stands for"""
    value = ~ulaw & 0xff
    size = (((value & 0x0f) << 3) + 0x84) << ((value & 0x70) >> 4)
    return 0x84 - size if value & 0x80 else size - 0x84


class Speaker:
    """in place of the radio program's sound: RTP packets to the group on
    loopback, as rtp_audio.c sends them, 16-bit samples at 48000 a second,
    each packet a steady level that says which packet it is. The gateway
    makes the telephone's sound of it, and a steady level comes out one
    byte of mu-law over and over"""

    def __init__(self):
        self.out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.out.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                            socket.inet_aton("127.0.0.1"))
        self.out.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 0)
        self.sequence = 1000

    def play(self):
        """one packet; returns what a listener is to hear of it, the bytes
        of mu-law after the first few"""
        self.sequence += 1
        said = self.sequence % 100 + 1
        header = struct.pack(">BBHII", 0x80, 96, self.sequence & 0xffff,
                             self.sequence * 960, 0x5b17)
        self.out.sendto(header + struct.pack(">h", linear_of(said)) * 960,
                        ("239.255.73.88", 5004))
        return bytes([said]) * (160 - SETTLING)


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
                return body[SETTLING:]
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


def free_port(kind):
    with socket.socket(socket.AF_INET, kind) as spare:
        spare.bind(("127.0.0.1", 0))
        return spare.getsockname()[1]


def whole_packet(sequence, level):
    """RTP of the whole sound, as rtp_voip.c sends the owner's speech: 960
    samples at one steady level"""
    return struct.pack(">BBHII", 0x80, 96, sequence, sequence * 960, 0x766f6970) \
        + struct.pack(">h", level) * 960


@unittest.skipUnless(os.path.exists(DAEMON), "zbitx_remote is not built: make remote")
class RemoteTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        settings = accounts.Settings()
        settings.database = os.path.join(self.folder, "accounts.db")
        settings.decodes = os.path.join(self.folder, "decodes.db")
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
        # in place of setup/bluetooth_audio.sh: it notes the word it was run
        # with, and says what the real one says when it plays
        self.bluetooth_script = os.path.join(self.folder, "bluetooth.sh")
        self.bluetooth_run = os.path.join(self.folder, "bluetooth.run")
        self.write_bluetooth_script("PLAYING")
        # in place of setup/sound_card.py, which the gateway always runs: it
        # says the names of its devices, and makes none
        self.sound_card_script = os.path.join(self.folder, "sound_card.sh")
        with open(self.sound_card_script, "w") as file:
            file.write("#!/bin/sh\necho 'STATUS zBitx_receive zBitx_transmit'\n")
        os.chmod(self.sound_card_script, 0o755)
        # in place of the radio program's port for speech to transmit
        self.microphone = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.microphone.bind(("127.0.0.1", 0))
        # a port for programs that set the radio through Hamlib, free now,
        # and one where the radio program would send the owner's VoIP speech
        self.cat_port = free_port(socket.SOCK_STREAM)
        self.voip_port = free_port(socket.SOCK_DGRAM)
        self.daemon = subprocess.Popen(
            [DAEMON, "--server", "127.0.0.1", "--port", str(self.port),
             "--radio-port", str(self.radio.port), "--key", self.key_file,
             "--microphone-port", str(self.microphone.getsockname()[1]),
             "--bluetooth-script", self.bluetooth_script,
             "--sound-card-script", self.sound_card_script,
             "--cat-port", str(self.cat_port), "--voip-port", str(self.voip_port)],
            stdout=subprocess.DEVNULL)
        self.others = []
        self.account = self.make_account("VU2ESE", "farhan@example.org")

    def tearDown(self):
        for daemon, radio, microphone in self.others:
            daemon.kill()
            daemon.wait()
            radio.close()
            microphone.close()
        self.daemon.kill()
        self.daemon.wait()
        self.microphone.close()
        self.radio.close()
        self.loop.call_soon_threadsafe(self.loop.stop)
        shutil.rmtree(self.folder, ignore_errors=True)

    def make_account(self, callsign, email, password=PASSWORD):
        self.service.register(callsign, email, "", "127.0.0.1")
        link = self.mail.sent[-1].split("/password?t=")[1].split()[0]
        self.service.set_password(link, password, password)
        with self.service.store.open() as db:
            return db.execute("select * from accounts where callsign = ?",
                              (callsign,)).fetchone()

    def change_password(self, callsign, password):
        """as the owner does it: a link mailed, and a new password chosen"""
        self.service.forgot(callsign, "127.0.0.1")
        link = self.mail.sent[-1].split("/password?t=")[1].split()[0]
        self.service.set_password(link, password, password)
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

    def write_bluetooth_script(self, says):
        with open(self.bluetooth_script, "w") as file:
            file.write("#!/bin/sh\necho \"$1\" >> %s\necho 'STATUS %s'\n" % (
                self.bluetooth_run, says))
        os.chmod(self.bluetooth_script, 0o755)

    def bluetooth_words(self):
        if not os.path.exists(self.bluetooth_run):
            return []
        return open(self.bluetooth_run).read().split()

    def switch_on(self):
        """REMOTE is turned on, and the radio says the website does not know
        it; its key, RADIO_KEY, is returned as it was shown"""
        self.radio.set("STREAMING", "ON")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status() == NOT_ADDED, "it did not say it was not added")
        return self.radio.controls["RADIO_KEY"]

    def switch_on_and_add(self):
        ok, told = self.service.radio_add(self.account, "home", self.switch_on())
        self.assertTrue(ok, told)
        self.wait_for(lambda: self.status() == "ONLINE vu2ese-home",
                      "the radio did not come on line")
        return self.service.radio_called("vu2ese-home")

    def test_off_until_the_owner_turns_it_on_but_its_key_is_shown(self):
        """the key is made and shown in RADIO_KEY as soon as the gateway is
        up, so the owner can add the radio first; nothing is connected to"""
        self.wait_for(lambda: self.radio.controls.get("RADIO_KEY"), "no key was shown")
        self.assertIn(self.status(), ("", "OFF"))
        self.assertNotIn("-", self.status())

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
        self.radio.set("STREAMING", "ON")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status() == "SET MYCALLSIGN FIRST", "it did not ask")
        self.radio.set("MYCALLSIGN", "VU2ESE")
        self.wait_for(lambda: self.status() == NOT_ADDED, "it did not say it was not added")

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
        client = Client(self.port, "VU2ESE-Home")
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
        client = Client(self.port, "vu2ese-home")
        self.assertEqual(client.read(), "CONNECTED vu2ese-home")
        self.radio.set("STREAMING", "OFF")
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.wait_for(lambda: self.service.radio_called("vu2ese-home")["online"] == 0,
                      "the radio stayed on line")
        client.socket.settimeout(5)
        while client.read():
            pass
        client.close()
        again = Client(self.port, "vu2ese-home")
        self.assertEqual(again.read(), "ERROR connect that radio is not on line")
        again.close()

    def test_turned_on_again_it_is_the_same_radio(self):
        self.switch_on_and_add()
        self.radio.set("STREAMING", "OFF")
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.radio.set("STREAMING", "ON")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status() == "ONLINE vu2ese-home",
                      "it did not come back on line")

    def test_the_wrong_password_and_another_radios_are_refused(self):
        radio = self.switch_on_and_add()
        wrong = Client(self.port, "vu2ese-home", radio["cookie"])
        self.assertTrue(wrong.welcome.startswith("ERROR AUTH"), wrong.welcome)
        wrong.close()
        other = self.make_account("VU2XYZ", "xyz@example.org", "their own password")
        self.service.radio_add(other, "home", "abcd-efgh-jkmn")
        stranger = Client(self.port, "vu2ese-home", "their own password")
        self.assertTrue(stranger.welcome.startswith("ERROR AUTH"), stranger.welcome)
        stranger.close()
        nobody = Client(self.port, "vu2ese-attic")
        self.assertTrue(nobody.welcome.startswith("ERROR AUTH"), nobody.welcome)
        nobody.close()

    def test_a_new_account_password_shuts_out_the_old(self):
        self.switch_on_and_add()
        self.change_password("VU2ESE", "a newer long password")
        old = Client(self.port, "vu2ese-home")
        self.assertTrue(old.welcome.startswith("ERROR AUTH"), old.welcome)
        old.close()
        new = Client(self.port, "vu2ese-home", "a newer long password")
        self.assertEqual(new.read(), "CONNECTED vu2ese-home")
        new.close()
        self.assertEqual(self.status(), "ONLINE vu2ese-home")

    def test_an_account_from_before_signs_in_at_the_website_first(self):
        self.switch_on_and_add()
        with self.service.store.open() as db:
            db.execute("update accounts set client_key = ''")
        early = Client(self.port, "vu2ese-home")
        self.assertIn("sign in once at the website", early.welcome)
        early.close()
        secret, _ = self.service.sign_in("VU2ESE", PASSWORD, "127.0.0.1")
        self.assertIsNotNone(secret)
        late = Client(self.port, "vu2ese-home")
        self.assertEqual(late.read(), "CONNECTED vu2ese-home")
        late.close()

    def test_a_second_client_is_told_the_radio_is_in_use(self):
        radio = self.switch_on_and_add()
        first = Client(self.port, "vu2ese-home")
        self.assertEqual(first.read(), "CONNECTED vu2ese-home")
        second = Client(self.port, "vu2ese-home")
        self.assertEqual(second.read(),
                         "ERROR connect that radio is being worked by another client")
        second.close()
        first.close()
        self.wait_for(lambda: self.remote.sessions.get(radio["id"]) == 0,
                      "the first client did not leave")
        third = Client(self.port, "vu2ese-home")
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

    def test_bluetooth_is_switched_among_the_connections_with_nothing_else_on(self):
        def said():
            return self.radio.controls.get("BLUETOOTH_STATUS", "")
        self.wait_for(lambda: said() == "OFF", "nothing was said of bluetooth")
        self.radio.set("BLUETOOTH", "ON")
        self.wait_for(lambda: said() == "PLAYING", "it was not said to be playing")
        self.assertEqual(self.bluetooth_words()[0], "start")
        self.assertEqual(self.status(), "OFF", "REMOTE and STREAMING are off")
        self.radio.set("BLUETOOTH", "OFF")
        self.wait_for(lambda: said() == "OFF", "it was not said to be off")
        self.assertEqual(self.bluetooth_words()[-1], "stop")
        # with no earphone there, the script says so, and so does the radio
        self.write_bluetooth_script("NO EARPHONE CONNECTED")
        self.radio.set("BLUETOOTH", "ON")
        self.wait_for(lambda: said() == "NO EARPHONE CONNECTED", "the want of an earphone was not said")

    def test_cat_answers_hamlib_while_its_switch_is_on(self):
        def said():
            return self.radio.controls.get("CAT_STATUS", "")

        def ask(program, line, lines=1):
            program.write(line + "\n")
            program.flush()
            return [program.readline().strip() for _ in range(lines)]
        self.wait_for(lambda: said() == "OFF", "nothing was said of CAT")
        with self.assertRaises(OSError):
            socket.create_connection(("127.0.0.1", self.cat_port), timeout=2)
        self.radio.set("CAT", "ON")
        # where a program is to connect is shown: an address and the port
        self.wait_for(lambda: said().endswith(":%d" % self.cat_port), "the port was not shown")
        self.assertRegex(said(), r"^\d+\.\d+\.\d+\.\d+:\d+$")
        self.assertEqual(self.status(), "OFF", "REMOTE and STREAMING are off")
        connection = socket.create_connection(("127.0.0.1", self.cat_port), timeout=5)
        program = connection.makefile("rw")
        self.assertEqual(ask(program, "f"), ["7074000"])
        self.assertEqual(ask(program, "m", 2), ["USB", "3000"])
        # what the program sets, the radio is set to
        self.assertEqual(ask(program, "F 14074000"), ["RPRT 0"])
        self.wait_for(lambda: self.radio.controls["FREQ"] == "14074000", "the radio was not tuned")
        self.assertEqual(ask(program, "M CW 500"), ["RPRT 0"])
        self.wait_for(lambda: self.radio.controls["MODE"] == "CW", "the mode was not set")
        # and what is changed at the radio, the program is told when it asks
        self.radio.set("FREQ", "7030000")
        self.wait_for(lambda: ask(program, "f") == ["7030000"], "the program was not told the dial")
        # a program that keys the radio and goes has it let go for it
        self.assertEqual(ask(program, "T 1"), ["RPRT 0"])
        self.wait_for(lambda: self.radio.controls.get("TX") == "ON", "the radio was not keyed")
        self.assertEqual(ask(program, "t"), ["1"])
        program.close()
        connection.close()
        self.wait_for(lambda: self.radio.controls.get("TX") == "OFF", "the radio was left keyed")
        # with the switch off there is no port
        connection = socket.create_connection(("127.0.0.1", self.cat_port), timeout=5)
        connection.sendall(b"t\n")
        self.assertEqual(connection.recv(100), b"0\n")
        self.radio.set("CAT", "OFF")
        self.wait_for(lambda: said() == "OFF", "it was not said to be off")
        connection.settimeout(5)
        self.assertEqual(connection.recv(100), b"", "the program was not let go")
        with self.assertRaises(OSError):
            socket.create_connection(("127.0.0.1", self.cat_port), timeout=2)

    def test_the_owners_two_switches_are_what_the_service_allows(self):
        radio = self.switch_on_and_add()
        speaker = Speaker()

        def streaming_kept():
            with self.service.store.open() as db:
                return db.execute("select streaming from radios where id = ?",
                                  (radio["id"],)).fetchone()[0]
        self.wait_for(lambda: self.remote.allowed.get(radio["id"]) == {"remote", "streaming"},
                      "the service was not told what is allowed")
        self.assertEqual(streaming_kept(), 1)
        stranger = self.listener(radio["id"])
        self.assertEqual(stranger.read(), "online")
        self.hear(speaker, stranger)

        # streaming is switched off: the stranger is let go, another is not
        # let in, and the radio stays on line, grey on the globe
        self.radio.set("STREAMING", "OFF")
        self.wait_for(lambda: streaming_kept() == 0, "streaming was not known to be off")
        stranger.socket.settimeout(5)
        said = stranger.read()
        while said != "closed":
            said = stranger.read()
        self.assertEqual(self.listener(radio["id"]).read(), "closed")
        self.assertEqual(self.status(), "ONLINE vu2ese-home")
        self.radio.copy("043015|-12|1797|CQ VU2ESE MK97")
        # its owner, signed in, still hears it, and still works it
        with self.service.store.open() as db:
            cookie = self.service.new_session(db, self.account["id"])
        port = self.remote.web.sockets[0].getsockname()[1]
        owner = Browser(port, self.service.settings.base_url, radio["id"], cookie=cookie)
        self.assertEqual(owner.read(), "online")
        self.hear(speaker, owner)
        page = self.owners_page(self.account, radio["id"])
        self.assertTrue(page.read().startswith("! connected"))
        time.sleep(2.5)
        self.assertEqual(decodes.Decodes(self.service.settings.decodes).recent(radio["id"]), [],
                         "what it copied was kept with streaming off")

        # remote is switched off and streaming on: anyone hears it, nobody
        # works it, and whoever was working it is let go
        self.radio.set("STREAMING", "ON")
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.remote.allowed.get(radio["id"]) == {"streaming"},
                      "remote was not known to be off")
        page.socket.settimeout(10)
        with self.assertRaises((ConnectionError, socket.timeout, struct.error)):
            while True:
                page.read()
        self.assertEqual(self.owners_page(self.account, radio["id"]).read(), "! closed")
        client = Client(self.port, "vu2ese-home")
        self.assertIn("switched off", client.read())
        self.assertEqual(self.listener(radio["id"], address="203.0.113.9").read(), "online")
        self.assertEqual(streaming_kept(), 1)

        # both off: nothing is connected to anything
        self.radio.set("STREAMING", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "it stayed on line with both off")

    def test_a_password_changed_lets_go_of_whoever_is_in(self):
        remote.REVOKED_LOOK_S = 1
        radio = self.switch_on_and_add()
        page = self.owners_page(self.account, radio["id"])
        self.assertTrue(page.read().startswith("! connected"))
        # the owner's password is set anew, which signs every browser out
        with self.service.store.open() as db:
            db.execute("delete from sessions where account = ?", (self.account["id"],))
        page.socket.settimeout(10)
        with self.assertRaises((ConnectionError, socket.timeout, struct.error)):
            while True:
                page.read()
        # a client that is in is let go when the owner's password is changed
        client = Client(self.port, "vu2ese-home")
        self.assertEqual(client.read(), "CONNECTED vu2ese-home")
        self.change_password("VU2ESE", "a newer long password")
        client.socket.settimeout(10)
        with self.assertRaises((ConnectionError, socket.timeout, OSError)):
            while True:
                if client.file.readline() == "":
                    raise ConnectionError("closed")

    def test_what_a_radio_copies_in_ft8_is_kept_once_each(self):
        # copied before the radio was on line: it is not sent
        self.radio.copy("042945|-07|1210|CQ W1AW FN31")
        radio = self.switch_on_and_add()
        self.radio.set("FREQ", "14074000")
        time.sleep(0.5)
        self.radio.copy("043015|-12|1797|CQ VU2ESE MK97")
        self.radio.copy("043015|TX|1500|W1AW VU2ESE MK97")
        self.radio.copy("not a message at all")
        self.radio.copy("043015|-12|1797|CQ VU2ESE MK97")

        def kept():
            return decodes.Decodes(self.service.settings.decodes).recent(radio["id"])
        self.wait_for(lambda: len(kept()) >= 2, "what the radio copied was not kept")
        time.sleep(2.5)
        rows = kept()
        self.assertEqual([row["text"] for row in rows], ["CQ VU2ESE MK97", "W1AW VU2ESE MK97"],
                         "each once, and not the one from before it was on line")
        self.assertEqual((rows[0]["hz"], rows[0]["sent"], rows[0]["strength"], rows[0]["pitch"]),
                         (14074000, "043015", -12, 1797))
        self.assertIsNone(rows[1]["strength"])
        self.assertEqual(self.status(), "ONLINE vu2ese-home",
                         "what was not a message put the radio off line")

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
        other = Client(self.port, "vu2ese-home")
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
        got = [self.microphone.recv(4000) for _ in said]
        for packet, sound in zip(got, said):
            # under a label of what it is and whose, the whole sound: RTP of
            # 960 16-bit samples, the level the page sent once the filter
            # has taken it up
            self.assertEqual(len(packet), 11 + 12 + 1920)
            self.assertEqual(packet[0], 1, "not labelled as speech to transmit")
            self.assertEqual(packet[1:11].rstrip(b"\0"), b"VU2ESE")
            self.assertEqual(packet[11:13], bytes([0x80, 96]), "not RTP of the whole sound")
            level = linear_of(sound[0])
            for sample in struct.unpack(">480h", packet[-960:]):
                self.assertLess(abs(sample - level), 3)
        numbers = [struct.unpack(">H", packet[13:15])[0] for packet in got]
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
        again = stream_id + struct.pack(">BBHII", 0x80, 0, numbers[1], 320,
                                        remote.SPEECH_SOURCE) + said[1]
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
        self.radio.set("STREAMING", "OFF")
        self.radio.set("REMOTE", "OFF")
        self.wait_for(lambda: self.status() == "OFF", "the daemon did not say OFF")
        self.radio.set("STREAMING", "ON")
        self.radio.set("REMOTE", "ON")
        self.wait_for(lambda: self.status() == NOT_ADDED, "it did not say it was not added")
        self.assertEqual(self.radio.controls["RADIO_KEY"], accounts.show_secret(radio["cookie"]))

    # VoIP: the repeater, through two radios' gateways

    def another_radio(self, callsign, email):
        """a second radio, its own account, stand-in and gateway, with only
        VOIP online, added on the website; returns its stand-in and the
        socket its speech to be heard comes to"""
        account = self.make_account(callsign, email)
        radio = RadioProgram()
        radio.controls["MYCALLSIGN"] = callsign
        microphone = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        microphone.bind(("127.0.0.1", 0))
        daemon = subprocess.Popen(
            [DAEMON, "--server", "127.0.0.1", "--port", str(self.port),
             "--radio-port", str(radio.port),
             "--key", os.path.join(self.folder, callsign + ".key"),
             "--microphone-port", str(microphone.getsockname()[1]),
             "--bluetooth-script", self.bluetooth_script,
             "--sound-card-script", self.sound_card_script,
             "--cat-port", str(free_port(socket.SOCK_STREAM)),
             "--voip-port", str(free_port(socket.SOCK_DGRAM))],
            stdout=subprocess.DEVNULL)
        self.others.append((daemon, radio, microphone))
        radio.set("VOIP", "ONLINE")
        self.wait_for(lambda: radio.controls.get("REMOTE_STATUS", "") == NOT_ADDED,
                      "the second radio did not say it was not added")
        ok, told = self.service.radio_add(account, "home", radio.controls["RADIO_KEY"])
        self.assertTrue(ok, told)
        return radio, microphone

    def test_two_radios_on_voip_hear_each_other_and_the_first_holds_the_channel(self):
        self.switch_on_and_add()
        self.radio.set("VOIP", "ONLINE")
        self.wait_for(lambda: self.radio.controls.get("VOIP_STATUS") == "7074 KHZ, 1 HERE",
                      "the first radio was not put in its channel")
        other, heard = self.another_radio("VU3XYZ", "xyz@example.org")
        self.wait_for(lambda: other.controls.get("VOIP_STATUS") == "7074 KHZ, 2 HERE"
                      and self.radio.controls.get("VOIP_STATUS") == "7074 KHZ, 2 HERE",
                      "the two radios were not both told of the channel")
        # the owner of the first speaks: the radio program's packets to the
        # gateway, a steady level, twenty milliseconds apart
        speaking = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        heard.settimeout(5)
        got = []
        for number in range(40):
            speaking.sendto(whole_packet(number, 3000), ("127.0.0.1", self.voip_port))
            time.sleep(0.02)
        while len(got) < 30:
            got.append(heard.recv(4000))
        for packet in got:
            # to be heard, not transmitted, and whose: the account's callsign
            self.assertEqual(len(packet), 11 + 12 + 1920)
            self.assertEqual(packet[0], 3, "not labelled as speech to be heard")
            self.assertEqual(packet[1:11].rstrip(b"\0"), b"VU2ESE")
        for sample in struct.unpack(">480h", got[-1][-960:]):
            self.assertLess(abs(sample - 3000), 3000 * 0.06)
        # the first holds the channel: the second, speaking now, is told
        # who has it, and its speech does not reach the first
        self.microphone.settimeout(0.5)
        other_port = None
        for daemon, radio, microphone in self.others:
            other_port = int(daemon.args[daemon.args.index("--voip-port") + 1])
        for number in range(40, 60):
            speaking.sendto(whole_packet(number, 3000), ("127.0.0.1", self.voip_port))
            speaking.sendto(whole_packet(number, 5000), ("127.0.0.1", other_port))
            time.sleep(0.02)
        self.wait_for(lambda: other.controls.get("VOIP_STATUS") == "BUSY VU2ESE",
                      "the second was not told the channel was held")
        with self.assertRaises(socket.timeout):
            self.microphone.recv(4000)
        speaking.close()
        # the second tunes away, to another channel
        other.set("FREQ", "7076000")
        self.wait_for(lambda: self.radio.controls.get("VOIP_STATUS") == "7074 KHZ, 1 HERE",
                      "the first was not told the second had gone")
        # VOIP offline: the first leaves the service's channels
        self.radio.set("VOIP", "OFFLINE")
        self.wait_for(lambda: self.radio.controls.get("VOIP_STATUS") == "OFFLINE",
                      "VOIP_STATUS did not say OFFLINE")
        self.wait_for(lambda: len(self.remote.voip.streams) == 1,
                      "the service kept the first radio's stream")

    def test_a_new_cookie_made_at_the_radio_and_typed_on_the_website(self):
        before = self.switch_on_and_add()
        self.radio.set("NEW_COOKIE", "MAKING")
        self.wait_for(lambda: self.radio.controls.get("RADIO_KEY")
                      != accounts.show_secret(before["cookie"])
                      and self.radio.controls.get("NEW_COOKIE") == "-",
                      "no new key was shown")
        shown = self.radio.controls["RADIO_KEY"]
        self.assertNotEqual(shown, accounts.show_secret(before["cookie"]))
        with open(self.key_file) as file:
            self.assertEqual(accounts.show_secret(file.read().strip()), shown)
        ok, told = self.service.radio_new_cookie(self.account, before["id"], shown)
        self.assertTrue(ok, told)
        self.wait_for(lambda: self.status() == "ONLINE vu2ese-home",
                      "the radio did not come back on line with its new cookie")
        after = self.service.radio_called("vu2ese-home")
        self.assertEqual(after["client_key"], before["client_key"])


if __name__ == "__main__":
    unittest.main(verbosity=1, warnings="ignore")
