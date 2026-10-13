// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rtp_microphone.c — speech to transmit, taken in on the radio itself as
// RTP, in the microphone's place.
//
// WHAT IT IS FOR
//
// rtp_audio.c sends what the radio plays out on the radio, for whoever
// listens from afar. This is the other way: whoever works the radio from
// afar speaks into a microphone that is not the radio's, and the speech
// has to reach the transmitter. The author's design: transmit audio cannot
// be multicast, it comes back to a listening socket. A program on the
// radio that has speech to transmit sends it here; zbitx_remote does, with
// what the owner says into a telephone on the other side of the world.
//
// WHAT IS TAKEN
//
// The sound as rtp_audio.c sends it: RTP with 16-bit samples, the more
// significant byte first, 48000 a second, 960 to a packet. Before the RTP
// packet is a label of eleven bytes, the author's design: one that says
// what kind of sound it is, and ten for the callsign of whoever is
// speaking, with noughts after it. The kinds are in rtp_microphone.h:
// speech from the owner working the radio from afar, and sound from
// another program on the radio, a digital mode's, both of which are to be
// transmitted; and someone speaking over the internet, which is to be
// heard and never transmitted, and is not taken here yet. It comes to UDP
// port 5006 on the loopback address and no other, so that nothing off the
// radio can reach it. A packet of any other size or kind, or with a
// sequence number at or behind the last, is dropped. When none has come
// for a second, the next begins a new stream, whatever its number.
//
// It was first mu-law at 8000 a second with no label, which is how speech
// crosses the internet. The gateway now turns that into this, so that the
// radio program has one form of sound to take, whoever it is from.
//
// WHEN IT TAKES THE MICROPHONE'S PLACE
//
// Only while the radio is sending, and from the first sample that comes
// from afar in a transmission until that transmission ends. Before any has
// come, the radio's own microphone is used, as ever: an operator at the
// radio is not affected by this being here. Once speech from afar is in
// use, a gap in it is sent as silence and not filled from the microphone:
// the link stalls, and the room the radio stands in is not to go on the
// air in the pauses. Whatever came while the radio was receiving is thrown
// away; it is stale by the time the radio sends.
//
// HOW MUCH IS KEPT IN HAND
//
// Packets cross the internet and come unevenly. Speech begins when LEAST,
// a hundred and twenty milliseconds, is waiting, and when it runs out
// there is silence until that much waits again. If more than MOST waits,
// which is what a long stall leaves when it ends, the oldest is let go:
// what is sent is to be near to what is being said. Nothing is done about
// the two clocks, the telephone's and the sound card's, differing; at the
// 200 parts in a million measured between this radio and a laptop, a
// minute's talking moves the queue 12 milliseconds, and each transmission
// begins it afresh.
//
// HOW LOUD
//
// The samples are brought up from 48000 to the sound card's 96000 a
// second, two for one, through a low-pass filter of 32 taps cut at 20 kHz,
// so that the copy of the sound that lies above 24 kHz is not there.
//
// MIC is the gain of the sound card's microphone input, which these
// samples never pass. So the control is told here too, set_gain(), and
// works on them as a plain multiplier, MIC 50 being the level below. That
// level, LEVEL_AT_HALF, was reckoned and not set on the air; the author
// monitored speech sent this way on 2026-10-13 and found it working well. It
// puts speech that peaks at half of mu-law's full scale, which is where a
// telephone's own levelling leaves it, at the peak of 2TONE's two tones,
// which is the drive the transmitter is known to take cleanly.
//
// WHO DOES WHAT
//
// poll() is the thread's that owns the controls, called each time round
// the radio's loop: it reads the socket and puts the samples in a ring.
// fill() is the audio thread's: the core calls it with each block from the
// microphone before the modem has it. The ring has one writer and one
// reader, each writing its own position only, and needs no lock.

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "rtp_audio.h"
#include "rtp_microphone.h"

#define RATE_OUT_HZ 96000
#define INTERPOLATE (RATE_OUT_HZ / RTP_AUDIO_RATE_HZ)
#define TAPS 32
#define TAPS_A_PHASE (TAPS / INTERPOLATE)
#define CUTOFF_HZ 20000.0
#define HAMMING_A 0.54
#define HAMMING_B 0.46
#define RING_SAMPLES 32768
#define LEAST_SAMPLES 5760
#define MOST_SAMPLES 19200
#define QUIET_MS 1000
#define LEVEL_AT_HALF 5250.0
#define HALF_PERCENT 50.0
#define STARTING_PERCENT 40
#define LOOPBACK "127.0.0.1"
#define RTP_VERSION_MASK 0xc0
#define RTP_VERSION_2 0x80
#define RTP_PAYLOAD_MASK 0x7f
#define PACKET_BYTES (RTP_MICROPHONE_LABEL_BYTES + RTP_PACKET_BYTES)
#define NOT_OPEN (-1)

static int in = NOT_OPEN;
static float taps[INTERPOLATE][TAPS_A_PHASE];

static int16_t ring[RING_SAMPLES];
static atomic_uint ring_put;
static atomic_uint ring_taken;
static atomic_int gain_percent = STARTING_PERCENT;
static atomic_int packets_taken;

// the poll's
static int is_stream_known;
static uint16_t last_sequence;
static int64_t last_packet_ms;

// the audio thread's
static float history[TAPS_A_PHASE];
static int history_at;
static int phase;
static int is_in_use;
static int is_speaking;

