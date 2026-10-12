// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rtp_audio.c — what the radio plays, sent out on the radio itself as RTP,
// for other programs to hear.
//
// WHAT IT IS FOR
//
// The radio's sound goes to its speaker. Other programs on the radio want
// it too: zbitx_remote, which carries it to the owner listening from afar,
// a virtual sound card for a program that decodes a digital mode, a
// recorder. The author's design: the radio program multicasts its audio
// locally, and whoever wants it listens. The radio program does not know
// who they are or how many, and none of them can hold it or one another
// up.
//
// WHAT IS SENT
//
// RTP, with the payload type 0 of its audio profile (RFC 3551): one channel
// of G.711 mu-law at 8000 samples a second. A packet is twenty
// milliseconds, 160 samples in 160 bytes after the 12 bytes of RTP's
// header, fifty packets a second. It is the commonest form there is for
// speech on a network, and a player that knows RTP plays it with nothing
// said of what it is:
//
//     ffplay rtp://239.255.73.88:5004
//
// It goes to the multicast group 239.255.73.88, port 5004, on the loopback
// interface, with a time to live of 0, so that it is never put on a wire or
// on the air: the author's rule is that the multicast stays on the radio.
// It is always sent. On the radio alone it disturbs nobody, and a program
// that wants it has no one to ask.
//
// WHAT SOUND IT IS
//
// The samples the core gives the sound card for the speaker, taken before
// the sound card's own volume is put on them. So the level of what is sent
// does not follow the radio's volume control, and the author's rule holds:
// someone listening from afar sets their own volume where they are.
// Whatever the speaker would play is sent, the CW sidetone and a monitor of
// the transmission among it.
//
// The speaker's samples come 96000 a second. They are brought down to 8000
// by a low-pass filter, a windowed sinc of 192 taps that is 3.6 kHz wide,
// worked out once for every twelve samples in; without the filter anything
// above 4 kHz would come out folded down among the speech. With half as
// many taps the filter fell away too slowly, and 3 kHz, the top of the
// speech, came out much reduced. They are then
// made louder by STREAM_GAIN, the sound card being given samples far below
// its full scale and its own amplifier making up the rest, and held to 16
// bits.
//
// WHO DOES WHAT
//
// rtp_audio_take() is the audio thread's: the core calls it with each block
// it has made for the speaker. It filters, and puts the 8000 a second into
// a ring. It touches no socket and waits for nothing.
//
// rtp_audio_poll() is the thread's that owns the controls: the radio's loop
// calls it each time round, every two milliseconds or so. It takes whole
// packets' worth out of the ring, makes them mu-law and sends them. If the
// loop is held up, the packets go late and together; RTP's timestamp says
// when each was meant, and it is the listener's business to play them
// evenly. If it is held up for longer than the ring holds, half a second,
// the oldest sound is let go.
//
// The ring has one writer and one reader and its two positions are each
// written by one of them only, so it needs no lock.

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/random.h>
#include <sys/socket.h>

#include "rtp_audio.h"

#define RATE_IN_HZ 96000
#define DECIMATE (RATE_IN_HZ / RTP_AUDIO_RATE_HZ)
#define TAPS 192
#define CUTOFF_HZ 3600.0
#define HAMMING_A 0.54
#define HAMMING_B 0.46
#define RING_SAMPLES 4096
#define STREAM_GAIN 1.0
#define INT32_TO_INT16 65536.0
#define INT16_MOST 32767
#define PERCENT 100
#define LOOPBACK "127.0.0.1"
#define RTP_VERSION_2 0x80
#define RTP_PAYLOAD_PCMU 0
#define ULAW_BIAS 0x84
#define ULAW_CLIP 32635
#define ULAW_SEGMENTS 8
#define NOT_OPEN (-1)

static float taps[TAPS];
static float history[TAPS];
static int history_at;
static int until_output;
static atomic_int is_filter_ready;

static int16_t ring[RING_SAMPLES];
static atomic_uint ring_put;
static atomic_uint ring_taken;
static atomic_int peak;

static int out = NOT_OPEN;
static struct sockaddr_in group;
static uint16_t sequence;
static uint32_t timestamp;
static uint32_t source;

// a sample as G.711 writes it, mu-law: the sign, three bits for which of
// eight segments its size falls in, and four for where in the segment
static uint8_t ulaw_of(int16_t sample) {
	int size = sample;
	int sign = 0;
	int segment = 0;
	int top;

	if (size < 0) {
		size = -size;
		sign = 0x80;
	}
	if (size > ULAW_CLIP)
		size = ULAW_CLIP;
	size += ULAW_BIAS;
	top = size >> 7;
	while (segment < ULAW_SEGMENTS - 1 && top > 1) {
		top >>= 1;
		segment++;
	}
	return (uint8_t)~(sign | (segment << 4) | ((size >> (segment + 3)) & 0x0f));
}

