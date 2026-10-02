// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// start_check.c — whether the spectrum has the humps as the radio starts.
//
// THE FAULT
//
// Now and then the radio starts with its spectrum in humps: the band's
// noise has nulls some 10 dB deep, about 7.5 kHz apart, and the sound is
// wrong. The author has seen it for years. His observation is that it is
// there from the start or not at all: it never comes on in a radio that
// started well. hw/audio.c tells what is known of it; reopening the sound
// card has cured it the one time that was tried.
//
// The author's decision is that the card is to be reopened only when the
// fault is there. So the radio measures as it starts, and when what it
// measures says the humps are there, tools/sdr.c reopens the card and
// measures again.
//
// WHAT IS MEASURED
//
// The humps are a ripple along the spectrum. So the spectrum of the
// samples as the sound card gave them is taken, from 15 to 45 kHz where the
// IF is; its slow shape, the IF filter's, is taken out as a curve of the
// second degree; and what is left is tried against a ripple of each period
// from 6 to 9 kHz. The size of the strongest, in dB from the middle to the
// peak, is the figure.
//
// On 2 October 2026 the fault came three times. With the humps the figure
// was 4.4 to 4.8 dB; on a dozen good starts and after each cure it was 0.8
// to 2.6 dB. START_CHECK_SUSPECT_DB, 3 dB, is between the two.
//
// Strong signals in the passband ripple the spectrum too and may raise the
// figure with no fault there; that is one of the things to be found out.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "kiss_fft.h"
#include "start_check.h"

#define RATE_HZ 96000.0
#define SEGMENT 4096
#define HZ_PER_BIN (RATE_HZ / SEGMENT)
#define BAND_LOW_HZ 15000.0
#define BAND_HIGH_HZ 45000.0
#define BAND_MIDDLE_HZ ((BAND_LOW_HZ + BAND_HIGH_HZ) / 2)
#define BAND_HALF_HZ ((BAND_HIGH_HZ - BAND_LOW_HZ) / 2)
#define BAND_BINS_MAX (SEGMENT / 2)
#define PERIOD_LOWEST_HZ 6000.0
#define PERIOD_HIGHEST_HZ 9000.0
#define PERIOD_STEP_HZ 250.0
#define LEAST_POWER 1e-9
#define DB_PER_BEL 10.0

static kiss_fft_cpx fft_in[SEGMENT];
static kiss_fft_cpx fft_out[SEGMENT];
static double level_db[BAND_BINS_MAX];
static double hz_of[BAND_BINS_MAX];

// the power in each bin of the band, summed over half-overlapped segments
// of one channel, and then in dB; says how many bins there are
static int band_levels(const int32_t *samples, int frames, int channels) {
	kiss_fft_cfg plan = kiss_fft_alloc(SEGMENT, 0, NULL, NULL);
	int first_bin = (int)ceil(BAND_LOW_HZ / HZ_PER_BIN);
	int last_bin = (int)floor(BAND_HIGH_HZ / HZ_PER_BIN);
	int bins = last_bin - first_bin + 1;
	double mean = 0;
	double window;
	int start;
	int i;

	if (plan == NULL)
		return 0;
	for (i = 0; i < frames; i++)
		mean += samples[i * channels];
	mean /= frames;
	memset(level_db, 0, sizeof(level_db));
	for (start = 0; start + SEGMENT <= frames; start += SEGMENT / 2) {
		for (i = 0; i < SEGMENT; i++) {
			window = 0.5 - 0.5 * cos(2 * M_PI * i / (SEGMENT - 1));
			fft_in[i].r = (float)((samples[(start + i) * channels] - mean) * window);
			fft_in[i].i = 0;
		}
		kiss_fft(plan, fft_in, fft_out);
		for (i = 0; i < bins; i++)
			level_db[i] += (double)fft_out[first_bin + i].r * fft_out[first_bin + i].r
				+ (double)fft_out[first_bin + i].i * fft_out[first_bin + i].i;
	}
	free(plan);
	for (i = 0; i < bins; i++) {
		level_db[i] = DB_PER_BEL * log10(level_db[i] + LEAST_POWER);
		hz_of[i] = (first_bin + i) * HZ_PER_BIN;
	}
	return bins;
}

static double determinant(double m[3][3]) {
	return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
		- m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
		+ m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// takes out of the levels the curve of the second degree that fits them
// best, which is the shape of the IF filter and not the ripple
static void take_out_the_slow_shape(int bins) {
	double power_sum[5] = { 0, 0, 0, 0, 0 };
	double level_sum[3] = { 0, 0, 0 };
	double m[3][3];
	double with_column[3][3];
	double fit[3];
	double whole;
	double x;
	double x_to_the;
	int row;
	int column;
	int p;
	int i;

	for (i = 0; i < bins; i++) {
		x = (hz_of[i] - BAND_MIDDLE_HZ) / BAND_HALF_HZ;
		x_to_the = 1;
		for (p = 0; p < 5; p++) {
			power_sum[p] += x_to_the;
			if (p < 3)
				level_sum[p] += x_to_the * level_db[i];
			x_to_the *= x;
		}
	}
	for (row = 0; row < 3; row++) {
		for (column = 0; column < 3; column++)
			m[row][column] = power_sum[row + column];
	}
	whole = determinant(m);
	if (whole == 0)
		return;
	for (p = 0; p < 3; p++) {
		memcpy(with_column, m, sizeof(m));
		for (row = 0; row < 3; row++)
			with_column[row][p] = level_sum[row];
		fit[p] = determinant(with_column) / whole;
	}
	for (i = 0; i < bins; i++) {
		x = (hz_of[i] - BAND_MIDDLE_HZ) / BAND_HALF_HZ;
		level_db[i] -= fit[0] + fit[1] * x + fit[2] * x * x;
	}
}

// how large a ripple of one period is in the levels, middle to peak
static double ripple_of_period(int bins, double period_hz) {
	double with_cosine = 0;
	double with_sine = 0;
	int i;

	for (i = 0; i < bins; i++) {
		with_cosine += level_db[i] * cos(2 * M_PI * hz_of[i] / period_hz);
		with_sine += level_db[i] * sin(2 * M_PI * hz_of[i] / period_hz);
	}
	return 2 * hypot(with_cosine, with_sine) / bins;
}

// the size in dB of the strongest ripple in the spectrum of the first
// channel of the samples, and its period; 0 if there are too few samples
double start_check_ripple_db(const int32_t *samples, int frames, int channels,
		double *period_hz) {
	double strongest = 0;
	double ripple;
	double period;
	int bins;

	*period_hz = 0;
	if (frames < SEGMENT)
		return 0;
	bins = band_levels(samples, frames, channels);
	if (bins == 0)
		return 0;
	take_out_the_slow_shape(bins);
	for (period = PERIOD_LOWEST_HZ; period <= PERIOD_HIGHEST_HZ; period += PERIOD_STEP_HZ) {
		ripple = ripple_of_period(bins, period);
		if (ripple > strongest) {
			strongest = ripple;
			*period_hz = period;
		}
	}
	return strongest;
}