static int64_t now_ms(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int rtp_microphone_open(void) {
	struct sockaddr_in here;
	double centre = (TAPS - 1) / 2.0;
	double wide = 2.0 * CUTOFF_HZ / RATE_OUT_HZ;
	double x;
	double sum[INTERPOLATE];
	double tap;
	int i;

	// each of the two samples made of one has its own sixteen taps, and
	// each sixteen add up to one, so that a steady level stays what it was
	memset(sum, 0, sizeof(sum));
	for (i = 0; i < TAPS; i++) {
		x = i - centre;
		tap = sin(M_PI * wide * x) / (M_PI * x)
			* (HAMMING_A - HAMMING_B * cos(2.0 * M_PI * i / (TAPS - 1)));
		taps[i % INTERPOLATE][i / INTERPOLATE] = (float)tap;
		sum[i % INTERPOLATE] += tap;
	}
	for (i = 0; i < TAPS; i++)
		taps[i % INTERPOLATE][i / INTERPOLATE] /= (float)sum[i % INTERPOLATE];

	in = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (in < 0)
		return -1;
	memset(&here, 0, sizeof(here));
	here.sin_family = AF_INET;
	here.sin_port = htons(RTP_MICROPHONE_PORT);
	inet_pton(AF_INET, LOOPBACK, &here.sin_addr);
	if (bind(in, (struct sockaddr *)&here, sizeof(here)) < 0) {
		close(in);
		in = NOT_OPEN;
		return -1;
	}
	return 0;
}

void rtp_microphone_close(void) {
	if (in != NOT_OPEN)
		close(in);
	in = NOT_OPEN;
}

void rtp_microphone_set_gain(int percent) {
	atomic_store(&gain_percent, percent);
}

// how many packets have been taken since this was last asked; for "report"
int rtp_microphone_packets(void) {
	return atomic_exchange(&packets_taken, 0);
}

// every packet that has come is put in the ring
void rtp_microphone_poll(void) {
	uint8_t packet[PACKET_BYTES + 1];
	const uint8_t *rtp = packet + RTP_MICROPHONE_LABEL_BYTES;
	const uint8_t *samples = rtp + RTP_HEADER_BYTES;
	unsigned int put;
	uint16_t sequence;
	int64_t now;
	ssize_t length;
	int i;

	if (in == NOT_OPEN)
		return;
	for (;;) {
		length = recv(in, packet, sizeof(packet), 0);
		if (length < 0)
			return;
		if (length != PACKET_BYTES || (rtp[0] & RTP_VERSION_MASK) != RTP_VERSION_2
				|| (rtp[1] & RTP_PAYLOAD_MASK) != RTP_AUDIO_PAYLOAD_TYPE)
			continue;
		// only what is to be transmitted is taken here, so far
		if (packet[0] != RTP_MICROPHONE_TO_TRANSMIT && packet[0] != RTP_MICROPHONE_FROM_A_PROGRAM)
			continue;
		sequence = (uint16_t)(rtp[2] << 8 | rtp[3]);
		now = now_ms();
		if (is_stream_known && now - last_packet_ms < QUIET_MS
				&& (int16_t)(sequence - last_sequence) <= 0)
			continue;
		is_stream_known = 1;
		last_sequence = sequence;
		last_packet_ms = now;
		put = atomic_load(&ring_put);
		// no room: the audio thread is not taking, and this is let go
		if (put - atomic_load(&ring_taken) > RING_SAMPLES - RTP_AUDIO_FRAME_SAMPLES)
			continue;
		for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++)
			ring[(put + i) % RING_SAMPLES] = (int16_t)(samples[2 * i] << 8 | samples[2 * i + 1]);
		atomic_store(&ring_put, put + RTP_AUDIO_FRAME_SAMPLES);
		atomic_fetch_add(&packets_taken, 1);
	}
}

// the audio thread's: a block from the microphone, 96000 samples a second,
// in which the speech from afar is put if there is any; is_sending says
// whether the radio is transmitting
void rtp_microphone_fill(int32_t *microphone, int frames, int is_sending) {
	unsigned int taken = atomic_load(&ring_taken);
	unsigned int waiting = atomic_load(&ring_put) - taken;
	float level = (float)(LEVEL_AT_HALF * atomic_load(&gain_percent) / HALF_PERCENT);
	float sum;
	int i;
	int tap;
	int at;

	if (!is_sending) {
		atomic_store(&ring_taken, taken + waiting);
		is_in_use = 0;
		is_speaking = 0;
		return;
	}
	if (!is_in_use) {
		if (waiting == 0)
			return;
		is_in_use = 1;
		memset(history, 0, sizeof(history));
		phase = 0;
	}
	if (waiting > MOST_SAMPLES) {
		taken += waiting - LEAST_SAMPLES;
		waiting = LEAST_SAMPLES;
	}
	for (i = 0; i < frames; i++) {
		if (phase == 0) {
			if (!is_speaking && waiting >= LEAST_SAMPLES)
				is_speaking = 1;
			if (is_speaking && waiting == 0)
				is_speaking = 0;
			history_at++;
			if (history_at == TAPS_A_PHASE)
				history_at = 0;
			history[history_at] = 0;
			if (is_speaking) {
				history[history_at] = ring[taken % RING_SAMPLES];
				taken++;
				waiting--;
			}
		}
		// taps[phase][j] multiplies the sample j back from the newest
		sum = 0;
		at = history_at;
		for (tap = 0; tap < TAPS_A_PHASE; tap++) {
			sum += taps[phase][tap] * history[at];
			at--;
			if (at < 0)
				at = TAPS_A_PHASE - 1;
		}
		microphone[i] = (int32_t)(sum * level);
		phase++;
		if (phase == INTERPOLATE)
			phase = 0;
	}
	atomic_store(&ring_taken, taken);
}
