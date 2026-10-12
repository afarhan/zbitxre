// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_rtp_audio.c — the radio's sound sent out on the radio as RTP.
//
// It needs no radio. rtp_audio.c is given tones as the core gives it the
// speaker's samples, and this program listens to the multicast group as any
// program on the radio would. What arrives is checked for being RTP of
// payload type 0, twenty milliseconds to a packet with nothing lost or out
// of order, and, with the mu-law undone, for being the tone that was put
// in, at its level and its pitch. A tone above half the outgoing sample
// rate is put in as well, to see that it does not come out folded down.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "rtp_audio.h"

#define BLOCK_FRAMES 1024
#define RATE_IN_HZ 96000.0
#define SECOND_OF_BLOCKS 94
#define PACKETS_MAX 64
#define SETTLED_PACKETS 5
#define QUARTER_SCALE 536870912.0
#define QUARTER_SCALE_16 8192.0
#define ULAW_BIAS 0x84

static int checks_failed;
static int listener;
static uint8_t packets[PACKETS_MAX][RTP_PACKET_BYTES];
static int packet_count;
static double phase;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static int linear_of(uint8_t ulaw) {
	int value = ~ulaw & 0xff;
	int size = (((value & 0x0f) << 3) + ULAW_BIAS) << ((value & 0x70) >> 4);

	if (value & 0x80)
		return ULAW_BIAS - size;
	return size - ULAW_BIAS;
}

static int listen_to_the_group(void) {
	struct sockaddr_in any;
	struct ip_mreq membership;
	struct timeval wait = { 0, 200000 };
	int one = 1;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&any, 0, sizeof(any));
	any.sin_family = AF_INET;
	any.sin_port = htons(RTP_AUDIO_PORT);
	if (bind(fd, (struct sockaddr *)&any, sizeof(any)) < 0)
		return -1;
	inet_pton(AF_INET, RTP_AUDIO_GROUP, &membership.imr_multiaddr);
	inet_pton(AF_INET, "127.0.0.1", &membership.imr_interface);
	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
	return fd;
}

// a second of a tone goes in as the speaker's blocks, and what comes out
// on the group is gathered
static void play_a_tone(double hz) {
	static int32_t block[BLOCK_FRAMES];
	ssize_t got;
	int blocks;
	int i;

	packet_count = 0;
	for (blocks = 0; blocks < SECOND_OF_BLOCKS; blocks++) {
		for (i = 0; i < BLOCK_FRAMES; i++) {
			block[i] = (int32_t)(QUARTER_SCALE * sin(phase));
			phase += 2.0 * M_PI * hz / RATE_IN_HZ;
		}
		rtp_audio_take(block, BLOCK_FRAMES);
		rtp_audio_poll();
	}
	got = recv(listener, packets[0], RTP_PACKET_BYTES, 0);
	while (got == RTP_PACKET_BYTES && packet_count < PACKETS_MAX - 1) {
		packet_count++;
		got = recv(listener, packets[packet_count], RTP_PACKET_BYTES, 0);
	}
}

// the loudest sample and how many times the sound crosses zero going up,
// in the packets after the first few, in which the filter fills
static void measure(int *loudest, double *hz) {
	int crossings = 0;
	int before = 0;
	int samples = 0;
	int now;
	int p;
	int i;

	*loudest = 0;
	for (p = SETTLED_PACKETS; p < packet_count; p++) {
		for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++) {
			now = linear_of(packets[p][RTP_HEADER_BYTES + i]);
			if (abs(now) > *loudest)
				*loudest = abs(now);
			if (before < 0 && now >= 0)
				crossings++;
			before = now;
			samples++;
		}
	}
	*hz = 0;
	if (samples > 0)
		*hz = (double)crossings * RTP_AUDIO_RATE_HZ / samples;
}

int main(void) {
	int loudest;
	double hz;
	int in_order = 1;
	int sequence;
	uint32_t stamp;
	uint32_t stamp_before = 0;
	int p;

	printf("rtp audio:\n");
	listener = listen_to_the_group();
	expect(listener >= 0, "a program on this computer can listen to the group on loopback");
	expect(rtp_audio_open() == 0, "the sender opens");

	play_a_tone(1000.0);
	expect(packet_count >= 45 && packet_count <= 50,
		"a second of sound is some fifty packets of 172 bytes");
	expect(packets[0][0] == 0x80 && packets[0][1] == 0,
		"each is RTP, version 2, payload type 0");
	sequence = (packets[0][2] << 8) | packets[0][3];
	for (p = 0; p < packet_count; p++) {
		stamp = ((uint32_t)packets[p][4] << 24) | ((uint32_t)packets[p][5] << 16)
			| ((uint32_t)packets[p][6] << 8) | packets[p][7];
		if (((packets[p][2] << 8) | packets[p][3]) != ((sequence + p) & 0xffff))
			in_order = 0;
		if (p > 0 && stamp - stamp_before != RTP_AUDIO_FRAME_SAMPLES)
			in_order = 0;
		stamp_before = stamp;
	}
	expect(in_order, "numbered one after another, their times 160 samples apart");
	measure(&loudest, &hz);
	expect(fabs(hz - 1000.0) < 15.0, "a 1 kHz tone comes out a 1 kHz tone");
	expect(loudest > QUARTER_SCALE_16 * 0.9 && loudest < QUARTER_SCALE_16 * 1.1,
		"at the level it went in at, a quarter of full scale");

	play_a_tone(6000.0);
	measure(&loudest, &hz);
	expect(loudest < QUARTER_SCALE_16 / 100,
		"a 6 kHz tone, which would fold down to 2 kHz, comes out 40 dB down or more");

	play_a_tone(3000.0);
	measure(&loudest, &hz);
	expect(loudest > QUARTER_SCALE_16 * 0.7 && fabs(hz - 3000.0) < 30.0,
		"a 3 kHz tone, the top of the speech, comes through");

	rtp_audio_close();
	if (checks_failed) {
		printf("rtp audio: %d checks FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("rtp audio: all checks passed\n");
	return EXIT_SUCCESS;
}
