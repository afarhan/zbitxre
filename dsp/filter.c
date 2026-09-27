// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// filter.c — the shape of a filter, as one number for each FFT bin.
//
// Thanks to Phil Karn, KA9Q. The method is his, and so is the code that
// makes the Kaiser window and shortens and tapers the impulse: it comes from
// filter.c of his ka9q-radio, Copyright 2017-2023, Phil Karn, KA9Q, by way of
// zbitxv2's fft_filter.c. The arithmetic is unchanged.
//
// The radio filters in the frequency domain: it turns a block of samples
// into bins, multiplies each bin by a number, and turns the bins back into
// samples. This file makes those numbers.
//
// The obvious shape, one inside the passband and zero outside, is a bad
// filter. A signal that falls between two bins, or any sharp edge, rings
// and clicks. So the shape is made in four steps:
//
//   1. Draw the obvious shape in the bins.
//   2. Turn it into samples. That is the filter's impulse response, and it
//      is as long as the block and does not die away.
//   3. Keep only the middle of it, FILTER_IMPULSE samples, and taper those
//      to nothing at both ends with a Kaiser window. Now it is short.
//   4. Turn it back into bins. The edges have become smooth.
//
// A filter shaped this way is what makes overlap-and-save work: with an
// impulse of FILTER_IMPULSE samples in a transform of FILTER_BINS, the last
// FILTER_BLOCK samples of every transform are clean and the rest are thrown
// away.
//
// Frequencies are given as a fraction of the sample rate, from -0.5 to 0.5.
// The first half of the bins are the positive frequencies and the second
// half the negative ones, so a passband of -3000 to -300 Hz is a lower
// sideband.
//
// The Kaiser window's beta sets how hard the taper is; zbitxv2 uses 5
// throughout.
//
// The two transforms are planned once, in filter_open(). Shaping a filter
// afterwards plans nothing, and is for one thread at a time.
//
// complex.h is included before fftw3.h so that FFTW's complex numbers are
// C's own.

#include <complex.h>
#include <math.h>
#include <string.h>
#include <fftw3.h>

#include "filter.h"

#define BESSEL_TERMS   40
#define BESSEL_ENOUGH  1e-12

static fftwf_plan to_samples;
static fftwf_plan to_bins;
static float complex *work;
static float taper[FILTER_IMPULSE];

static float bessel_i0(float z) {
	float t;
	float sum;
	float term;
	int k;

	t = (z * z) / 4;
	sum = 1 + t;
	term = t;
	for (k = 2; k < BESSEL_TERMS; k++) {
		term *= t / (k * k);
		sum += term;
		if (term < BESSEL_ENOUGH * sum)
			break;
	}
	return sum;
}

static void make_kaiser(float *window, unsigned int count, float beta) {
	float numerator;
	float one_over_denominator;
	float step;
	float p;
	int n;

	numerator = M_PI * beta;
	one_over_denominator = 1. / bessel_i0(numerator);
	step = 2.0 / (count - 1);

	for (n = 0; n < (int)(count / 2); n++) {
		p = step * n - 1;
		window[n] = bessel_i0(numerator * sqrtf(1 - p * p))
			* one_over_denominator;
		window[count - 1 - n] = window[n];
	}
	if (count & 1)
		window[(count - 1) / 2] = 1;
}

void filter_hann(float *window, int count) {
	int n;

	for (n = 0; n < count; n++)
		window[n] = 0.5 - 0.5 * cos(2 * M_PI * n / (count - 1));
}

int filter_open(void) {
	if (work != NULL)
		return 0;
	work = fftwf_alloc_complex(FILTER_BINS);
	if (work == NULL)
		return -1;
	to_bins = fftwf_plan_dft_1d(FILTER_BINS, work, work, FFTW_FORWARD,
		FFTW_MEASURE);
	to_samples = fftwf_plan_dft_1d(FILTER_BINS, work, work, FFTW_BACKWARD,
		FFTW_MEASURE);
	if (to_bins == NULL || to_samples == NULL)
		return -1;
	return 0;
}

static void draw_the_obvious_shape(float complex *shape, float low,
		float high) {
	float inside;
	float at;
	int n;

	inside = 1. / ((float)FILTER_BINS);
	for (n = 0; n < FILTER_BINS; n++) {
		if (n <= FILTER_BINS / 2)
			at = (float)n / FILTER_BINS;
		else
			at = (float)(n - FILTER_BINS) / FILTER_BINS;

		if (at >= low && at <= high)
			shape[n] = inside;
		else
			shape[n] = 0;
	}
}

static void shorten_and_taper(float complex *shape, float beta) {
	int n;

	memcpy(work, shape, FILTER_BINS * sizeof(*work));
	fftwf_execute(to_samples);

	make_kaiser(taper, FILTER_IMPULSE, beta);

	for (n = FILTER_IMPULSE - 1; n >= 0; n--)
		work[n] = work[(n - FILTER_IMPULSE / 2 + FILTER_BINS) % FILTER_BINS];
	for (n = FILTER_IMPULSE - 1; n >= 0; n--)
		work[n] = work[n] * taper[n] * 1.f;
	memset(work + FILTER_IMPULSE, 0,
		(FILTER_BINS - FILTER_IMPULSE) * sizeof(*work));

	fftwf_execute(to_bins);
	memcpy(shape, work, FILTER_BINS * sizeof(*shape));
}

int filter_shape(float complex *shape, float low, float high, float beta) {
	if (work == NULL)
		return -1;
	if (isnan(low) || isnan(high) || isnan(beta))
		return -1;
	draw_the_obvious_shape(shape, low, high);
	shorten_and_taper(shape, beta);
	return 0;
}
