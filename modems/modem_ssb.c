// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_ssb.c — USB and LSB.
//
// The bins are demodulated as one sideband, and the audio goes to the
// speaker. Which sideband is the receiver's mode, set by sdr_request(), so
// the two modes are one function.

#include <stdint.h>

#include "sdr.h"
#include "modem.h"

static int32_t baseband_output[MAX_BINS/2];

static void rx_ssb(fftw_complex *bins) {
	demod_ssb(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

const struct modem modem_usb = {
	.name = "USB",
	.order = 1,
	.rx = rx_ssb,
};

const struct modem modem_lsb = {
	.name = "LSB",
	.order = 2,
	.rx = rx_ssb,
};
