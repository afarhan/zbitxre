// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_cw.c — CW and CWR.
//
// CW is received as one sideband, narrow, with the first oscillator set off
// by the pitch so that the signal is heard at the pitch; core_sdr.c does
// that when the mode is set. The audio goes to the speaker. Decoding the CW
// into text is to come here, and the keyer, for transmitting.

#include <stdint.h>

#include "sdr.h"
#include "modem.h"

static int32_t baseband_output[MAX_BINS/2];

static void rx_cw(fftw_complex *bins) {
	demod_ssb(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

const struct modem modem_cw = {
	.name = "CW",
	.order = 3,
	.rx = rx_cw,
};

const struct modem modem_cwr = {
	.name = "CWR",
	.order = 4,
	.rx = rx_cw,
};
