// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// filter.c — the shape of a filter, as one number for each FFT bin.
//
// Thanks to Phil Karn, KA9Q. The method is his, and so is the code that
// makes the Kaiser window and windows the filter: it comes from filter.c of
// his ka9q-radio, Copyright 2017-2023, Phil Karn, KA9Q, by way of zbitxv2's
// fft_filter.c. The functions keep the names they have there and the
// arithmetic is unchanged.
//
// The radio filters in the frequency domain: it turns a block of samples
// into bins, multiplies each bin by a number, and turns the bins back into
// samples. This file makes those numbers, the fir_coeff of a struct filter.
//
// The obvious shape, one inside the passband and zero outside, is a bad
// filter. A signal that falls between two bins, or any sharp edge, rings
// and clicks. So filter_tune() makes the shape in four steps:
//
//   1. It draws the obvious shape in the bins.
//   2. window_filter() turns it into samples. That is the filter's impulse
//      response, and it is as long as the block and does not die away.
//   3. It keeps only the middle of it, M samples, and tapers those to
//      nothing at both ends with a Kaiser window. Now it is short.
//   4. It turns it back into bins. The edges have become smooth.
//
// L is the length of a block of samples, M the length of the impulse, and
// N = L + M - 1 the length of the transform. The radio uses L = 1024 and
// M = 1025, so N = 2048. With an impulse of M samples in a transform of N,
// the last L samples of every transform are clean and the rest are thrown
// away, which is what makes overlap and save work.
//
// Frequencies are given as a fraction of the sample rate, from -0.5 to 0.5.
// The first half of the bins are the positive frequencies and the second
// half the negative ones, so a passband of -3000 to -300 Hz is a lower
// sideband.
//
// The Kaiser window's beta sets how hard the taper is; the radio uses 5
// throughout.
//
// The two transforms that window the filter are kiss_fft's, as the
// receiver's are. kiss_fft works on a struct of two floats, laid out as a
// complex float is, so the response is handed to it as its own.
//
// filter_tune() is for one thread at a time.
//
// complex.h is included before fftw3.h so that FFTW's complex numbers are
// C's own.

#define _GNU_SOURCE 1
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <complex.h>
#include <math.h>

#include "sdr.h"

// Modified Bessel function of the 0th kind, used by the Kaiser window
static float i0(float const z){
	const float t = (z*z)/4;
	float sum = 1 + t;
	float term = t;
	for(int k=2; k<40; k++){
		term *= t/(k*k);
		sum += term;
		if(term < 1e-12 * sum)
			break;
	}
	return sum;
}

// Compute an entire Kaiser window
// More efficient than repeatedly calling kaiser(n,M,beta)
static int make_kaiser(float * const window,unsigned int const M,float const beta){
	assert(window != NULL);
	if(window == NULL)
		return -1;
	// Precompute unchanging partial values
	float const numc = M_PI * beta;
	float const inv_denom = 1. / i0(numc); // Inverse of denominator
	float const pc = 2.0 / (M-1);

	// The window is symmetrical, so compute only half of it and mirror
	// this won't compute the middle value in an odd-length sequence
	for(unsigned int n = 0; n < M/2; n++){
		float const p = pc * n  - 1;
		window[n] = i0(numc * sqrtf(1-p*p)) * inv_denom;
		window[M-1-n] = window[n];
	}
	// If sequence length is odd, middle value is unity
	if(M & 1)
		window[(M-1)/2] = 1; // The -1 is actually unnecessary

	return 0;
}

// Apply Kaiser window to filter frequency response
// "response" is SIMD-aligned array of N complex floats
// Impulse response will be limited to first M samples in the time domain
// Phase is adjusted so "time zero" (center of impulse response) is at M/2
// L and M refer to the decimated output
static int window_filter(int const L,int const M,complex float * const response,float const beta){

	//total length of the convolving samples
	int const N = L + M - 1;

	// the response is worked on in a copy, and each transform is given a
	// second array to write to
	complex float * const buffer = malloc(N * sizeof(complex float));
	complex float * const transformed = malloc(N * sizeof(complex float));

	kiss_fft_cfg fwd_filter_plan = kiss_fft_alloc(N, 0, NULL, NULL);
	kiss_fft_cfg rev_filter_plan = kiss_fft_alloc(N, 1, NULL, NULL);

	// Convert to time domain
	kiss_fft(rev_filter_plan, (const kiss_fft_cpx *)response, (kiss_fft_cpx *)buffer);
	kiss_fft_free(rev_filter_plan);

	float kaiser_window[M];
	make_kaiser(kaiser_window,M,beta);

	// Round trip through FFT/IFFT scales by N
	float const gain = 1.;

	//shift the buffer to make it causal
	for(int n = M - 1; n >= 0; n--)
		buffer[n] = buffer[ (n-M/2+N) % N];

	// apply window and gain
	for(int n = M - 1; n >= 0; n--)
		buffer[n] = buffer[n] * kaiser_window[n] * gain;

	// Pad with zeroes on right side
	memset(buffer+M,0,(N-M)*sizeof(*buffer));

	// Now back to frequency domain
	kiss_fft(fwd_filter_plan, (const kiss_fft_cpx *)buffer, (kiss_fft_cpx *)transformed);
	kiss_fft_free(fwd_filter_plan);

	memcpy(response,transformed,N*sizeof(*response));

	free(buffer);
	free(transformed);
	return 0;
}

struct filter *filter_new(int input_length, int impulse_length){

	struct filter *f = malloc(sizeof(struct filter));
	f->L = input_length;
	f->M = impulse_length;
	f->N = f->L + f->M - 1;
	f->fir_coeff = malloc(f->N * sizeof(complex float));

	return f;
}

int filter_tune(struct filter *f, float const low,float const high,float const kaiser_beta){

	if(isnan(low) || isnan(high) || isnan(kaiser_beta))
		return -1;

	float gain = 1./((float)f->N);

	for(int n = 0; n < f->N; n++){
		float s;
		//the first half is +ve frequencies in frequency domain
		if(n <= f->N/2)
			s = (float)n / f->N;
		else	//the second half is -ve frequencies, inverted
			s = (float)(n-f->N) / f->N;

		if(s >= low && s <= high)
			f->fir_coeff[n] = gain;
		else
			f->fir_coeff[n] = 0;
	}

	window_filter(f->L, f->M, f->fir_coeff, kaiser_beta);
	return 0;
}

void filter_print(struct filter *f){

	printf("#Filter windowed FIR frequency coefficients\n");
	for(int n=0;n<f->N;n++){
		printf("%d,%.17f,%.17f\n", n, crealf(f->fir_coeff[n]), cimagf(f->fir_coeff[n]));
	}
}
