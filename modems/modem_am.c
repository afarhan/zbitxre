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
//
// To transmit, the carrier is a steady level and the microphone is added
// to it: (1 + m) times the carrier's level, m the microphone's sample over
// 200000000, held between -1 and 1, so that the envelope never goes
// through nothing nor past twice the carrier. These are zbitxv2's
// figures, and its carrier's level, a tone of its vfo.c over 50000000000;
// zbitxv2 held m only at -1.
//
// zbitxv2 made the carrier here as a tone at 24 kHz, where its receiver
// worked, and the core sent it where it was. zbitxre works at center_bin,
// which is 28.125 kHz on the zbitx, so the carrier is made at nothing, a
// steady level, and the core moves it up to center_bin as it moves a
// sideband, but keeps both sides of it: the level lands on center_bin as
// the carrier, and the speech either side of it as the two sidebands.
// core_sdr.c's tx_process() says how. The modem need not know where the
// radio works.

#include <stdint.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"

#define AM_LOW_HZ 300
// zbitxv2's: its vfo's tone, 2^30 at the peak, over 50000000000
#define AM_CARRIER (1073741824.0 / 50000000000.0)
#define AM_MIC_FULL_SCALE 200000000.0

static int32_t baseband_output[MAX_BINS/2];

static void rx_am(complex float *bins) {
	demod_am(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

static void tx_am(const int32_t *mic, double *samples) {
	int i;

	for (i = 0; i < MAX_BINS/2; i++) {
		double modulation = (1.0 * mic[i]) / AM_MIC_FULL_SCALE;

		if (modulation < -1.0)
			modulation = -1.0;
		if (modulation > 1.0)
			modulation = 1.0;
		samples[i] = (1.0 + modulation) * AM_CARRIER;
	}
}

static void am_select(void) {
	set_passband(AM_LOW_HZ, control_get_int("BW_VOICE"));
}

const struct modem modem_am = {
	.name = "AM",
	.is_spoken = 1,
	.order = 5,
	.select = am_select,
	.rx = rx_am,
	.tx = tx_am,
};
