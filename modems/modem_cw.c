// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_cw.c — CW and CWR.
//
// CW is received as one sideband, narrow, with the first oscillator set off
// by the pitch so that the signal is heard at the pitch; core_sdr.c does
// that with the pitch it is given. The audio goes to the speaker. Decoding
// the CW into text is to come here, and the keyer, for transmitting.
//
// The modem's own controls are registered in its start(), in the group CW:
//
//   WPM     the keyer's speed, in words a minute, 5 to 50.
//   PITCH   the tone a CW signal is heard at, 100 to 3000 Hz in steps of 10.
//   BW_CW   the width of the passband, 50 to 3000 Hz in steps of 50.
//
// As in zbitxv2, the passband is centred on the pitch: from PITCH - BW_CW/2
// to PITCH + BW_CW/2. Narrowing it closes in on the signal being heard, and
// a signal off the pitch by more than half the bandwidth is not heard. CW
// keeps a bandwidth of its own, apart from the voice modes'.
//
// The keyer is not written yet, so a new speed is only kept.

#include <stdint.h>
#include <stdio.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"

#define WPM_LOWEST 5
#define WPM_HIGHEST 50
#define PITCH_LOWEST 100
#define PITCH_HIGHEST 3000
#define PITCH_STEP 10
#define BW_CW_LOWEST 50
#define BW_CW_HIGHEST 3000
#define BW_CW_STEP 50
#define REQUEST_MAX 32
#define ANSWER_MAX 100

static int32_t baseband_output[MAX_BINS/2];

static void rx_cw(fftw_complex *bins) {
	demod_ssb(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

//the core moves the oscillator by the pitch only when the mode is set, as
//zbitxv2 did, so the mode is set again after this, and after a new pitch
static void cw_select(void) {
	char request[REQUEST_MAX];
	char answer[ANSWER_MAX];
	int pitch = control_get_int("PITCH");
	int half_width = control_get_int("BW_CW") / 2;

	snprintf(request, sizeof(request), "rx_pitch=%d", pitch);
	sdr_request(request, answer);
	set_passband(pitch - half_width, pitch + half_width);
}

static int apply_pitch(struct control *c, const char *value, char *why, int room) {
	const char *mode = control_get("MODE");
	char request[REQUEST_MAX];
	char answer[ANSWER_MAX];

	(void)why;
	(void)room;
	snprintf(c->value, sizeof(c->value), "%s", value);
	if (mode == NULL || modem_find(mode) == NULL || modem_find(mode)->select != cw_select)
		return 0;
	cw_select();
	snprintf(request, sizeof(request), "r1:mode=%s", mode);
	sdr_request(request, answer);
	return 0;
}

//a new bandwidth is put to use at once by whichever mode is in use, if it
//is one of those that use it
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

static struct control wpm = {
	.group = "CW", .name = "WPM", .kind = CONTROL_NUMBER,
	.min = WPM_LOWEST, .max = WPM_HIGHEST, .step = 1,
	.value = "20",
};

static struct control pitch = {
	.group = "CW", .name = "PITCH", .kind = CONTROL_NUMBER,
	.min = PITCH_LOWEST, .max = PITCH_HIGHEST, .step = PITCH_STEP,
	.value = "600", .apply = apply_pitch,
};

static struct control bw_cw = {
	.group = "CW", .name = "BW_CW", .kind = CONTROL_NUMBER,
	.min = BW_CW_LOWEST, .max = BW_CW_HIGHEST, .step = BW_CW_STEP,
	.value = "400", .apply = apply_to_mode_in_use,
};

static void cw_start(void) {
	control_register(&wpm);
	control_register(&pitch);
	control_register(&bw_cw);
}

const struct modem modem_cw = {
	.name = "CW",
	.order = 3,
	.start = cw_start,
	.select = cw_select,
	.rx = rx_cw,
};

const struct modem modem_cwr = {
	.name = "CWR",
	.order = 4,
	.select = cw_select,
	.rx = rx_cw,
};
