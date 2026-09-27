// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// tone.c — an audio tone, one sample at a time.
//
// A tone is a phase that advances by a fixed step with every sample and
// wraps at 65536, which is one full turn. The sample is the sine of the
// phase, looked up in a table that holds a quarter of a turn; the other
// three quarters are the same numbers read backwards or negated.
//
// The step is the frequency times 65536 over the sample rate, in whole
// numbers, so a tone is within 1.5 Hz of what was asked.
//
// A sample is the sine times 2^30.
//
// This is zbitxv2's vfo.c. It is called a tone here because in a radio a VFO
// is the oscillator that tunes it, and this is not that.

#include <math.h>

#include "tone.h"

#define SAMPLE_RATE      96000
#define FULL_TURN        65536
#define QUARTER_TURN     16384
#define HALF_TURN        32768
#define THREE_QUARTERS   49152
#define TABLE_ENTRIES    (QUARTER_TURN + 1)
#define PEAK             1073741824.0

static int quarter[TABLE_ENTRIES];

void tone_open(void) {
	double angle;
	int i;

	for (i = 0; i < TABLE_ENTRIES; i++) {
		angle = (M_PI / 2) * ((double)i) / ((double)TABLE_ENTRIES);
		quarter[i] = (int)(sin(angle) * PEAK);
	}
}

void tone_start(struct tone *tone, int hz, int phase) {
	tone->step = (hz * FULL_TURN) / SAMPLE_RATE;
	tone->phase = phase;
	tone->hz = hz;
}

int tone_next(struct tone *tone) {
	int sample;

	if (tone->phase < QUARTER_TURN)
		sample = quarter[tone->phase];
	else if (tone->phase < HALF_TURN)
		sample = quarter[HALF_TURN - 1 - tone->phase];
	else if (tone->phase < THREE_QUARTERS)
		sample = -quarter[tone->phase - HALF_TURN];
	else
		sample = -quarter[FULL_TURN - 1 - tone->phase];

	tone->phase += tone->step;
	tone->phase &= FULL_TURN - 1;
	return sample;
}
