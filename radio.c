// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// radio.c — the radio as its operator sees it: a frequency, a band, a mode
// and a volume.
//
// core_sdr.c is the first level: it does what it is told, a request at a
// time, and remembers nothing an operator would call a setting. This is the
// second level. Its four controls are registered in the table of controls,
// control.c, in the group RADIO, and each one's apply turns a new value into
// the requests the core needs. control.c says how a change is made and who
// is told of it.
//
// THE CONTROLS
//
//   FREQ    the frequency, in Hz, from 500 kHz to 30 MHz.
//   BAND    the amateur band the frequency is in: 80M, 60M, 40M, 30M, 20M,
//           17M, 15M, 12M or 10M, or OUT when it is in none. Setting BAND
//           goes to that band, to the frequency and mode last used on it.
//   MODE    USB, LSB, CW, CWR or AM.
//   AUDIO   the volume, 0 to 100.
//
// The names are zbitxv2's, so that its clients and this radio say the same
// thing in the same words.
//
// A MEMORY FOR EACH BAND
//
// Each band remembers the frequency and the mode last used on it. Leaving a
// band, by tuning out of it or by choosing another, notes where the operator
// was; coming back to it returns there. Until a band has been used it opens
// at a frequency of its own, below 10 MHz in LSB and above it in USB, as is
// usual.
//
// THE PASSBAND FOLLOWS THE MODE
//
// The core keeps one passband and does not change it with the mode. Here a
// mode brings its own: 300 to 3000 Hz for SSB, 400 Hz wide around the pitch
// for CW, 3 kHz either side of the carrier for AM.
//
// This is all for the thread that owns the controls, the same thread that
// calls sdr_request().

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdr.h"
#include "control.h"
#include "radio.h"

#define LOWEST_HZ 500000
#define HIGHEST_HZ 30000000
#define VOLUME_MAX 100
#define STARTING_HZ "7088000"
#define STARTING_BAND "40M"
#define STARTING_MODE "LSB"
#define STARTING_VOLUME "60"
#define REQUEST_MAX 64
#define ANSWER_MAX 100

#define SSB_LOW_HZ 300
#define SSB_HIGH_HZ 3000
#define CW_PITCH_HZ 700
#define CW_HALF_WIDTH_HZ 200
#define AM_HALF_WIDTH_HZ 3000
#define AM_LOW_HZ 50

#define NO_BAND (-1)

struct band {
	const char *name;
	int low_hz;
	int high_hz;
	int hz;
	char mode[CONTROL_VALUE_MAX];
};

static struct band bands[] = {
	{ "80M",  3500000,  4000000,  3700000, "LSB" },
	{ "60M",  5250000,  5450000,  5357000, "USB" },
	{ "40M",  7000000,  7300000,  7088000, "LSB" },
	{ "30M", 10100000, 10150000, 10136000, "USB" },
	{ "20M", 14000000, 14350000, 14200000, "USB" },
	{ "17M", 18068000, 18168000, 18130000, "USB" },
	{ "15M", 21000000, 21450000, 21300000, "USB" },
	{ "12M", 24890000, 24990000, 24950000, "USB" },
	{ "10M", 28000000, 29700000, 28500000, "USB" },
};

#define BANDS ((int)(sizeof(bands) / sizeof(bands[0])))

static void core_request(const char *key, const char *value) {
	char request[REQUEST_MAX];
	char answer[ANSWER_MAX];

	snprintf(request, sizeof(request), "%s=%s", key, value);
	sdr_request(request, answer);
}

static void core_request_int(const char *key, int value) {
	char text[CONTROL_VALUE_MAX];

	snprintf(text, sizeof(text), "%d", value);
	core_request(key, text);
}

static int band_for_freq(int hz) {
	int i;

	for (i = 0; i < BANDS; i++) {
		if (hz >= bands[i].low_hz && hz <= bands[i].high_hz)
			return i;
	}
	return NO_BAND;
}

