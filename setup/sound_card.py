#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
sound_card.py — two sound devices that are not sound cards, for another
program on the radio to work the radio by.

    setup/sound_card.py start     make the two devices and carry the sound
    setup/sound_card.py stop      stop, and take the devices away

WHAT IT IS FOR

WSJT-X, FreeDV, fldigi and their like work a radio through a sound card:
they listen to the receiver on one sound device and send by playing into
another. The radio program has the radio's one real sound card to itself.
So the author's design is that the gateway gives such programs a sound
card that is not one; this makes it, and is run by the gateway when
SOUND_CARD among the radio's CONNECTIONS is turned on.

THE TWO DEVICES

They are made in the desktop's sound server, PipeWire or PulseAudio, with
the server's own pactl, and are there for as long as this runs:

    zBitx_receive    to listen by: the other program's input. It is what
                     the radio's speaker is given, before the volume
                     control, at 48000 samples a second.
    zBitx_transmit   to send by: the other program's output. What is
                     played into it is transmitted, while the radio is
                     sending.

In the other program they are chosen by those names. In WSJT-X: Settings,
Audio, Input zBitx_receive and Output zBitx_transmit.

HOW THE SOUND IS CARRIED

The radio program sends its sound out on the radio as RTP to a multicast
group, and takes sound to transmit on a port of its own; rtp_audio.c and
rtp_microphone.c say how, 16-bit samples at 48000 a second both ways.
This takes the samples out of the one and plays them into zBitx_receive
with the server's pacat, and records zBitx_transmit with its parec and
sends that to the other, labelled as sound from a program.

What is recorded is sent only while there is sound in it. With nothing
playing into zBitx_transmit the server gives noughts, and they are not
sent: the radio program lets sound that comes to it take its microphone's
place, and an operator speaking into the microphone with this switched on
and no other program sending would be sending silence. A program that is
sending is sending tones the whole while, so nothing of its is lost; a
fifth of a second of quiet is let pass before the sending stops, for a
program that pauses.

The other program says when the radio is to send, by CAT, which the
gateway also gives, or by the radio's own PTT. The radio is to be in USB,
as for any digital mode worked through a sound card, and MIC sets how hard
the sound drives it.

WHAT IT SAYS

The last line is "STATUS " and what the gateway is to show: the two
devices' names, or what is wrong.

It needs python3 and the sound server's pactl, pacat and parec, all of
which Raspberry Pi OS with a desktop has. On a radio with no desktop
there is no sound server and this says so.
"""

import os
import signal
import socket
import struct
import subprocess
import sys
import threading
import time

GROUP = "239.255.73.88"
SOUND_PORT = 5004
TRANSMIT_PORT = 5006
RATE = 48000
FRAME_SAMPLES = 960
FRAME_BYTES = FRAME_SAMPLES * 2
RTP_HEADER_BYTES = 12
RTP_PAYLOAD = 96
FROM_A_PROGRAM = 2
CALLSIGN_BYTES = 10
QUIET_FRAMES_SENT = 10
RECEIVE = "zBitx_receive"
TRANSMIT = "zBitx_transmit"
RECEIVE_SINK = "zbitx_receive_sink"
RUNNING = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "zbitx_sound_card.pid")


def pactl(*words):
    return subprocess.run(("pactl",) + words, capture_output=True, text=True)


def modules():
    """the ids of the modules this made, by the names it gave their devices"""
    found = []
    for line in pactl("list", "short", "modules").stdout.splitlines():
        fields = line.split("\t")
        if len(fields) > 2 and ("zbitx_" in fields[2] or "zBitx_" in fields[2]):
            found.append(fields[0])
    return found


def make_devices():
    """the two devices, made if they are not there. zBitx_transmit is a
    sink that plays to nowhere, and what is played into it is recorded
    from its monitor. zBitx_receive is a source, made of the monitor of a
    second such sink that this plays the radio's sound into: a program
    that lists only inputs then has it to choose"""
    if modules():
        return True
    form = "format=s16le rate=%d channels=1 channel_map=mono" % RATE
    made = [
        pactl("load-module", "module-null-sink", "sink_name=" + TRANSMIT, *form.split(),
              "sink_properties=device.description=" + TRANSMIT),
        pactl("load-module", "module-null-sink", "sink_name=" + RECEIVE_SINK, *form.split(),
              "sink_properties=device.description=zBitx_receive_feed"),
        pactl("load-module", "module-remap-source", "source_name=" + RECEIVE,
              "master=" + RECEIVE_SINK + ".monitor", *form.split(),
              "source_properties=device.description=" + RECEIVE),
    ]
    return all(step.returncode == 0 for step in made)


def take_devices_away():
    for module in reversed(modules()):
        pactl("unload-module", module)


def runner():
    """the one that is carrying the sound, or None"""
    try:
        pid = int(open(RUNNING).read())
        os.kill(pid, 0)
        return pid
    except (OSError, ValueError):
        return None


def carry_received(player):
    """the radio's sound, out of its packets and into zBitx_receive"""
    heard = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    heard.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    heard.bind(("", SOUND_PORT))
    heard.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                     socket.inet_aton(GROUP) + socket.inet_aton("127.0.0.1"))
    while True:
        packet = heard.recv(4000)
        player.stdin.write(packet[RTP_HEADER_BYTES:])
        player.stdin.flush()


