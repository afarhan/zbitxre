# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
voip.py — the repeater of my.sbitx.net: speech from one radio relayed to
the others tuned to the same frequency.

WHAT THE AUTHOR ASKED FOR

The dial is the room. A radio whose owner has VOIP online is in the
channel of its dial, the frequency rounded to the nearest kilohertz, and
hears whoever speaks there; its owner speaks by putting the radio in the
mode VOIP and pressing PTT, which then sends to the internet and never
keys the transmitter. The first to speak holds the channel, for a minute
at most. The speaker is named by the callsign of the account the radio
belongs to, written in here and never taken from the radio; how many
radios are in a channel is said to each, as a count, and who they are is
not: "that's the nature of ham radio: you have no idea who is listening".

NOT A TRAFFIC AMPLIFIER

A repeater sends many packets for each it is sent, and a UDP packet's
return address can be forged. The author's words: "as the media repeater
at my.sbitx.net is a traffic amplifier, it is vital that we have some way
to throttle and also authenticate the device/user". So, as he agreed on
15 October 2026:

  - Nothing is sent back to where a packet came from. Speech goes only to
    radios that hold a signed-in connection to remote.py, at the address
    their own tagged packets come from. A packet whose tag is wrong is
    dropped and answered by nothing at all, so a forged one gains nothing.
  - A radio may send PACKETS_A_SECOND, the rate of twenty milliseconds of
    speech, with BURST over it for the internet's unevenness; more is
    dropped.
  - A channel has one speaker at a time, for ONE_OVER_S at most, and the
    same radio may not take it again for AGAIN_AFTER_S after its over, so
    that another can answer. A channel holds CHANNEL_MOST radios.
  - All channels together are sent no more than the service's ceiling, a
    setting, voip_packets_a_second; an over that would take the service
    past it is refused.
  - Every over is said, with its callsign, channel, length and how many
    heard it. Someone who abuses the rest is suspended by customer
    service, which stops all of an account's radios at once.

THE PACKET

The same, 196 bytes, each way:

     0   8   the stream's id
     8   1   kind: 0 a radio saying where it is tuned, 3 speech
     9   1   codec: 0 mu-law, 8000 a second, 20 ms
    10  10   callsign, with noughts after it; from here, the speaker's
    20   4   the dial's frequency in Hz, the more significant byte first
    24   4   the packet's number, likewise
    28 160   sound
   188   8   tag

The tag is the first eight bytes of HMAC-SHA256 of all before it. A
radio's stream has two keys, made from the stretched cookie and a nonce
that remote.py sends it, as the radio's sound's are, and never sent: one
for what the radio sends, of the words "zbitx-voip-up-1", and one for what
is sent to it, "zbitx-voip-down-1", so that a radio's own packet, copied
on its way, cannot be sent back to it as another's. A packet numbered at
or behind the last taken is dropped.

A radio sends kind 0 every quarter of a minute and when its dial moves:
that keeps it in its channel, keeps its home router's way in open, and
says where it is tuned. One not heard from for GONE_AFTER_S is let go.
The kinds 1, 2 and 4 are the radio's own, rtp_microphone.h has them, and
are not taken here.

