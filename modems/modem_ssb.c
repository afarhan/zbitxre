// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_ssb.c — USB and LSB.
//
// The bins are demodulated as one sideband, and the audio goes to the
// speaker. Which sideband is the receiver's mode, set by sdr_request(), so
// the two modes are one function.
//
// To transmit, the microphone's samples are what is sent, at the level
// zbitxv2's tx_process() took them at. The core makes one sideband of them.
//
// The modem's own control is registered in its start(), in the group VOICE:
//
//   BW_VOICE  the width of the passband, 300 to 3000 Hz in steps of 50.
//
// As in zbitxv2, the voice modes keep one bandwidth between them and CW
// another, so that going from SSB to CW and back does not lose either. For
// SSB the passband's low edge stays at 300 Hz, below which a voice has
// little worth hearing, and the bandwidth moves the high edge: 300 to
// 300 + BW_VOICE. AM uses the same bandwidth, and modem_am.c says how.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"

#define VOICE_LOW_HZ 300
#define BW_VOICE_LOWEST 300
#define BW_VOICE_HIGHEST 3000
#define BW_VOICE_STEP 50

static int32_t baseband_output[MAX_BINS/2];

static void rx_ssb(complex float *bins) {
	demod_ssb(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

#define MIC_FULL_SCALE 2000000000.0

static void tx_ssb(const int32_t *mic, double *samples) {
	int i;

	for (i = 0; i < MAX_BINS/2; i++)
		samples[i] = (1.0 * mic[i]) / MIC_FULL_SCALE;
}

static void ssb_select(void) {
	set_passband(VOICE_LOW_HZ, VOICE_LOW_HZ + control_get_int("BW_VOICE"));
}

//a new bandwidth is put to use at once by whichever mode is in use, if
//it is one of those that use it
static int apply_to_mode_in_use(struct control *c, const char *value, char *why, int room) {
	const char *mode = control_get("MODE");
	const struct modem *m;

	(void)why;
	(void)room;
	if (mode == NULL)
		return 0;
	//control.c keeps the new value only after apply returns, and select()
	//reads it from the control, so it is kept here first
	snprintf(c->value, sizeof(c->value), "%s", value);
	m = modem_find(mode);
	if (m != NULL && m->select != NULL)
		m->select();
	return 0;
}

static struct control bw_voice = {
	.group = "VOICE", .name = "BW_VOICE", .kind = CONTROL_NUMBER,
	.min = BW_VOICE_LOWEST, .max = BW_VOICE_HIGHEST, .step = BW_VOICE_STEP,
	.value = "2200", .apply = apply_to_mode_in_use,
};

static void ssb_start(void) {
	control_register(&bw_voice);
}

const struct modem modem_usb = {
	.name = "USB",
	.order = 1,
	.start = ssb_start,
	.select = ssb_select,
	.rx = rx_ssb,
	.tx = tx_ssb,
};

const struct modem modem_lsb = {
	.name = "LSB",
	.order = 2,
	.select = ssb_select,
	.rx = rx_ssb,
	.tx = tx_ssb,
};
