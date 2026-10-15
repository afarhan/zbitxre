#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
test_voip.py — the repeater's deciding, voip.py, with a clock of its own.

    python3 accounts/test_voip.py

No sockets: what the repeater would send, and what it would tell each
radio, is kept here and looked at. remote.py's part, the stream given to a
radio that has VOIP online and the packets it takes on its UDP port, is
tried at the end against the service itself, on this computer.
"""

import asyncio
import os
import socket
import sys
import tempfile
import shutil
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import voip

COOKIE_KEY = "stretched"
STEP = 0.02


class Radio:
    """a radio with VOIP online, as the repeater sees it: what it sends is
    numbered and tagged with its up key"""

    def __init__(self, repeater, number, callsign, address):
        self.number = number
        self.id = bytes([number]) * voip.STREAM_ID_BYTES
        self.address = address
        self.nonce = "%032x" % number
        self.stream = repeater.open(number, self.id, callsign, COOKIE_KEY, self.nonce)
        self.up_key = voip.key_for("zbitx-voip-up-1", COOKIE_KEY, self.nonce)
        self.down_key = voip.key_for("zbitx-voip-down-1", COOKIE_KEY, self.nonce)
        self.said = 100

    def packet(self, kind, hz, sound=bytes(voip.SOUND_BYTES), callsign="NOBODY"):
        self.said += 1
        return voip.packet(self.id, self.up_key, kind, callsign, hz, self.said, sound)


class VoipTest(unittest.TestCase):
    def setUp(self):
        self.sent = []   # (packet, address)
        self.told = []   # (radio, line)
        self.said = []
        self.now = 1000.0
        self.repeater = voip.Voip(lambda packet, address: self.sent.append((packet, address)),
                                  lambda radio, line: self.told.append((radio, line)),
                                  2000, self.said.append)

    def radio(self, number, callsign, hz=7074000):
        radio = Radio(self.repeater, number, callsign, ("198.51.100.%d" % number, 40000 + number))
        self.tuned(radio, hz)
        return radio

    def take(self, radio, data, address=None):
        self.repeater.take(data, address or radio.address, self.now)

    def tuned(self, radio, hz):
        self.take(radio, radio.packet(voip.KIND_TUNED, hz))

    def speak(self, radio, hz=7074000, packets=1, sound=b"\x55" * voip.SOUND_BYTES):
        for _ in range(packets):
            self.take(radio, radio.packet(voip.KIND_SPEECH, hz, sound))
            self.now += STEP

    def to(self, radio):
        return [packet for packet, address in self.sent if address == radio.address]

    def told_to(self, radio):
        return [line for number, line in self.told if number == radio.number]

    def wait(self, seconds):
        self.now += seconds
        self.repeater.sweep(self.now)

    def test_speech_reaches_the_others_in_the_channel_with_the_accounts_callsign(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ", 7074400)
        c = self.radio(3, "VU2ABC", 7074600)
        self.speak(a, packets=3)
        self.assertEqual(self.to(a), [])
        self.assertEqual(self.to(c), [])
        got = self.to(b)
        self.assertEqual(len(got), 3)
        kind, codec, callsign, hz, number = voip.struct.unpack(
            voip.HEAD, got[0][voip.STREAM_ID_BYTES:voip.STREAM_ID_BYTES + voip.HEAD_BYTES])
        self.assertEqual((kind, codec, callsign.rstrip(b"\0"), hz), (3, 0, b"VU2ESE", 7074000))
        self.assertEqual(got[0][:voip.STREAM_ID_BYTES], b.id)
        self.assertEqual(got[0][-voip.TAG_BYTES:], voip.tag_of(b.down_key, got[0][:-voip.TAG_BYTES]))
        self.assertEqual(got[0][voip.STREAM_ID_BYTES + voip.HEAD_BYTES:-voip.TAG_BYTES],
                         b"\x55" * voip.SOUND_BYTES)
        self.assertEqual([voip.struct.unpack(">I", each[24:28])[0] for each in got], [1, 2, 3])

    def test_the_callsign_a_radio_writes_is_not_believed(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        self.take(a, a.packet(voip.KIND_SPEECH, 7074000, callsign="W1AW"))
        self.assertEqual(self.to(b)[0][10:16], b"VU2ESE")

    def test_the_room_is_a_count_told_to_each(self):
        a = self.radio(1, "VU2ESE")
        self.assertEqual(self.told_to(a)[-1], "VOIP ROOM 7074 1")
        b = self.radio(2, "VU3XYZ")
        self.assertEqual(self.told_to(a)[-1], "VOIP ROOM 7074 2")
        self.assertEqual(self.told_to(b)[-1], "VOIP ROOM 7074 2")
        self.tuned(b, 14074000)
        self.assertEqual(self.told_to(a)[-1], "VOIP ROOM 7074 1")
        self.assertEqual(self.told_to(b)[-1], "VOIP ROOM 14074 1")
        self.repeater.close(1)
        self.assertNotIn(7074, self.repeater.channels)

    def test_a_forged_packet_is_answered_by_nothing(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        victim = ("203.0.113.9", 53)
        sent_before, told_before = len(self.sent), len(self.told)
        # a wrong tag, a stream that is not one, a short packet, another's key
        bad = bytearray(a.packet(voip.KIND_SPEECH, 7074000))
        bad[-1] ^= 1
        self.repeater.take(bytes(bad), victim, self.now)
        self.repeater.take(b"\x09" * 8 + bytes(bad[8:]), victim, self.now)
        self.repeater.take(bytes(bad[:-1]), victim, self.now)
        self.repeater.take(voip.packet(a.id, b.up_key, 3, "", 7074000, 999), victim, self.now)
        self.assertEqual(len(self.sent), sent_before)
        self.assertEqual(len(self.told), told_before)
        self.assertEqual(a.stream.address, a.address)
        self.assertNotIn(victim, [address for _, address in self.sent])

    def test_a_packet_sent_again_is_dropped(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        first = a.packet(voip.KIND_SPEECH, 7074000)
        self.take(a, first)
        self.take(a, first, ("203.0.113.9", 53))
        self.assertEqual(len(self.to(b)), 1)
        self.assertEqual(a.stream.address, a.address)

    def test_a_radio_sending_too_fast_is_held_to_fifty_a_second(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        for _ in range(500):    # five hundred in a second
            self.take(a, a.packet(voip.KIND_SPEECH, 7074000))
            self.now += 0.002
        self.assertLessEqual(len(self.to(b)), voip.PACKETS_A_SECOND + voip.BURST)
        self.assertGreater(self.repeater.usage["dropped"], 400)

    def test_steady_speech_with_jitter_is_not_dropped(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        for at in range(500):
            # ten seconds, in clumps of five as the internet brings them
            self.take(a, a.packet(voip.KIND_SPEECH, 7074000))
            self.now += 0.1 if at % 5 == 4 else 0.0
        self.assertEqual(len(self.to(b)), 500)

    def test_the_first_to_speak_holds_the_channel_and_the_other_is_told_busy(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        self.speak(a, packets=5)
        self.speak(b, packets=5)
        self.assertEqual(len(self.to(b)), 5)
        self.assertEqual(self.to(a), [])
        self.assertIn("VOIP BUSY VU2ESE", self.told_to(b))
        self.assertEqual(self.told_to(b).count("VOIP BUSY VU2ESE"), 1)

    def test_when_the_over_ends_another_may_answer_and_the_speaker_waits(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        self.speak(a, packets=5)
        self.wait(1.5)
        self.assertEqual(len(self.said), 1)
        self.assertIn("VU2ESE spoke on 7074 kHz", self.said[0])
        self.assertIn("heard by 1", self.said[0])
        self.speak(a, packets=2)
        self.assertEqual(self.to(b), self.to(b)[:5])
        self.assertIn("VOIP BUSY wait", self.told_to(a))
        self.wait(2)
        self.speak(b, packets=3)
        self.assertEqual(len(self.to(a)), 3)

    def test_an_over_is_cut_at_a_minute(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        self.speak(a, packets=int(70 / STEP))
        self.assertLessEqual(len(self.to(b)), int(60 / STEP) + 1)
        self.assertGreater(len(self.to(b)), int(59 / STEP))
        self.assertIn("VOIP BUSY one minute", self.told_to(a))
        # the channel is free for another, though a holds PTT still
        self.speak(b, packets=2)
        self.assertEqual(len(self.to(a)), 2)

    def test_a_full_channel_takes_no_more(self):
        for number in range(1, voip.CHANNEL_MOST + 1):
            self.radio(number, "VU2A%02d" % number)
        late = self.radio(99, "VU3LATE")
        self.assertIn("VOIP BUSY channel full", self.told_to(late))
        self.assertNotIn(late.stream, self.repeater.channels[7074])

    def test_an_over_that_would_pass_the_ceiling_is_refused(self):
        self.repeater.ceiling = 2 * voip.PACKETS_A_SECOND
        a = self.radio(1, "VU2ESE")
        for number in range(2, 4):
            self.radio(number, "VU2B%02d" % number)
        c = self.radio(10, "VU2C10", 14074000)
        d = self.radio(11, "VU2C11", 14074000)
        self.speak(a, packets=2)
        self.speak(c, 14074000, packets=2)
        self.assertIn("VOIP BUSY service full", self.told_to(c))
        self.wait(2)
        self.speak(c, 14074000, packets=2)
        self.assertEqual(len(self.to(d)), 2)

    def test_a_radio_not_heard_from_is_let_go(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        self.wait(voip.GONE_AFTER_S - 5)
        self.tuned(a, 7074000)
        self.wait(10)
        self.assertEqual(self.repeater.channels[7074], {a.stream})
        self.speak(a)
        self.assertEqual(self.to(b), [])

    def test_a_radio_going_offline_while_speaking_frees_the_channel(self):
        a = self.radio(1, "VU2ESE")
        b = self.radio(2, "VU3XYZ")
        self.speak(a, packets=3)
        self.repeater.close(1)
        self.assertEqual(self.repeater.floors, {})
        self.speak(b)
        self.assertEqual(self.to(b), self.to(b)[:3])


class ServiceTest(unittest.TestCase):
    """remote.py's part: a radio that says ALLOWS voip is given a stream,
    and the packets go by its UDP port"""

    def setUp(self):
        import accounts
        import remote
        self.accounts, self.remote_module = accounts, remote
        self.folder = tempfile.mkdtemp()
        settings = accounts.Settings()
        settings.database = os.path.join(self.folder, "accounts.db")
        settings.decodes = os.path.join(self.folder, "decodes.db")
        settings.registration = "open"
        settings.pow_bits = 0
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
        self.sockets = []

    def tearDown(self):
        for each in self.sockets:
            each.close()

        async def stop():
            self.tcp.close()
            for task in asyncio.all_tasks():
                if task is not asyncio.current_task():
                    task.cancel()

        asyncio.run_coroutine_threadsafe(stop(), self.loop).result(5)
        time.sleep(0.1)
        self.loop.call_soon_threadsafe(self.loop.stop)
        shutil.rmtree(self.folder, ignore_errors=True)

    def add(self, callsign):
        """an account with a radio, its cookie and its stretched key"""
        service = self.remote.service
        with service.store.open() as db:
            db.execute("insert into accounts (callsign, email, password, status, created) "
                       "values (?, ?, 'x', 'active', 0)",
                       (callsign, callsign.lower() + "@example.org"))
            account = db.execute("select * from accounts where callsign = ?",
                                 (callsign,)).fetchone()
        cookie = "abcdefghjkmn"
        ok, told = service.radio_add(account, "home", cookie)
        self.assertTrue(ok, told)
        return cookie

    def online(self, callsign, cookie):
        """a radio's daemon: signed in, VOIP allowed, its stream returned"""
        line = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        self.sockets.append(line)
        file = line.makefile("rw", newline="\n")
        challenge = file.readline().split()[1]
        nonce = "0" * 32
        file.write("AUTH radio %s %s %s\n" % (callsign, nonce, self.remote_module.proof(
            cookie, callsign, "radio", callsign, challenge, nonce)))
        file.write("radio\n")
        file.flush()
        self.assertTrue(file.readline().startswith("WELCOME"))
        self.assertEqual(file.readline().strip(), "OK")
        file.write("ALLOWS voip\n")
        file.flush()
        words = file.readline().split()
        while words[:2] != ["VOIP", "STREAM"]:
            words = file.readline().split()
        key = self.accounts.stretch(cookie, callsign)
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(("127.0.0.1", 0))
        udp.settimeout(2)
        self.sockets.append(udp)
        return {"id": bytes.fromhex(words[2]), "file": file, "udp": udp, "said": 0,
                "up": voip.key_for("zbitx-voip-up-1", key, words[3]),
                "down": voip.key_for("zbitx-voip-down-1", key, words[3])}

    def send(self, radio, kind):
        radio["said"] += 1
        radio["udp"].sendto(voip.packet(radio["id"], radio["up"], kind, "", 7074000,
                                        radio["said"], b"\x42" * voip.SOUND_BYTES),
                            ("127.0.0.1", self.port))

    def test_two_radios_on_the_service_hear_each_other(self):
        a = self.online("VU2ESE", self.add("VU2ESE"))
        b = self.online("VU3XYZ", self.add("VU3XYZ"))
        self.send(a, voip.KIND_TUNED)
        self.send(b, voip.KIND_TUNED)
        time.sleep(0.2)
        self.send(a, voip.KIND_SPEECH)
        heard, _ = b["udp"].recvfrom(1024)
        self.assertEqual(len(heard), voip.PACKET_BYTES)
        self.assertEqual(heard[10:16], b"VU2ESE")
        self.assertEqual(heard[-8:], voip.tag_of(b["down"], heard[:-8]))
        self.assertEqual(heard[28:188], b"\x42" * voip.SOUND_BYTES)

    def test_a_radio_given_a_new_cookie_on_the_website_is_let_go(self):
        self.remote_module.REVOKED_LOOK_S = 0.3
        try:
            a = self.online("VU2ESE", self.add("VU2ESE"))
            radio = self.remote.service.radio_called("vu2ese-home")
            account = {"id": radio["account"], "callsign": "VU2ESE"}
            ok, told = self.remote.service.radio_new_cookie(account, radio["id"], "pqrstuvwxyza")
            self.assertTrue(ok, told)
            a["file"].readline()
            ends = time.time() + 3
            while time.time() < ends and a["file"].readline():
                pass
            self.assertEqual(self.remote.voip.streams, {})
            self.assertNotIn(radio["id"], self.remote.lines)
        finally:
            self.remote_module.REVOKED_LOOK_S = 20

    def test_voip_offline_lets_the_stream_go(self):
        a = self.online("VU2ESE", self.add("VU2ESE"))
        a["file"].write("ALLOWS remote\n")
        a["file"].flush()
        time.sleep(0.3)
        self.assertEqual(self.remote.voip.streams, {})


if __name__ == "__main__":
    unittest.main(verbosity=1, warnings="ignore")
