// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_am.c — AM.
//
// The bins are filtered around the carrier and rectified, and the audio goes
// to the speaker. core_sdr.c says why the filter is around the tuned bin.
//
// AM uses the voice modes' bandwidth, BW_VOICE, which modem_ssb.c keeps.
// Its passband is zbitxv2's: from 300 Hz to BW_VOICE, either side of the
// carrier.

#include <stdint.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"

#define AM_LOW_HZ 300

static int32_t baseband_output[MAX_BINS/2];

static void rx_am(complex float *bins) {
	demod_am(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

static void am_select(void) {
	set_passband(AM_LOW_HZ, control_get_int("BW_VOICE"));
}

const struct modem modem_am = {
	.name = "AM",
	.order = 5,
	.select = am_select,
	.rx = rx_am,
};
