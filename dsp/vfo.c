// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// vfo.c — a tone generator, one sample at a time.
//
// This is zbitxv2's vfo.c. It makes the audio tones the radio needs: the two
// tones of the two-tone test, the tune carrier, the AM carrier. It is not the
// oscillator that tunes the radio; that is the Si5351.
//
// A tone is a phase that advances by phase_increment with every sample and
// wraps at 65536, which is one full turn. The sample is the sine of the
// phase, looked up in phase_table. The table holds a quarter of a turn, and
// the other three quarters are the same numbers read backwards or negated.
//
// The phase_increment is the frequency times 65536 over the sampling
// frequency, in whole numbers, so a tone is within 1.5 Hz of what was asked.
//
// A sample is the sine times 2^30.

#include <math.h>
#include <complex.h>
#include <fftw3.h>

#include "sdr.h"

//we define one more than needed to cover the boundary of quadrature
static int phase_table[MAX_PHASE_COUNT];
int sampling_freq = 96000;

void vfo_init_phase_table(){
	for (int i = 0; i < MAX_PHASE_COUNT; i++){
		double d = (M_PI/2) * ((double)i)/((double)MAX_PHASE_COUNT);
		phase_table[i] = (int)(sin(d) * 1073741824.0);
	}
}

void vfo_start(struct vfo *v, int frequency_hz, int start_phase){
	v->phase_increment = (frequency_hz * 65536) / sampling_freq;
	v->phase = start_phase;
	v->freq_hz = frequency_hz;
}

int vfo_read(struct vfo *v){
	int i = 0;
	if (v->phase < 16384)
		i = phase_table[v->phase];
	else if (v->phase < 32768)
		i = phase_table[32767 - v->phase];
	else if (v->phase < 49152)
		i =  -phase_table[v->phase - 32768];
	else
		i = -phase_table[65535- v->phase];

	//increment the phase and modulo-65536
	v->phase += v->phase_increment;
	v->phase &= 0xffff;

	return i;
}
