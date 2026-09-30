// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_cw.c — CW and CWR.
//
// CW is received as one sideband, narrow, with the first oscillator set off
// by the pitch so that the signal is heard at the pitch; core_sdr.c does
// that when the mode is set. The audio goes to the speaker. Decoding the CW
// into text is to come here, and the keyer, for transmitting.
//
// The modem's own controls are registered in its start(), in the group CW:
//
//   WPM     the keyer's speed, in words a minute, 5 to 50.
//
// The keyer is not written yet, so a new speed is only kept.

#include <stdint.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"

static int32_t baseband_output[MAX_BINS/2];

static void rx_cw(fftw_complex *bins) {
	demod_ssb(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

#define WPM_LOWEST 5
#define WPM_HIGHEST 50

static struct control wpm = {
	.group = "CW", .name = "WPM", .kind = CONTROL_NUMBER,
	.min = WPM_LOWEST, .max = WPM_HIGHEST, .step = 1,
	.value = "20",
};

static void cw_start(void) {
	control_register(&wpm);
}

const struct modem modem_cw = {
	.name = "CW",
	.order = 3,
	.start = cw_start,
	.rx = rx_cw,
};

const struct modem modem_cwr = {
	.name = "CWR",
	.order = 4,
	.rx = rx_cw,
};
