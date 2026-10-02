// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_ft8.c — FT8 messages are made into sound, and the decoder is to find
// them in it. It needs no radio.
//
// A slot of 15 seconds is filled with noise, and into it are put messages
// as ft8_lib encodes them, each at its own pitch, beginning half a second
// into the slot as a station's does. modems/ft8_monitor.c is given the
// slot and has to give the messages back, each at the pitch it was sent
// at. A slot of noise alone has to give none.
//
// The sound of a message is made as zbitxv2's sbitx_ft8_encode() made it,
// with synth_gfsk() and gfsk_pulse() from ft8_lib's example gen_ft8.c, by
// Kārlis Goba, YL3JG, MIT licence: the tone glides from one symbol to the
// next through a Gaussian filter, which is what keeps an FT8 signal narrow.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_lib/constants.h"
#include "ft8_lib/encode.h"
#include "ft8_lib/pack.h"
#include "ft8_monitor.h"

#define SLOT_SAMPLES (FT8_SAMPLE_RATE * 15)
#define STARTS_AT_SAMPLE (FT8_SAMPLE_RATE / 2)
#define FOUND_MAX 20
#define PITCH_WITHIN_HZ 4
#define NOISE_LEVEL 0.05f
#define STRONG 0.1f
#define WEAK 0.01f
#define NOWHERE (-1)

#define FT8_SYMBOL_BT 2.0f ///< symbol smoothing filter bandwidth factor (BT)
#define GFSK_CONST_K 5.336446f ///< == pi * sqrt(2 / log(2))
// a symbol is 0.16 second, which at 12000 samples a second is
#define SYMBOL_SAMPLES 1920
#define PULSE_SYMBOLS 3
#define RAMP_DIVISOR 8

static float slot[SLOT_SAMPLES];
static float wave[FT8_NN * SYMBOL_SAMPLES];
static float dphi[(FT8_NN + 2) * SYMBOL_SAMPLES];
static float pulse[PULSE_SYMBOLS * SYMBOL_SAMPLES];
static struct ft8_decoded found[FOUND_MAX];
static int found_count;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

/// Computes a GFSK smoothing pulse.
static void gfsk_pulse(int n_spsym, float symbol_bt, float *pulse) {
	float t;
	float arg1;
	float arg2;
	int i;

	for (i = 0; i < PULSE_SYMBOLS * n_spsym; ++i) {
		t = i / (float)n_spsym - 1.5f;
		arg1 = GFSK_CONST_K * symbol_bt * (t + 0.5f);
		arg2 = GFSK_CONST_K * symbol_bt * (t - 0.5f);
		pulse[i] = (erff(arg1) - erff(arg2)) / 2;
	}
}

/// Synthesize waveform data using GFSK phase shaping.
static void synth_gfsk(const uint8_t *symbols, int n_sym, float f0, float symbol_bt,
		int n_spsym, float *signal) {
	int n_wave = n_sym * n_spsym;
	float dphi_peak = 2 * M_PI / n_spsym;
	float phi = 0;
	float env;
	int n_ramp = n_spsym / RAMP_DIVISOR;
	int ib;
	int i;
	int j;
	int k;

	// Shift frequency up by f0
	for (i = 0; i < n_wave + 2 * n_spsym; ++i)
		dphi[i] = 2 * M_PI * f0 / FT8_SAMPLE_RATE;

	gfsk_pulse(n_spsym, symbol_bt, pulse);
	for (i = 0; i < n_sym; ++i) {
		ib = i * n_spsym;
		for (j = 0; j < PULSE_SYMBOLS * n_spsym; ++j)
			dphi[j + ib] += dphi_peak * symbols[i] * pulse[j];
	}

	// Add dummy symbols at beginning and end with tone values equal to 1st
	// and last symbol, respectively
	for (j = 0; j < 2 * n_spsym; ++j) {
		dphi[j] += dphi_peak * pulse[j + n_spsym] * symbols[0];
		dphi[j + n_sym * n_spsym] += dphi_peak * pulse[j] * symbols[n_sym - 1];
	}

	// Calculate and insert the audio waveform
	for (k = 0; k < n_wave; ++k) {
		signal[k] = sinf(phi);
		phi = fmodf(phi + dphi[k + n_spsym], 2 * M_PI);
	}

	// Apply envelope shaping to the first and last symbols
	for (i = 0; i < n_ramp; ++i) {
		env = (1 - cosf(2 * M_PI * i / (2 * n_ramp))) / 2;
		signal[i] *= env;
		signal[n_wave - 1 - i] *= env;
	}
}

static void fill_with_noise(void) {
	int i;

	srand(1);
	for (i = 0; i < SLOT_SAMPLES; i++)
		slot[i] = NOISE_LEVEL * (2.0f * rand() / RAND_MAX - 1.0f);
}

// a message is added to the slot, at a pitch and a strength
static int add_message(const char *text, int pitch_hz, float strength) {
	uint8_t packed[FTX_LDPC_K_BYTES];
	uint8_t tones[FT8_NN];
	char message[FT8_TEXT_ROOM];
	int i;

	snprintf(message, sizeof(message), "%s", text);
	if (pack77(message, packed) < 0)
		return -1;
	ft8_encode(packed, tones);
	synth_gfsk(tones, FT8_NN, (float)pitch_hz, FT8_SYMBOL_BT, SYMBOL_SAMPLES, wave);
	for (i = 0; i < FT8_NN * SYMBOL_SAMPLES; i++)
		slot[STARTS_AT_SAMPLE + i] += strength * wave[i];
	return 0;
}

static int find_message(const char *text) {
	int i;

	for (i = 0; i < found_count; i++) {
		if (!strcmp(found[i].text, text))
			return i;
	}
	return NOWHERE;
}

static int is_found_at(const char *text, int pitch_hz) {
	int which = find_message(text);

	if (which == NOWHERE)
		return 0;
	return abs(found[which].freq_hz - pitch_hz) <= PITCH_WITHIN_HZ;
}

int main(void) {
	int strong;
	int weak;
	int i;

	printf("ft8:\n");

	fill_with_noise();
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	expect(found_count == 0, "a slot of noise has no message in it");

	fill_with_noise();
	expect(add_message("CQ VU2ESE MK97", 1200, STRONG) == 0
		&& add_message("VU2XZ VU2ESE -12", 1750, STRONG) == 0
		&& add_message("K1ABC W9XYZ RR73", 437, WEAK) == 0,
		"three messages are made into sound");
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	for (i = 0; i < found_count; i++)
		printf("          %+3d dB %4d Hz %4.1f s  %s\n", found[i].snr, found[i].freq_hz,
			found[i].time_sec, found[i].text);
	expect(found_count == 3, "three are found, each once");
	expect(is_found_at("CQ VU2ESE MK97", 1200), "a CQ, at the pitch it was sent at");
	expect(is_found_at("VU2XZ VU2ESE -12", 1750), "a report, beside it");
	expect(is_found_at("K1ABC W9XYZ RR73", 437),
		"and one a tenth as strong, a fifth of the noise");
	strong = find_message("CQ VU2ESE MK97");
	weak = find_message("K1ABC W9XYZ RR73");
	expect(strong != NOWHERE && weak != NOWHERE && found[strong].snr > found[weak].snr,
		"the stronger is given the better signal to noise");

	fill_with_noise();
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	expect(found_count == 0, "the next slot, of noise, has nothing left over in it");

	if (checks_failed) {
		printf("ft8: %d checks FAILED\n", checks_failed);
		return 1;
	}
	printf("ft8: all checks passed\n");
	return 0;
}
