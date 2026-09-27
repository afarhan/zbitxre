// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_am.c — that the AM filter is around the carrier, wherever the board
// puts it.
//
// It needs no radio. The receiver is set up for the zbitx's first board, put
// in AM with the AGC off, and given an AM station: a carrier with a 400 Hz
// tone on it. The test then looks for the 400 Hz tone in what goes to the
// speaker.
//
// The station is played twice at the same strength. First its carrier is put
// on the board's centre bin, where a station tuned in lands. The tone must
// come through. Then the carrier is put at 24 kHz, the middle of what the
// sound card sees, which is where the filter would be if it did not follow
// the bin. On a board whose centre bin is not there, that station is out of
// the passband and the tone must be all but gone.
//
// On a board that works at 24 kHz the two are the same station and the
// second check is left out.
//
// The station is kept weak. With the AGC off the receiver's gain is fixed and
// high, and a strong station takes the samples past what 32 bits hold.
//
// How strong the tone is, is measured the way a tone decoder does it: the
// samples are multiplied by a sine and a cosine of 400 Hz and summed.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "audio.h"
#include "board.h"
#include "sdr.h"

#define BLOCK 1024
#define SAMPLE_RATE 96000.0
#define HZ_PER_BIN 46.875
#define MIDDLE_HZ 24000.0
#define TONE_HZ 400.0
#define DEPTH 0.5
#define LEVEL 2.0e6
#define WARM_UP_BLOCKS 60
#define MEASURED_BLOCKS 45
#define LEAST_REJECTION 100.0
#define ANSWER_MAX 100

static int checks_failed;
static double seconds;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static void ask(char *request) {
	char answer[ANSWER_MAX];

	sdr_request(request, answer);
}

static void one_block(double carrier_hz, int32_t *to_speaker) {
	static int32_t from_receiver[BLOCK];
	static int32_t from_microphone[BLOCK];
	static int32_t to_transmitter[BLOCK];
	struct audio_block block;
	double envelope;
	int i;

	for (i = 0; i < BLOCK; i++) {
		envelope = 1.0 + DEPTH * sin(2 * M_PI * TONE_HZ * seconds);
		from_receiver[i] = (int32_t)(LEVEL * envelope
			* sin(2 * M_PI * carrier_hz * seconds));
		seconds += 1.0 / SAMPLE_RATE;
	}
	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = BLOCK;
	sound_block(&block);
}

static double tone_heard(double carrier_hz) {
	static int32_t to_speaker[BLOCK];
	double with_sine;
	double with_cosine;
	double t;
	long count;
	int b;
	int i;

	for (b = 0; b < WARM_UP_BLOCKS; b++)
		one_block(carrier_hz, to_speaker);

	with_sine = 0;
	with_cosine = 0;
	count = 0;
	for (b = 0; b < MEASURED_BLOCKS; b++) {
		one_block(carrier_hz, to_speaker);
		for (i = 0; i < BLOCK; i++) {
			t = count / SAMPLE_RATE;
			with_sine += to_speaker[i] * sin(2 * M_PI * TONE_HZ * t);
			with_cosine += to_speaker[i] * cos(2 * M_PI * TONE_HZ * t);
			count++;
		}
	}
	return 2.0 * sqrt(with_sine * with_sine + with_cosine * with_cosine) / count;
}

int main(void) {
	const struct board *board;
	double carrier_hz;
	double on_the_bin;
	double in_the_middle;

	board = &board_zbitx_1;
	if (setup_without_radio(board) < 0) {
		printf("the receiver did not open\n");
		return EXIT_FAILURE;
	}
	carrier_hz = HZ_PER_BIN * board->centre_bin;
	printf("board: %s, centre bin %d, a station's carrier at %.0f Hz\n",
		board->name, board->centre_bin, carrier_hz);

	ask("r1:freq=7040000");
	ask("r1:mode=AM");
	ask("r1:agc=OFF");
	ask("r1:low=300");
	ask("r1:high=3000");

	on_the_bin = tone_heard(carrier_hz);
	printf("          tone from a station on the centre bin: %.0f\n", on_the_bin);
	expect(on_the_bin > 0, "a station on the centre bin is heard");

	if (carrier_hz != MIDDLE_HZ) {
		in_the_middle = tone_heard(MIDDLE_HZ);
		printf("          tone from a station at 24 kHz:        %.0f\n",
			in_the_middle);
		expect(in_the_middle * LEAST_REJECTION < on_the_bin,
			"a station at 24 kHz is out of the passband");
	}

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("AM filter: all checks passed\n");
	return EXIT_SUCCESS;
}
