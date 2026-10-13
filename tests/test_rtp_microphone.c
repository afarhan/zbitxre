// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_rtp_microphone.c — speech to transmit, taken in on the radio as RTP.
//
// It needs no radio. Packets are sent to rtp_microphone.c's port as
// zbitx_remote sends them, a tone in 16-bit samples at 48000 a second under
// its label, twenty milliseconds of it for
// every twenty that pass in the blocks the test asks to have filled, as
// the core asks with each block from the microphone. Each block is first
// filled with a number that stands for the radio's own microphone, so that
// it is seen whether the block was left alone, filled with the tone, or
// filled with silence.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "rtp_audio.h"
#include "rtp_microphone.h"

#define BLOCK_FRAMES 1024
#define RATE_OUT_HZ 96000.0
#define OWN_MICROPHONE 12345
#define HALF_SCALE 16384.0
// half of mu-law's full scale, at the MIC of 40 the radio starts with:
// 16384 * 5250 * 40 / 50
#define TONE_OUT 68812800.0
#define GATHERED_MOST (96000 * 2)
#define WHOLE_PACKET (RTP_MICROPHONE_LABEL_BYTES + RTP_PACKET_BYTES)

static int checks_failed;
static int sender;
static struct sockaddr_in port;
static uint16_t sequence = 4000;
static double phase;
static double packets_owed;
static int32_t gathered[GATHERED_MOST];
static int gathered_count;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

// one packet of the tone, numbered as given: the label, of what kind it is
// and whose, then RTP with its samples
static void send_packet(double hz, uint16_t number, int kind, int payload, int bytes) {
	uint8_t packet[RTP_MICROPHONE_LABEL_BYTES + RTP_PACKET_BYTES + 8];
	uint8_t *rtp = packet + RTP_MICROPHONE_LABEL_BYTES;
	int sample;
	int i;

	memset(packet, 0, sizeof(packet));
	packet[0] = (uint8_t)kind;
	memcpy(packet + RTP_MICROPHONE_KIND_BYTES, "VU2ESE", 6);
	rtp[0] = 0x80;
	rtp[1] = (uint8_t)payload;
	rtp[2] = (uint8_t)(number >> 8);
	rtp[3] = (uint8_t)number;
	for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++) {
		sample = (int)(HALF_SCALE * sin(phase));
		rtp[RTP_HEADER_BYTES + 2 * i] = (uint8_t)((uint16_t)sample >> 8);
		rtp[RTP_HEADER_BYTES + 2 * i + 1] = (uint8_t)sample;
		phase += 2.0 * M_PI * hz / RTP_AUDIO_RATE_HZ;
	}
	sendto(sender, packet, bytes, 0, (struct sockaddr *)&port, sizeof(port));
	usleep(200);
}

// so many blocks are asked for, as the core asks; with a tone, its packets
// come as fast as the blocks use them
static void run(int blocks, double hz, int is_sending) {
	int32_t block[BLOCK_FRAMES];
	int i;

	for (; blocks > 0; blocks--) {
		if (hz > 0) {
			packets_owed += BLOCK_FRAMES / RATE_OUT_HZ * RTP_AUDIO_RATE_HZ
				/ RTP_AUDIO_FRAME_SAMPLES;
			while (packets_owed >= 1) {
				send_packet(hz, ++sequence, RTP_MICROPHONE_TO_TRANSMIT, RTP_AUDIO_PAYLOAD_TYPE,
					WHOLE_PACKET);
				packets_owed -= 1;
			}
		}
		rtp_microphone_poll();
		for (i = 0; i < BLOCK_FRAMES; i++)
			block[i] = OWN_MICROPHONE;
		rtp_microphone_fill(block, BLOCK_FRAMES, is_sending);
		if (gathered_count + BLOCK_FRAMES <= GATHERED_MOST) {
			memcpy(gathered + gathered_count, block, sizeof(block));
			gathered_count += BLOCK_FRAMES;
		}
	}
}

static int all_are(int from, int to, int32_t value) {
	int i;

	for (i = from; i < to; i++)
		if (gathered[i] != value)
			return 0;
	return 1;
}

// how large the tone at hz is in what was gathered between two places
static double tone_size(int from, int to, double hz) {
	double with_sine = 0;
	double with_cosine = 0;
	int i;

	for (i = from; i < to; i++) {
		with_sine += gathered[i] * sin(2.0 * M_PI * hz * i / RATE_OUT_HZ);
		with_cosine += gathered[i] * cos(2.0 * M_PI * hz * i / RATE_OUT_HZ);
	}
	return 2.0 * sqrt(with_sine * with_sine + with_cosine * with_cosine) / (to - from);
}