This file does the deciding and knows nothing of sockets: remote.py gives
it each packet and the times, and it says what to send where, by the two
functions it is made with. test_voip.py tries it with a clock of its own.
"""

import hashlib
import hmac
import struct

STREAM_ID_BYTES = 8
CALLSIGN_BYTES = 10
SOUND_BYTES = 160
TAG_BYTES = 8
HEAD = ">BB10sII"
HEAD_BYTES = struct.calcsize(HEAD)
PACKET_BYTES = STREAM_ID_BYTES + HEAD_BYTES + SOUND_BYTES + TAG_BYTES
KIND_TUNED = 0
KIND_SPEECH = 3
CODEC_MULAW = 0
PACKETS_A_SECOND = 50
BURST = 25
ONE_OVER_S = 60
AGAIN_AFTER_S = 3
# a gap this long in a radio's speech ends its over
OVER_GAP_S = 1.0
CHANNEL_MOST = 50
GONE_AFTER_S = 45
AHEAD_MOST = 3000
# told a radio of its speech being dropped no oftener than this
BUSY_EVERY_S = 1.0
HZ_A_CHANNEL = 1000
# a packet that comes this long after the one before it is counted late
LATE_S = 0.1


def channel_of(hz):
    """the dial rounded to the nearest kilohertz, in kilohertz"""
    return (hz + HZ_A_CHANNEL // 2) // HZ_A_CHANNEL


def key_for(words, cookie_key, nonce):
    return hmac.new(cookie_key.encode(), (words + " " + nonce).encode(),
                    hashlib.sha256).hexdigest().encode()


def tag_of(key, body):
    return hmac.new(key, body, hashlib.sha256).digest()[:TAG_BYTES]


def packet(stream_id, key, kind, callsign, hz, number, sound=bytes(SOUND_BYTES)):
    body = stream_id + struct.pack(HEAD, kind, CODEC_MULAW,
                                   callsign.encode()[:CALLSIGN_BYTES], hz, number) + sound
    return body + tag_of(key, body)


class Stream:
    """a radio that has VOIP online: its keys, its account's callsign, the
    address its packets come from, its channel, and how it has been
    speaking"""

    def __init__(self, radio, stream_id, callsign, cookie_key, nonce):
        self.radio = radio
        self.id = stream_id
        self.callsign = callsign
        self.up_key = key_for("zbitx-voip-up-1", cookie_key, nonce)
        self.down_key = key_for("zbitx-voip-down-1", cookie_key, nonce)
        self.address = None
        self.heard_at = None
        self.last = None
        self.channel = None
        self.sent = 0
        self.tokens = BURST
        self.tokens_at = None
        # this radio's run of speech: when its PTT went down, its last
        # packet, whether it was cut at a minute, and its last over's end
        self.run_began = None
        self.last_speech = None
        self.is_cut = False
        self.over_ended = None
        self.busy_told_at = None


class Floor:
    def __init__(self, stream, now):
        self.stream = stream
        self.began = now
        self.heard_by = 0
        # how the speaker's packets came: how many, and the gaps between
        # them, which the log says at the end of the over. The author heard
        # the zbitx's speech pumping at the sbitx on 16 October 2026, and
        # the other way not; this says whether the packets came late
        self.packets = 0
        self.last_at = now
        self.longest_gap = 0.0
        self.late = 0


class Voip:
    def __init__(self, send, tell, ceiling, say=lambda text: None):
        """send(packet, address) puts a packet on the wire; tell(radio,
        line) says a line on a radio's connection; ceiling is the most
        packets a second sent by all channels together"""
        self.send = send
        self.tell = tell
        self.say = say
        self.ceiling = ceiling
        self.streams = {}    # a stream's id -> its Stream
        self.stream_of = {}  # a radio's id -> its stream's id
        self.channels = {}   # a channel -> the streams in it
        self.floors = {}     # a channel -> its Floor, while someone speaks
        self.usage = dict.fromkeys(("in", "out", "dropped", "overs"), 0)

    # a radio's VOIP going online and offline, from remote.py

    def open(self, radio, stream_id, callsign, cookie_key, nonce):
        self.close(radio)
        stream = Stream(radio, stream_id, callsign, cookie_key, nonce)
        self.streams[stream_id] = stream
        self.stream_of[radio] = stream_id
        return stream

    def close(self, radio):
        stream = self.streams.pop(self.stream_of.pop(radio, None), None)
        if stream is not None:
            self.leave(stream, None)

    # channels

    def join(self, stream, channel, now):
        if stream.channel == channel:
            return True
        members = self.channels.get(channel, set())
        if len(members) >= CHANNEL_MOST:
            self.leave(stream, now)
            self.busy(stream, "channel full", now)
            return False
        self.leave(stream, now)
        stream.channel = channel
        self.channels.setdefault(channel, set()).add(stream)
        self.count(channel)
        return True

    def leave(self, stream, now):
        channel = stream.channel
        if channel is None:
            return
        floor = self.floors.get(channel)
        if floor is not None and floor.stream is stream:
            self.end_over(channel, stream.last_speech if now is None else now)
        stream.channel = None
        members = self.channels.get(channel, set())
        members.discard(stream)
        if members:
            self.count(channel)
        else:
            self.channels.pop(channel, None)

    def count(self, channel):
        members = self.channels.get(channel, ())
        for member in members:
            self.tell(member.radio, "VOIP ROOM %d %d" % (channel, len(members)))

    # a packet

    def take(self, data, address, now):
        """a packet of PACKET_BYTES from the UDP port: dropped, without a
        word to where it came from, unless it is a stream's, its tag is
        right and its number is ahead"""
        stream = self.streams.get(data[:STREAM_ID_BYTES])
        if stream is None:
            return
        if not hmac.compare_digest(tag_of(stream.up_key, data[:-TAG_BYTES]),
                                   data[-TAG_BYTES:]):
            return
        kind, codec, _, hz, number = struct.unpack(
            HEAD, data[STREAM_ID_BYTES:STREAM_ID_BYTES + HEAD_BYTES])
        if stream.last is not None and not 0 < (number - stream.last) % (1 << 32) < AHEAD_MOST:
            return
        if kind not in (KIND_TUNED, KIND_SPEECH) or codec != CODEC_MULAW:
            return
        stream.last = number
        if not self.has_tokens(stream, now):
            self.usage["dropped"] += 1
            return
        stream.address = address
        stream.heard_at = now
        if not self.join(stream, channel_of(hz), now):
            return
        if kind == KIND_SPEECH:
            self.usage["in"] += 1
            self.speech(stream, hz, data[STREAM_ID_BYTES + HEAD_BYTES:-TAG_BYTES], now)

    def has_tokens(self, stream, now):
        if stream.tokens_at is not None:
            stream.tokens = min(BURST, stream.tokens
                                + (now - stream.tokens_at) * PACKETS_A_SECOND)
        stream.tokens_at = now
        if stream.tokens < 1:
            return False
        stream.tokens -= 1
        return True

    def speech(self, stream, hz, sound, now):
        channel = stream.channel
        # an over that has had its gap is ended first, the speaker's own
        # among them: one who lets go of PTT and presses it again is
        # beginning another over, and waits as anyone does
        floor = self.floors.get(channel)
        if floor is not None and self.is_over(floor, now):
            self.end_over(channel, floor.stream.last_speech)
            floor = None
        if stream.last_speech is None or now - stream.last_speech > OVER_GAP_S:
            stream.run_began = now
            stream.is_cut = False
        stream.last_speech = now
        if floor is None:
            refusal = self.refusal(stream, channel, now)
            if refusal:
                self.drop(stream, refusal, now)
                return
            floor = self.floors[channel] = Floor(stream, now)
        elif floor.stream is not stream:
            self.drop(stream, floor.stream.callsign, now)
            return
        elif now - floor.began >= ONE_OVER_S:
            stream.is_cut = True
            self.end_over(channel, now)
            self.drop(stream, "one minute", now)
            return
        gap = now - floor.last_at
        floor.last_at = now
        floor.packets += 1
        if floor.packets > 1:
            floor.longest_gap = max(floor.longest_gap, gap)
            if gap > LATE_S:
                floor.late += 1
        listeners = [member for member in self.channels[channel]
                     if member is not stream and member.address is not None]
        floor.heard_by = max(floor.heard_by, len(listeners))
        for member in listeners:
            member.sent = (member.sent + 1) % (1 << 32)
            self.send(packet(member.id, member.down_key, KIND_SPEECH, stream.callsign, hz,
                             member.sent, sound), member.address)
            self.usage["out"] += 1

    def refusal(self, stream, channel, now):
        """why a radio may not take a free channel, or '' if it may"""
        if stream.is_cut:
            return "one minute"
        if stream.over_ended is not None and now - stream.over_ended < AGAIN_AFTER_S:
            return "wait"
        sending = sum(len(self.channels.get(each, ())) - 1 for each in self.floors)
        if (sending + len(self.channels[channel]) - 1) * PACKETS_A_SECOND > self.ceiling:
            return "service full"
        return ""

    def drop(self, stream, why, now):
        self.usage["dropped"] += 1
        self.busy(stream, why, now)

    def busy(self, stream, why, now):
        if (stream.busy_told_at is None or now is None
                or now - stream.busy_told_at >= BUSY_EVERY_S):
            stream.busy_told_at = now
            self.tell(stream.radio, "VOIP BUSY %s" % why)

    # overs

    @staticmethod
    def is_over(floor, now):
        return now - floor.stream.last_speech > OVER_GAP_S

    def end_over(self, channel, ended):
        floor = self.floors.pop(channel, None)
        if floor is None:
            return
        floor.stream.over_ended = ended
        self.usage["overs"] += 1
        seconds = ended - floor.began
        self.say("voip: %s spoke on %d kHz for %d s, heard by %d; %d packets of %d, "
                 "%d more than %d ms after the one before, the longest gap %d ms" % (
                     floor.stream.callsign, channel, round(seconds), floor.heard_by,
                     floor.packets, round(seconds * PACKETS_A_SECOND) + 1, floor.late,
                     round(LATE_S * 1000), round(floor.longest_gap * 1000)))

    def sweep(self, now):
        """each second or so: overs that have ended are ended, and radios
        not heard from are let go"""
        for channel, floor in list(self.floors.items()):
            if self.is_over(floor, now):
                self.end_over(channel, floor.stream.last_speech)
        for stream in list(self.streams.values()):
            if (stream.channel is not None and stream.heard_at is not None
                    and now - stream.heard_at > GONE_AFTER_S):
                self.leave(stream, now)
