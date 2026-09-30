// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_am.c — AM.
//
// The bins are filtered around the carrier and rectified, and the audio goes
// to the speaker. core_sdr.c says why the filter is around the tuned bin.

#include <stdint.h>

#include "sdr.h"
#include "modem.h"

static int32_t baseband_output[MAX_BINS/2];

static void rx_am(fftw_complex *bins) {
	demod_am(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

const struct modem modem_am = {
	.name = "AM",
	.order = 5,
	.rx = rx_am,
};
