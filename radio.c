// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// radio.c — the radio as its operator sees it: a frequency, a band, a mode
// and a volume.
//
// core_sdr.c is the first level: it does what it is told, a request at a
// time, and remembers nothing an operator would call a setting. This is the
// second level. It keeps the settings, turns each into the requests the core
// needs, and says whenever one changes.
//
// Every change goes through radio_set(), whoever makes it: a desktop client,
// the front panel, the terminal. So all of them see the same radio. When a
// setting changes, the function given to radio_open() is called with its
// label and its new value, and whoever gave it passes the news on. When a
// change moves another setting, a new band moving the frequency, each that
// moved is told of.
//
// THE SETTINGS
//
//   FREQ    the frequency, in Hz, from 500 kHz to 30 MHz.
//   BAND    the amateur band the frequency is in: 80M, 60M, 40M, 30M, 20M,
//           17M, 15M, 12M or 10M, or OUT when it is in none. Setting BAND
//           goes to that band, to the frequency and mode last used on it.
//   MODE    USB, LSB, CW, CWR or AM.
//   AUDIO   the volume, 0 to 100.
//
// The labels are zbitxv2's, so that its clients and this radio say the same
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
// radio_set(), radio_get() and radio_send_all() are for the thread that owns
// the controls, the same thread that calls sdr_request(). They are not for
// the audio thread.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdr.h"
#include "radio.h"

#define LOWEST_HZ 500000
#define HIGHEST_HZ 30000000
#define VOLUME_MAX 100
#define STARTING_HZ 7088000
#define STARTING_VOLUME 60
#define SIDEBAND_TURNS_HZ 10000000
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
	char mode[RADIO_VALUE_MAX];
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

static const char *modes[] = { "USB", "LSB", "CW", "CWR", "AM" };

#define MODES ((int)(sizeof(modes) / sizeof(modes[0])))

static int hz_now;
static int volume_now;
static int band_now = NO_BAND;
static char mode_now[RADIO_VALUE_MAX];
static radio_notify_fn notify_fn;

static void core_request_int(const char *key, int value) {
	char request[REQUEST_MAX];
	char answer[ANSWER_MAX];

	snprintf(request, sizeof(request), "%s=%d", key, value);
	sdr_request(request, answer);
}

static void core_request(const char *key, const char *value) {
	char request[REQUEST_MAX];
	char answer[ANSWER_MAX];

	snprintf(request, sizeof(request), "%s=%s", key, value);
	sdr_request(request, answer);
}

static void notify(const char *label, const char *value) {
	if (notify_fn != NULL)
		notify_fn(label, value);
}

static void notify_int(const char *label, int value) {
	char text[RADIO_VALUE_MAX];

	snprintf(text, sizeof(text), "%d", value);
	notify(label, text);
}

static int band_for_freq(int hz) {
	int i;

	for (i = 0; i < BANDS; i++) {
		if (hz >= bands[i].low_hz && hz <= bands[i].high_hz)
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
	if (band_now == NO_BAND)
		return;
	bands[band_now].hz = hz_now;
	strcpy(bands[band_now].mode, mode_now);
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

static void change_mode(const char *mode) {
	strcpy(mode_now, mode);
	core_request("r1:mode", mode);
	set_passband(mode);
	notify("MODE", mode_now);
}

static void change_frequency(int hz) {
	int band = band_for_freq(hz);

	if (band != band_now) {
		save_band_state();
		band_now = band;
		notify("BAND", band_name(band_now));
	}
	hz_now = hz;
	core_request_int("r1:freq", hz_now);
	notify_int("FREQ", hz_now);
}

static void change_band(int band) {
	save_band_state();
	band_now = band;
	hz_now = bands[band].hz;
	core_request_int("r1:freq", hz_now);
	if (strcmp(mode_now, bands[band].mode))
		change_mode(bands[band].mode);
	notify("BAND", band_name(band_now));
	notify_int("FREQ", hz_now);
}

static int find_band(const char *name) {
	int i;

	for (i = 0; i < BANDS; i++) {
		if (!strcmp(bands[i].name, name))
			return i;
	}
	return NO_BAND;
}

static int is_valid_mode(const char *name) {
	int i;

	for (i = 0; i < MODES; i++) {
		if (!strcmp(modes[i], name))
			return 1;
	}
	return 0;
}

static int is_integer(const char *text) {
	char *end;

	if (*text == 0)
		return 0;
	strtol(text, &end, 10);
	return *end == 0;
}

static int set_error(char *why, int room, const char *reason) {
	snprintf(why, room, "%s", reason);
	return -1;
}

void radio_open(radio_notify_fn changed) {
	notify_fn = changed;
	volume_now = STARTING_VOLUME;
	core_request_int("r1:volume", volume_now);
	strcpy(mode_now, "");
	band_now = NO_BAND;
	change_frequency(STARTING_HZ);
	change_mode(bands[band_now].mode);
}

int radio_set(const char *label, const char *value, char *why, int room) {
	int number = atoi(value);

	if (!strcmp(label, "FREQ")) {
		if (!is_integer(value) || number < LOWEST_HZ || number > HIGHEST_HZ)
			return set_error(why, room, "a frequency from 500000 to 30000000 Hz");
		change_frequency(number);
		return 0;
	}
	if (!strcmp(label, "BAND")) {
		if (find_band(value) == NO_BAND)
			return set_error(why, room, "one of 80M 60M 40M 30M 20M 17M 15M 12M 10M");
		change_band(find_band(value));
		return 0;
	}
	if (!strcmp(label, "MODE")) {
		if (!is_valid_mode(value))
			return set_error(why, room, "one of USB LSB CW CWR AM");
		change_mode(value);
		return 0;
	}
	if (!strcmp(label, "AUDIO")) {
		if (!is_integer(value) || number < 0 || number > VOLUME_MAX)
			return set_error(why, room, "a volume from 0 to 100");
		volume_now = number;
		core_request_int("r1:volume", volume_now);
		notify_int("AUDIO", volume_now);
		return 0;
	}
	return set_error(why, room, "no such setting");
}

int radio_get(const char *label, char *value, int room) {
	if (!strcmp(label, "FREQ"))
		snprintf(value, room, "%d", hz_now);
	else if (!strcmp(label, "BAND"))
		snprintf(value, room, "%s", band_name(band_now));
	else if (!strcmp(label, "MODE"))
		snprintf(value, room, "%s", mode_now);
	else if (!strcmp(label, "AUDIO"))
		snprintf(value, room, "%d", volume_now);
	else
		return -1;
	return 0;
}

void radio_send_all(radio_notify_fn tell_one) {
	static const char *labels[] = { "FREQ", "BAND", "MODE", "AUDIO" };
	char value[RADIO_VALUE_MAX];
	unsigned i;

	for (i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
		radio_get(labels[i], value, sizeof(value));
		tell_one(labels[i], value);
	}
}