// the audio thread's: a block made for the speaker, 96000 samples a second
void rtp_audio_take(const int32_t *speaker, int frames) {
	unsigned int put = atomic_load(&ring_put);
	float sum;
	int sample;
	int loudest = atomic_load(&peak);
	int i;
	int tap;
	int at;

	if (!atomic_load(&is_filter_ready))
		return;
	for (i = 0; i < frames; i++) {
		history[history_at] = (float)speaker[i];
		history_at++;
		if (history_at == TAPS)
			history_at = 0;
		until_output--;
		if (until_output > 0)
			continue;
		until_output = DECIMATE;
		sum = 0;
		at = history_at;
		for (tap = 0; tap < TAPS; tap++) {
			sum += taps[tap] * history[at];
			at++;
			if (at == TAPS)
				at = 0;
		}
		sample = (int)(sum * STREAM_GAIN / INT32_TO_INT16);
		if (sample > INT16_MOST)
			sample = INT16_MOST;
		if (sample < -INT16_MOST)
			sample = -INT16_MOST;
		if (abs(sample) > loudest)
			loudest = abs(sample);
		ring[put % RING_SAMPLES] = (int16_t)sample;
		put++;
	}
	atomic_store(&peak, loudest);
	atomic_store(&ring_put, put);
}

// the loudest sample sent since this was last asked, as a percentage of
// the loudest there can be; for "report"
int rtp_audio_peak_percent(void) {
	return atomic_exchange(&peak, 0) * PERCENT / INT16_MOST;
}

int rtp_audio_open(void) {
	struct in_addr loopback;
	unsigned char ttl = 0;
	unsigned char loop = 1;
	double centre = (TAPS - 1) / 2.0;
	double wide = 2.0 * CUTOFF_HZ / RATE_IN_HZ;
	double x;
	double sum = 0;
	int i;

	for (i = 0; i < TAPS; i++) {
		x = i - centre;
		taps[i] = (float)(sin(M_PI * wide * x) / (M_PI * x)
			* (HAMMING_A - HAMMING_B * cos(2.0 * M_PI * i / (TAPS - 1))));
		sum += taps[i];
	}
	for (i = 0; i < TAPS; i++)
		taps[i] = (float)(taps[i] / sum);
	until_output = DECIMATE;
	atomic_store(&is_filter_ready, 1);

	out = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (out < 0)
		return -1;
	inet_pton(AF_INET, LOOPBACK, &loopback);
	if (setsockopt(out, IPPROTO_IP, IP_MULTICAST_IF, &loopback, sizeof(loopback)) < 0
			|| setsockopt(out, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0
			|| setsockopt(out, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
		close(out);
		out = NOT_OPEN;
		return -1;
	}
	memset(&group, 0, sizeof(group));
	group.sin_family = AF_INET;
	group.sin_port = htons(RTP_AUDIO_PORT);
	inet_pton(AF_INET, RTP_AUDIO_GROUP, &group.sin_addr);
	if (getrandom(&source, sizeof(source), 0) != sizeof(source))
		source = (uint32_t)getpid();
	if (getrandom(&sequence, sizeof(sequence), 0) != sizeof(sequence))
		sequence = 0;
	return 0;
}

void rtp_audio_close(void) {
	if (out != NOT_OPEN)
		close(out);
	out = NOT_OPEN;
}

// every whole packet's worth of sound in the ring is sent
void rtp_audio_poll(void) {
	uint8_t packet[RTP_PACKET_BYTES];
	unsigned int taken = atomic_load(&ring_taken);
	unsigned int put = atomic_load(&ring_put);
	int i;

	if (put - taken > RING_SAMPLES)
		taken = put - RTP_AUDIO_FRAME_SAMPLES;
	while (put - taken >= RTP_AUDIO_FRAME_SAMPLES) {
		packet[0] = RTP_VERSION_2;
		packet[1] = RTP_PAYLOAD_PCMU;
		packet[2] = (uint8_t)(sequence >> 8);
		packet[3] = (uint8_t)sequence;
		packet[4] = (uint8_t)(timestamp >> 24);
		packet[5] = (uint8_t)(timestamp >> 16);
		packet[6] = (uint8_t)(timestamp >> 8);
		packet[7] = (uint8_t)timestamp;
		packet[8] = (uint8_t)(source >> 24);
		packet[9] = (uint8_t)(source >> 16);
		packet[10] = (uint8_t)(source >> 8);
		packet[11] = (uint8_t)source;
		for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++)
			packet[RTP_HEADER_BYTES + i] = ulaw_of(ring[(taken + i) % RING_SAMPLES]);
		taken += RTP_AUDIO_FRAME_SAMPLES;
		sequence++;
		timestamp += RTP_AUDIO_FRAME_SAMPLES;
		if (out != NOT_OPEN)
			sendto(out, packet, sizeof(packet), 0, (struct sockaddr *)&group, sizeof(group));
	}
	atomic_store(&ring_taken, taken);
}
