// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_rtp_voip.c — the radio's side of VoIP, rtp_voip.c.
//
// It needs no radio. Another's speech is sent to rtp_microphone.c's port as
// the gateway sends it, a tone under the label that says it is to be heard
// and whose it is, and the blocks the speaker is given are looked at: the
// receiver's, which the test fills with a number that stands for it, or
// the tone, or silence. The owner's speech is a tone given as the
// microphone's blocks, and the packets the gateway would be sent are caught
// at its port.

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
#include "rtp_voip.h"

#define BLOCK_FRAMES 1024
#define RATE_RADIO_HZ 96000.0
#define RECEIVER 12345
#define HALF_SCALE 16384.0
#define WHOLE_PACKET (RTP_MICROPHONE_LABEL_BYTES + RTP_PACKET_BYTES)
#define GATHERED_MOST (96000 * 4)
// the microphone's 32 bits to 16, as rtp_voip.c and RADE's modem take them
#define MIC_A_SAMPLE 6553.6

static int checks_failed;
static int sender;
static struct sockaddr_in port;
static uint16_t sequence = 100;
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

// one packet of another's speech, a 1 kHz tone at half scale, as the
// gateway hands it on
static void send_heard(const char *callsign) {
	uint8_t packet[WHOLE_PACKET];
	uint8_t *rtp = packet + RTP_MICROPHONE_LABEL_BYTES;
	int sample;
	int i;

	memset(packet, 0, sizeof(packet));
	packet[0] = RTP_MICROPHONE_VOIP;
	memcpy(packet + RTP_MICROPHONE_KIND_BYTES, callsign, strlen(callsign));
	rtp[0] = 0x80;
	rtp[1] = RTP_AUDIO_PAYLOAD_TYPE;
	sequence++;
	rtp[2] = (uint8_t)(sequence >> 8);
	rtp[3] = (uint8_t)sequence;
	for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++) {
		sample = (int)(HALF_SCALE * sin(phase));
		rtp[RTP_HEADER_BYTES + 2 * i] = (uint8_t)((uint16_t)sample >> 8);
		rtp[RTP_HEADER_BYTES + 2 * i + 1] = (uint8_t)sample;
		phase += 2.0 * M_PI * 1000.0 / RTP_AUDIO_RATE_HZ;
	}
	sendto(sender, packet, sizeof(packet), 0, (struct sockaddr *)&port, sizeof(port));
	usleep(200);
}

// so many of the speaker's blocks; with is_speech, another's packets come
// as fast as the blocks use them
static void run(int blocks, int is_speech, int is_sending) {
	int32_t block[BLOCK_FRAMES];
	int i;

	for (; blocks > 0; blocks--) {
		if (is_speech) {
			packets_owed += BLOCK_FRAMES / RATE_RADIO_HZ * RTP_AUDIO_RATE_HZ
				/ RTP_AUDIO_FRAME_SAMPLES;
			while (packets_owed >= 1) {
				send_heard("VU3XYZ");
				packets_owed -= 1;
			}
		}
		rtp_microphone_poll();
		for (i = 0; i < BLOCK_FRAMES; i++)
			block[i] = RECEIVER;
		rtp_voip_speaker(block, BLOCK_FRAMES, is_sending);
		if (gathered_count + BLOCK_FRAMES <= GATHERED_MOST) {
			memcpy(gathered + gathered_count, block, sizeof(block));
			gathered_count += BLOCK_FRAMES;
		}
	}
}

static int count_of(int from, int to, int32_t value) {
	int count = 0;
	int i;

	for (i = from; i < to; i++)
		if (gathered[i] == value)
			count++;
	return count;
}

static double tone_size(int from, int to, double hz) {
	double with_sine = 0;
	double with_cosine = 0;
	int i;

	for (i = from; i < to; i++) {
		with_sine += gathered[i] * sin(2.0 * M_PI * hz * i / RATE_RADIO_HZ);
		with_cosine += gathered[i] * cos(2.0 * M_PI * hz * i / RATE_RADIO_HZ);
	}
	return 2.0 * sqrt(with_sine * with_sine + with_cosine * with_cosine) / (to - from);
}

// the gateway's port, where the owner's speech is caught
static int gateway_open(void) {
	struct sockaddr_in here;
	int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);

	memset(&here, 0, sizeof(here));
	here.sin_family = AF_INET;
	here.sin_port = htons(RTP_VOIP_PORT);
	inet_pton(AF_INET, "127.0.0.1", &here.sin_addr);
	if (fd < 0 || bind(fd, (struct sockaddr *)&here, sizeof(here)) < 0)
		return -1;
	return fd;
}

