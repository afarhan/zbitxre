// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_telephone.c — the radio's sound made into the telephone's, and back.
//
// It needs no radio and no network. Tones are put through
// clients/remote/telephone.c a packet at a time, as the gateway puts the
// radio's sound through it, and what comes out is measured: that a tone
// comes out the tone it was at the level it was, that a tone which would
// fold down among the speech does not, that a level that does not change
// comes out the same from packet to packet, and that sound taken down and
// brought back up is still what it was.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "telephone.h"

#define PACKETS 50
#define SETTLED_PACKETS 3
#define HALF_SCALE 16384.0
#define ULAW_BIAS 0x84

static int checks_failed;
static double phase;
static double down[PACKETS * TELEPHONE_FRAME_SAMPLES];
static double up[PACKETS * WHOLE_FRAME_SAMPLES];
static uint8_t carried[PACKETS][TELEPHONE_FRAME_SAMPLES];

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

	return (value & 0x80) ? ULAW_BIAS - size : size - ULAW_BIAS;
}

// a second of a tone at the whole's rate goes down to the telephone's, and
// what came down goes back up
static void carry_a_tone(double hz) {
	uint8_t whole[WHOLE_FRAME_BYTES];
	int sample;
	int p;
	int i;

	telephone_open();
	for (p = 0; p < PACKETS; p++) {
		for (i = 0; i < WHOLE_FRAME_SAMPLES; i++) {
			sample = (int)(HALF_SCALE * sin(phase));
			phase += 2.0 * M_PI * hz / WHOLE_RATE_HZ;
			whole[2 * i] = (uint8_t)((uint16_t)sample >> 8);
			whole[2 * i + 1] = (uint8_t)sample;
		}
		telephone_from_whole(whole, carried[p]);
		for (i = 0; i < TELEPHONE_FRAME_SAMPLES; i++)
			down[p * TELEPHONE_FRAME_SAMPLES + i] = linear_of(carried[p][i]);
		telephone_to_whole(carried[p], whole);
		for (i = 0; i < WHOLE_FRAME_SAMPLES; i++)
			up[p * WHOLE_FRAME_SAMPLES + i] = (int16_t)(whole[2 * i] << 8 | whole[2 * i + 1]);
	}
}

// how large the tone at hz is in what is between two places
static double tone_size(const double *sound, int from, int to, double hz, double rate) {
	double with_sine = 0;
	double with_cosine = 0;
	int i;

	for (i = from; i < to; i++) {
		with_sine += sound[i] * sin(2.0 * M_PI * hz * i / rate);
		with_cosine += sound[i] * cos(2.0 * M_PI * hz * i / rate);
	}
	return 2.0 * sqrt(with_sine * with_sine + with_cosine * with_cosine) / (to - from);
}

static double all_of(const double *sound, int from, int to) {
	double sum = 0;
	int i;

	for (i = from; i < to; i++)
		sum += sound[i] * sound[i];
	return sqrt(2.0 * sum / (to - from));
}

int main(void) {
	int from_down = SETTLED_PACKETS * TELEPHONE_FRAME_SAMPLES;
	int to_down = PACKETS * TELEPHONE_FRAME_SAMPLES;
	int from_up = SETTLED_PACKETS * WHOLE_FRAME_SAMPLES;
	int to_up = PACKETS * WHOLE_FRAME_SAMPLES;
	uint8_t whole[WHOLE_FRAME_BYTES];
	uint8_t telephone[TELEPHONE_FRAME_SAMPLES];
	double size;
	double all;
	int is_steady = 1;
	int p;
	int i;

	printf("telephone:\n");
	carry_a_tone(1000.0);
	size = tone_size(down, from_down, to_down, 1000.0, TELEPHONE_RATE_HZ);
	all = all_of(down, from_down, to_down);
	expect(fabs(size - HALF_SCALE) < HALF_SCALE * 0.03,
		"a 1 kHz tone at half scale comes down a 1 kHz tone at half scale");
	expect(all * all - size * size < size * size / 1000.0,
		"with all that is not the tone 30 dB under it or more, which is mu-law's own");
	size = tone_size(up, from_up, to_up, 1000.0, WHOLE_RATE_HZ);
	all = all_of(up, from_up, to_up);
	expect(fabs(size - HALF_SCALE) < HALF_SCALE * 0.05,
		"and brought back up is a 1 kHz tone at half scale");
	expect(all * all - size * size < size * size / 1000.0, "as clean as it came down");
	expect(tone_size(up, from_up, to_up, 7000.0, WHOLE_RATE_HZ) < size / 100.0,
		"its copy at 7 kHz, which bringing it up leaves, is 40 dB down or more");

	carry_a_tone(3000.0);
	expect(tone_size(down, from_down, to_down, 3000.0, TELEPHONE_RATE_HZ) > HALF_SCALE * 0.7,
		"a 3 kHz tone, the top of the speech, comes through");

	carry_a_tone(5000.0);
	expect(all_of(down, from_down, to_down) < HALF_SCALE / 100.0,
		"a 5 kHz tone, which would fold down to 3 kHz, comes down 40 dB down or more");

	// a level that does not change, as the tests of the gateway use: it
	// comes down the one byte in every place, from packet to packet
	telephone_open();
	for (i = 0; i < WHOLE_FRAME_SAMPLES; i++) {
		whole[2 * i] = (uint8_t)(1052 >> 8);
		whole[2 * i + 1] = (uint8_t)(1052 & 0xff);
	}
	for (p = 0; p < 4; p++) {
		telephone_from_whole(whole, telephone);
		for (i = 0; i < TELEPHONE_FRAME_SAMPLES; i++)
			if (p > 0 && telephone[i] != telephone[0])
				is_steady = 0;
	}
	expect(is_steady && linear_of(telephone[0]) > 1000 && linear_of(telephone[0]) < 1100,
		"a steady level comes down the same byte in every place of every packet");

	if (checks_failed) {
		printf("telephone: %d checks FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("telephone: all checks passed\n");
	return EXIT_SUCCESS;
}
