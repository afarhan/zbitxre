// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_2tone.c — 2TONE, two steady tones for testing the transmitter.
//
// It is zbitxv2's 2TONE mode. It transmits two tones of equal strength,
// 700 Hz and 1900 Hz, as an upper sideband signal: two carriers 700 Hz and
// 1900 Hz above the frequency the radio is tuned to. With them the
// transmitter, its filters and the change between receive and transmit can
// be tried and measured with nothing else in the way, no microphone and no
// key; and two tones show, on a spectrum analyser, how linear the
// transmitter is. It needs no input: the tones begin when the radio goes
// to transmit and stop when it comes back.
//
// It receives as USB does, with the voice modes' bandwidth.

#include <stdint.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"

#define TONE_A_HZ 700
#define TONE_B_HZ 1900
#define VOICE_LOW_HZ 300
// zbitxv2 divides the sum of the two tones by this
#define TONES_DIVISOR 50000000000.0

static int32_t baseband_output[MAX_BINS/2];
static struct vfo tone_a;
static struct vfo tone_b;

static void rx_2tone(complex float *bins) {
	demod_ssb(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

static void tx_2tone(const int32_t *mic, double *samples) {
	int i;

	(void)mic;
	for (i = 0; i < MAX_BINS/2; i++)
		samples[i] = (1.0 * vfo_read(&tone_a) + 1.0 * vfo_read(&tone_b)) / TONES_DIVISOR;
}

static void select_2tone(void) {
	set_passband(VOICE_LOW_HZ, VOICE_LOW_HZ + control_get_int("BW_VOICE"));
}

static void start_2tone(void) {
	vfo_start(&tone_a, TONE_A_HZ, 0);
	vfo_start(&tone_b, TONE_B_HZ, 0);
}

const struct modem modem_2tone = {
	.name = "2TONE",
	.order = 6,
	.start = start_2tone,
	.select = select_2tone,
	.rx = rx_2tone,
	.tx = tx_2tone,
};