// the owner speaks a tone into the microphone for so many blocks; the
// packets the gateway is sent are counted, and the samples of the last
// kept in what is returned
static int speak(int gateway, int blocks, double hz, double level, int16_t *last) {
	uint8_t packet[RTP_PACKET_BYTES + 8];
	int32_t block[BLOCK_FRAMES];
	double at = 0;
	int count = 0;
	int i;

	for (; blocks > 0; blocks--) {
		for (i = 0; i < BLOCK_FRAMES; i++) {
			block[i] = (int32_t)(level * sin(at));
			at += 2.0 * M_PI * hz / RATE_RADIO_HZ;
		}
		rtp_voip_microphone(block, BLOCK_FRAMES);
		rtp_voip_poll();
		while (recv(gateway, packet, sizeof(packet), 0) > 0) {
			if (packet[0] == 0x80 && packet[1] == RTP_AUDIO_PAYLOAD_TYPE)
				count++;
			for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++)
				last[i] = (int16_t)(packet[RTP_HEADER_BYTES + 2 * i] << 8
					| packet[RTP_HEADER_BYTES + 2 * i + 1]);
		}
	}
	return count;
}

int main(void) {
	char callsign[16];
	int16_t last[RTP_AUDIO_FRAME_SAMPLES];
	double loudest;
	int gateway;
	int sent;
	int i;

	printf("rtp voip:\n");
	expect(rtp_microphone_open() == 0 && rtp_voip_open() == 0, "the ports open");
	sender = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&port, 0, sizeof(port));
	port.sin_family = AF_INET;
	port.sin_port = htons(RTP_MICROPHONE_PORT);
	inet_pton(AF_INET, "127.0.0.1", &port.sin_addr);

	// nobody speaking: the receiver is heard
	rtp_voip_set_hearing(1);
	gathered_count = 0;
	run(10, 0, 0);
	expect(count_of(0, gathered_count, RECEIVER) == gathered_count,
		"with nobody speaking the receiver is heard");

	// another speaks
	gathered_count = 0;
	run(60, 1, 0);
	expect(count_of(BLOCK_FRAMES * 30, gathered_count, RECEIVER) == 0,
		"while another speaks, the speech takes the receiver's place");
	expect(tone_size(BLOCK_FRAMES * 30, gathered_count, 1000) > HALF_SCALE * 16000.0 * 0.9,
		"and is the tone that was sent, at the speaker's level");
	rtp_voip_heard_callsign(callsign, sizeof(callsign));
	expect(!strcmp(callsign, "VU3XYZ"), "who is speaking is said");

	// the speech stops: half a second of silence, then the receiver
	gathered_count = 0;
	run(80, 0, 0);
	expect(count_of(BLOCK_FRAMES * 10, BLOCK_FRAMES * 40, RECEIVER) == 0,
		"for a while after the speech stops, the receiver is not heard");
	expect(count_of(BLOCK_FRAMES * 60, gathered_count, RECEIVER) == BLOCK_FRAMES * 20,
		"and then it is heard again");
	usleep(800000);
	rtp_voip_heard_callsign(callsign, sizeof(callsign));
	expect(callsign[0] == 0, "and nobody is said to be speaking");

	// transmitting, or where hearing is not allowed, the receiver's block
	// is left alone and the speech is let go
	gathered_count = 0;
	run(40, 1, 1);
	expect(count_of(0, gathered_count, RECEIVER) == gathered_count,
		"while the radio transmits, another's speech is not heard");
	rtp_voip_set_hearing(0);
	gathered_count = 0;
	run(40, 1, 0);
	expect(count_of(0, gathered_count, RECEIVER) == gathered_count,
		"nor where hearing it is not allowed: a mode that is not a voice mode");
	rtp_voip_heard_callsign(callsign, sizeof(callsign));
	expect(callsign[0] == 0, "and then nobody is said to be speaking");
	rtp_voip_set_hearing(1);
	gathered_count = 0;
	run(3, 0, 0);
	expect(count_of(0, gathered_count, RECEIVER) == gathered_count,
		"what came while it was not allowed is not played after");

	// the owner speaks
	gateway = gateway_open();
	expect(gateway >= 0, "the gateway's port can be listened on");
	rtp_voip_set_speaking(0);
	expect(speak(gateway, 20, 1000, 8192 * MIC_A_SAMPLE, last) == 0,
		"not speaking, nothing goes to the gateway");
	rtp_voip_set_speaking(1);
	// a second of blocks is 93.75 of them, and fifty packets
	sent = speak(gateway, 94, 1000, 8192 * MIC_A_SAMPLE, last);
	expect(sent >= 49 && sent <= 50, "speaking, fifty packets a second go to the gateway");
	loudest = 0;
	for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++)
		if (abs(last[i]) > loudest)
			loudest = abs(last[i]);
	expect(fabs(loudest - 8192) < 8192 * 0.03,
		"with the microphone's samples at MIC_A_SAMPLE to one, as RADE takes them");
	sent = speak(gateway, 94, 30000, 8192 * MIC_A_SAMPLE, last);
	loudest = 0;
	for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++)
		if (abs(last[i]) > loudest)
			loudest = abs(last[i]);
	expect(loudest < 8192 * 0.1, "a 30 kHz tone, above what 48000 a second holds, is kept out");
	rtp_voip_set_speaking(0);
	expect(speak(gateway, 20, 1000, 8192 * MIC_A_SAMPLE, last) == 0,
		"and when the owner stops, nothing more goes");

	close(gateway);
	rtp_voip_close();
	rtp_microphone_close();
	if (checks_failed) {
		printf("rtp voip: %d checks FAILED\n", checks_failed);
		return 1;
	}
	printf("rtp voip: all checks passed\n");
	return 0;
}