def carry_to_transmit(recorder):
    """what is played into zBitx_transmit, to the radio program, while
    there is sound in it"""
    out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    label = bytes([FROM_A_PROGRAM]) + bytes(CALLSIGN_BYTES)
    silence = bytes(FRAME_BYTES)
    source = int.from_bytes(os.urandom(4), "big")
    number = 0
    quiet = QUIET_FRAMES_SENT
    while True:
        frame = recorder.stdout.read(FRAME_BYTES)
        if len(frame) < FRAME_BYTES:
            return
        quiet = quiet + 1 if frame == silence else 0
        if quiet > QUIET_FRAMES_SENT:
            continue
        number = (number + 1) % 65536
        header = struct.pack(">BBHII", 0x80, RTP_PAYLOAD, number,
                             (number * FRAME_SAMPLES) % (1 << 32), source)
        out.sendto(label + header + frame, ("127.0.0.1", TRANSMIT_PORT))


def run():
    """carries the sound both ways until it is stopped"""
    with open(RUNNING, "w") as file:
        file.write(str(os.getpid()))
    form = ["--raw", "--format=s16be", "--rate=%d" % RATE, "--channels=1",
            "--client-name=zbitx"]
    player = subprocess.Popen(["pacat", "--device=" + RECEIVE_SINK, "--latency-msec=40"] + form,
                              stdin=subprocess.PIPE)
    recorder = subprocess.Popen(["parec", "--device=" + TRANSMIT + ".monitor",
                                 "--latency-msec=20"] + form, stdout=subprocess.PIPE)

    def stop(*_):
        player.kill()
        recorder.kill()
        try:
            os.remove(RUNNING)
        except OSError:
            pass
        os._exit(0)
    signal.signal(signal.SIGTERM, stop)
    threading.Thread(target=carry_received, args=(player,), daemon=True).start()
    try:
        carry_to_transmit(recorder)
    finally:
        stop()


def main(argv):
    word = argv[0] if argv else "start"
    if word == "run":
        run()
        return
    if pactl("info").returncode != 0:
        print("there is no sound server to make the devices in")
        print("STATUS NO SOUND SERVER")
        return
    if word == "stop":
        pid = runner()
        if pid is not None:
            os.kill(pid, signal.SIGTERM)
            time.sleep(0.3)
        take_devices_away()
        print("STATUS OFF")
        return
    if not make_devices():
        take_devices_away()
        print("the sound server would not make the devices")
        print("STATUS COULD NOT BE MADE")
        return
    if runner() is None:
        subprocess.Popen([sys.executable, os.path.abspath(__file__), "run"],
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    print("in the other program choose %s to listen by and %s to send by" % (RECEIVE, TRANSMIT))
    print("STATUS %s %s" % (RECEIVE, TRANSMIT))


if __name__ == "__main__":
    main(sys.argv[1:])