static int find_band(const char *name) {
	int i;

	for (i = 0; i < BANDS; i++) {
		if (!strcmp(bands[i].name, name))
			return i;
	}
	return NO_BAND;
}

static const char *band_name(int band) {
	if (band == NO_BAND)
		return "OUT";
	return bands[band].name;
}

static void save_band_state(void) {
	int band = find_band(control_get("BAND"));

	if (band == NO_BAND)
		return;
	bands[band].hz = control_get_int("FREQ");
	snprintf(bands[band].mode, sizeof(bands[band].mode), "%s", control_get("MODE"));
}

static void set_passband(const char *mode) {
	if (!strcmp(mode, "CW") || !strcmp(mode, "CWR")) {
		core_request_int("rx_pitch", CW_PITCH_HZ);
		core_request_int("r1:low", CW_PITCH_HZ - CW_HALF_WIDTH_HZ);
		core_request_int("r1:high", CW_PITCH_HZ + CW_HALF_WIDTH_HZ);
		return;
	}
	if (!strcmp(mode, "AM")) {
		core_request_int("r1:low", AM_LOW_HZ);
		core_request_int("r1:high", AM_HALF_WIDTH_HZ);
		return;
	}
	core_request_int("r1:low", SSB_LOW_HZ);
	core_request_int("r1:high", SSB_HIGH_HZ);
}

static int apply_mode(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	core_request("r1:mode", value);
	set_passband(value);
	return 0;
}

static int apply_freq(struct control *c, const char *value, char *why, int room) {
	int band = band_for_freq(atoi(value));

	(void)c;
	(void)why;
	(void)room;
	if (band != find_band(control_get("BAND"))) {
		save_band_state();
		control_update("BAND", band_name(band));
	}
	core_request("r1:freq", value);
	return 0;
}

static int apply_band(struct control *c, const char *value, char *why, int room) {
	int band = find_band(value);
	char hz[CONTROL_VALUE_MAX];

	(void)c;
	(void)why;
	(void)room;
	save_band_state();
	snprintf(hz, sizeof(hz), "%d", bands[band].hz);
	core_request("r1:freq", hz);
	control_update("FREQ", hz);
	if (strcmp(control_get("MODE"), bands[band].mode)) {
		apply_mode(NULL, bands[band].mode, NULL, 0);
		control_update("MODE", bands[band].mode);
	}
	return 0;
}

static int apply_audio(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	core_request("r1:volume", value);
	return 0;
}

static struct control freq = {
	.group = "RADIO", .name = "FREQ", .kind = CONTROL_NUMBER,
	.min = LOWEST_HZ, .max = HIGHEST_HZ, .step = 1,
	.value = STARTING_HZ, .apply = apply_freq,
};

static struct control band = {
	.group = "RADIO", .name = "BAND", .kind = CONTROL_CHOICE,
	.choices = "80M/60M/40M/30M/20M/17M/15M/12M/10M",
	.value = STARTING_BAND, .apply = apply_band,
};

static struct control mode = {
	.group = "RADIO", .name = "MODE", .kind = CONTROL_CHOICE,
	.choices = "USB/LSB/CW/CWR/AM",
	.value = STARTING_MODE, .apply = apply_mode,
};

static struct control audio = {
	.group = "RADIO", .name = "AUDIO", .kind = CONTROL_NUMBER,
	.min = 0, .max = VOLUME_MAX, .step = 1,
	.value = STARTING_VOLUME, .apply = apply_audio,
};

int radio_open(void) {
	if (control_register(&freq) < 0 || control_register(&band) < 0
			|| control_register(&mode) < 0 || control_register(&audio) < 0)
		return -1;
	core_request("r1:freq", freq.value);
	apply_mode(NULL, mode.value, NULL, 0);
	core_request("r1:volume", audio.value);
	return 0;
}
