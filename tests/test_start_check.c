// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_start_check.c — the measure of the humps, tried on noise that is
// made here.
//
// Plain noise has a flat spectrum and should measure small. The same noise
// with a copy of itself added 13 samples late has a spectrum with nulls
// 7.4 kHz apart, which is the shape of the fault, and should measure large
// and at about that period. It needs no radio.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "start_check.h"

#define FRAMES 16384
#define CHANNELS 2
#define ECHO_SAMPLES 13
#define ECHO_PERIOD_HZ (96000.0 / ECHO_SAMPLES)
#define PERIOD_SLACK_HZ 600.0
#define NOISE_PEAK 1000000
#define SEED 12345u

static int32_t noise[FRAMES + ECHO_SAMPLES];
static int32_t samples[FRAMES * CHANNELS];
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

int main(void) {
	double period_hz;
	double plain_db;
	double echo_db;
	int i;

	srand(SEED);
	for (i = 0; i < FRAMES + ECHO_SAMPLES; i++)
		noise[i] = rand() % (2 * NOISE_PEAK) - NOISE_PEAK;

	for (i = 0; i < FRAMES; i++)
		samples[i * CHANNELS] = noise[i + ECHO_SAMPLES];
	plain_db = start_check_ripple_db(samples, FRAMES, CHANNELS, &period_hz);
	printf("          plain noise measures %.1f dB\n", plain_db);
	expect(plain_db < START_CHECK_SUSPECT_DB, "plain noise is not suspected");

	for (i = 0; i < FRAMES; i++)
		samples[i * CHANNELS] = noise[i + ECHO_SAMPLES] + noise[i] / 2;
	echo_db = start_check_ripple_db(samples, FRAMES, CHANNELS, &period_hz);
	printf("          with its echo it measures %.1f dB at %.2f kHz\n",
		echo_db, period_hz / 1000);
	expect(echo_db > START_CHECK_SUSPECT_DB, "noise with an echo 13 samples late is suspected");
	expect(period_hz > ECHO_PERIOD_HZ - PERIOD_SLACK_HZ
		&& period_hz < ECHO_PERIOD_HZ + PERIOD_SLACK_HZ,
		"and its ripple is found at the echo's period");
	expect(start_check_ripple_db(samples, 100, CHANNELS, &period_hz) == 0,
		"too few samples measure nothing");

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("start check: all checks passed\n");
	return EXIT_SUCCESS;
}
