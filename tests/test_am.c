// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_am.c — that the AM filter is around the carrier, wherever the board
// puts it, and that AM is sent there too.
//
// It needs no radio; it is built with tests/hw_stand_ins.c. The receiver is set up for the zbitx's first board, put
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
//
// Then the radio transmits in AM, the microphone given a 1 kHz tone that
// modulates it to half. What goes to the transmitter must have its carrier
// on the centre bin, where the radio is tuned, and a sideband 1 kHz either
// side of it, each a quarter of the carrier, as AM at half modulation has.
// zbitxv2 sent its carrier at 24 kHz, wherever the radio worked; on the
// zbitx, at bin 600, that is 4 kHz off the frequency, and there must be
// nothing there.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "audio.h"
#include "board.h"
#include "sdr.h"
#include "hw_stand_ins.h"

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
#define MIC_TONE_HZ 1000.0
// the microphone's samples are halved on the way in, and modems/modem_am.c
// takes 200000000 as full modulation: this is half
#define MIC_LEVEL 2.0e8
#define SIDEBAND_SHARE 0.25
#define SIDEBAND_SLACK 0.03
#define LEAST_CARRIER 1000.0

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

static int32_t to_transmitter[BLOCK];

static void one_block(double carrier_hz, double mic_level, int32_t *to_speaker) {
	static int32_t from_receiver[BLOCK];
	static int32_t from_microphone[BLOCK];
	struct audio_block block;
	double envelope;
	int i;

	for (i = 0; i < BLOCK; i++) {
		envelope = 1.0 + DEPTH * sin(2 * M_PI * TONE_HZ * seconds);
		from_receiver[i] = (int32_t)(LEVEL * envelope
			* sin(2 * M_PI * carrier_hz * seconds));
		from_microphone[i] = (int32_t)(mic_level
			* sin(2 * M_PI * MIC_TONE_HZ * seconds));
		seconds += 1.0 / SAMPLE_RATE;
	}
	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = BLOCK;
	hw_stand_in_block(&block);
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
		one_block(carrier_hz, 0, to_speaker);

	with_sine = 0;
	with_cosine = 0;
	count = 0;
	for (b = 0; b < MEASURED_BLOCKS; b++) {
		one_block(carrier_hz, 0, to_speaker);
		for (i = 0; i < BLOCK; i++) {
			t = count / SAMPLE_RATE;
			with_sine += to_speaker[i] * sin(2 * M_PI * TONE_HZ * t);
			with_cosine += to_speaker[i] * cos(2 * M_PI * TONE_HZ * t);
			count++;
		}
	}
	return 2.0 * sqrt(with_sine * with_sine + with_cosine * with_cosine) / count;
}

//how strong each of four frequencies is in what goes to the transmitter
#define SENT_HZ 4

static void sent(const double *hz, double *strength) {
	static int32_t to_speaker[BLOCK];
	double with_sine[SENT_HZ] = { 0 };
	double with_cosine[SENT_HZ] = { 0 };
	double t;
	long count;
	int b;
	int i;
	int k;

	for (b = 0; b < WARM_UP_BLOCKS; b++)
		one_block(0, MIC_LEVEL, to_speaker);
	count = 0;
	for (b = 0; b < MEASURED_BLOCKS; b++) {
		one_block(0, MIC_LEVEL, to_speaker);
		for (i = 0; i < BLOCK; i++) {
			t = count / SAMPLE_RATE;
			for (k = 0; k < SENT_HZ; k++) {
				with_sine[k] += to_transmitter[i] * sin(2 * M_PI * hz[k] * t);
				with_cosine[k] += to_transmitter[i] * cos(2 * M_PI * hz[k] * t);
			}
			count++;
		}
	}
	for (k = 0; k < SENT_HZ; k++)
		strength[k] = 2.0 * sqrt(with_sine[k] * with_sine[k]
			+ with_cosine[k] * with_cosine[k]) / count;
}

static void check_sending(double carrier_hz) {
	double hz[SENT_HZ] = { carrier_hz, carrier_hz - MIC_TONE_HZ,
		carrier_hz + MIC_TONE_HZ, MIDDLE_HZ };
	double strength[SENT_HZ];
	char answer[ANSWER_MAX];

	ask("tx_power=40");
	sdr_request("tx=on", answer);
	expect(answer[0] == 'o', "AM is let transmit");
	sent(hz, strength);
	ask("tx=off");
	printf("          sent: carrier %.0f, lower %.0f, upper %.0f, at 24 kHz %.0f\n",
		strength[0], strength[1], strength[2], strength[3]);
	expect(strength[0] > LEAST_CARRIER, "the carrier is sent on the centre bin");
	expect(fabs(strength[1] / strength[0] - SIDEBAND_SHARE) < SIDEBAND_SLACK
			&& fabs(strength[2] / strength[0] - SIDEBAND_SHARE) < SIDEBAND_SLACK,
		"with both sidebands, each a quarter of the carrier at half modulation");
	if (carrier_hz != MIDDLE_HZ)
		expect(strength[3] * LEAST_REJECTION < strength[0],
			"and nothing at 24 kHz, where zbitxv2 sent its carrier");
}

int main(void) {
	const struct board *board;
	double carrier_hz;
	double on_the_bin;
	double in_the_middle;

	board = &board_zbitx_1;
	hw_stand_in_radio(board->radio);
	if (setup() < 0) {
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

	check_sending(carrier_hz);

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("AM: all checks passed\n");
	return EXIT_SUCCESS;
}