static double rms(int from, int to) {
	double sum = 0;
	int i;

	for (i = from; i < to; i++)
		sum += (double)gathered[i] * gathered[i];
	return sqrt(sum / (to - from));
}

int main(void) {
	double size;
	double all;
	int before;

	sender = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&port, 0, sizeof(port));
	port.sin_family = AF_INET;
	port.sin_port = htons(RTP_MICROPHONE_PORT);
	inet_pton(AF_INET, "127.0.0.1", &port.sin_addr);

	printf("rtp microphone:\n");
	expect(rtp_microphone_open() == 0, "the port opens");

	run(20, 0, 1);
	expect(all_are(0, gathered_count, OWN_MICROPHONE),
		"sending with nothing from afar, the radio's own microphone is left as it is");

	// half a second of a 1 kHz tone
	gathered_count = 0;
	run(47, 1000, 1);
	expect(all_are(BLOCK_FRAMES * 2, BLOCK_FRAMES * 9, 0),
		"from the first packet there is silence until 120 ms of speech is waiting");
	size = tone_size(24000, 43200, 1000);
	all = rms(24000, 43200) * sqrt(2.0);
	expect(fabs(size - TONE_OUT) < TONE_OUT * 0.03,
		"then a 1 kHz tone at half scale comes out at the level MIC 40 gives it");
	expect(all * all - size * size < size * size / 1000.0,
		"and what is not the tone is 30 dB under it or more");

	// the speech stops and the radio goes on sending
	gathered_count = 0;
	run(40, 0, 1);
	expect(all_are(gathered_count - BLOCK_FRAMES * 10, gathered_count, 0),
		"when the speech stops in mid transmission there is silence, not the microphone");

	// the transmission ends, and another begins with nobody speaking from afar
	gathered_count = 0;
	run(5, 0, 0);
	run(5, 0, 1);
	expect(all_are(0, gathered_count, OWN_MICROPHONE),
		"receiving, and in the next transmission, the microphone is the radio's own again");

	// what came while receiving is stale
	gathered_count = 0;
	run(20, 1000, 0);
	run(3, 0, 1);
	expect(all_are(0, gathered_count, OWN_MICROPHONE),
		"speech that came while the radio was receiving is not sent when it transmits");

	// a tone whose copy above 4 kHz would be heard
	gathered_count = 0;
	run(47, 3000, 1);
	size = tone_size(24000, 43200, 3000);
	expect(size > TONE_OUT * 0.7, "a 3 kHz tone, the top of the speech, comes through");
	expect(tone_size(24000, 43200, 45000) < size / 100.0,
		"its copy at 45 kHz, which bringing it up to 96000 leaves, is 40 dB down or more");

	run(5, 0, 0);
	rtp_microphone_packets();
	send_packet(1000, ++sequence, RTP_MICROPHONE_FROM_A_PROGRAM, RTP_AUDIO_PAYLOAD_TYPE, WHOLE_PACKET);
	send_packet(1000, sequence, RTP_MICROPHONE_TO_TRANSMIT, RTP_AUDIO_PAYLOAD_TYPE, WHOLE_PACKET);
	send_packet(1000, (uint16_t)(sequence - 5), RTP_MICROPHONE_TO_TRANSMIT, RTP_AUDIO_PAYLOAD_TYPE,
		WHOLE_PACKET);
	send_packet(1000, ++sequence, RTP_MICROPHONE_TO_TRANSMIT, 0, WHOLE_PACKET);
	send_packet(1000, ++sequence, RTP_MICROPHONE_TO_TRANSMIT, RTP_AUDIO_PAYLOAD_TYPE,
		WHOLE_PACKET - 1);
	send_packet(1000, ++sequence, RTP_MICROPHONE_TO_TRANSMIT, RTP_AUDIO_PAYLOAD_TYPE,
		WHOLE_PACKET + 8);
	send_packet(1000, ++sequence, RTP_MICROPHONE_VOIP, RTP_AUDIO_PAYLOAD_TYPE, WHOLE_PACKET);
	rtp_microphone_poll();
	before = rtp_microphone_packets();
	expect(before == 1,
		"one from a program is taken; one repeated, one out of order, one of another "
		"payload, two of the wrong size and one that is not to be transmitted are dropped");

	rtp_microphone_set_gain(80);
	gathered_count = 0;
	run(47, 1000, 1);
	expect(fabs(tone_size(24000, 43200, 1000) - 2.0 * TONE_OUT) < TONE_OUT * 0.06,
		"with MIC at 80 the tone is twice as large as at 40");

	rtp_microphone_close();
	if (checks_failed) {
		printf("rtp microphone: %d checks FAILED\n", checks_failed);
		return 1;
	}
	printf("rtp microphone: all checks passed\n");
	return 0;
}
