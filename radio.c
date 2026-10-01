// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// radio.c — the radio as its operator sees it: a frequency, a band, a mode,
// a volume, the IF gain, the microphone's gain, the drive, and whether it
// is transmitting.
//
// core_sdr.c is the first level: it does what it is told, a request at a
// time, and remembers nothing an operator would call a setting. This is the
// second level. Its controls are registered in the table of controls,
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
//   MODE    USB, LSB, CW, CWR, AM or 2TONE.
//   AUDIO   the volume, 0 to 100: 0 is silence, and 1 to 100 are spread
//           over the 40 dB of the sound card's range that are of use;
//           hw/audio.c says how.
//   IF      the gain of the sound card's input, 0 to 100, which is where
//           the second IF comes in.
//   DRIVE   the transmitter's drive, 0 to 100. The core scales it for the
//           band the radio is on.
//   MIC     the gain of the sound card's input from the microphone, 0 to
//           100.
//   TX      OFF or ON: whether the radio is transmitting. Setting it ON
//           goes to transmit, if the mode in use can transmit: USB, LSB,
//           CW, CWR and 2TONE can; AM cannot yet, and setting TX ON in it
//           is refused. Changing the mode or the band goes back to
//           receive first.
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
// The core keeps one passband and does not change it with the mode. Each
// mode brings its own: when a mode is chosen, its modem's select() is
// called, and sets the passband from the modem's own controls. SSB and AM
// keep one bandwidth, CW another, as zbitxv2 did; modems/modem_ssb.c and
// modems/modem_cw.c say how each is used.
//
// This is all for the thread that owns the controls, the same thread that
// calls sdr_request().

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"
#include "radio.h"

#define LOWEST_HZ 500000
#define HIGHEST_HZ 30000000
#define VOLUME_MAX 100
#define STARTING_HZ "7088000"
#define STARTING_BAND "40M"
#define STARTING_MODE "LSB"
#define STARTING_VOLUME "60"
#define STARTING_IF "60"
#define STARTING_DRIVE "40"
#define STARTING_MIC "40"
#define GAIN_MAX 100
#define DRIVE_MAX 100
#define REQUEST_MAX 64
#define ANSWER_MAX 100

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

static int core_request(const char *key, const char *value) {
	char request[REQUEST_MAX];
	char answer[ANSWER_MAX];

	snprintf(request, sizeof(request), "%s=%s", key, value);
	sdr_request(request, answer);
	return !strcmp(answer, "ok");
}

// back to receive, if the radio is transmitting
static void stop_transmitting(void) {
	if (strcmp(control_get("TX"), "ON"))
		return;
	core_request("tx", "off");
	control_update("TX", "OFF");
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

static void select_modem(const char *mode) {
	const struct modem *m = modem_find(mode);

	if (m != NULL && m->select != NULL)
		m->select();
}

// The mode is set in the core, then the modem sets the receiver for it, and
// then the mode is set once more. The order matters twice over. The core
// builds the receiver's filter when the passband is set, on the side of
// the tuned frequency that the mode it then has listens to; so the mode
// has to be set before the modem sets the passband, or the filter is left
// on the last mode's side, the sideband the new mode has cut off, and all
// that is heard is the AGC at full gain on what little leaks through. And
// the core moves the oscillator by CW's pitch only when the mode is set;
// so the mode is set again after the modem has set the pitch.
static int apply_mode(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	stop_transmitting();
	core_request("r1:mode", value);
	select_modem(value);
	core_request("r1:mode", value);
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
	stop_transmitting();
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

static int apply_if(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	core_request("r1:gain", value);
	return 0;
}

static int apply_drive(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	core_request("tx_power", value);
	return 0;
}

static int apply_mic(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	core_request("tx_gain", value);
	return 0;
}

static int apply_tx(struct control *c, const char *value, char *why, int room) {
	(void)c;
	if (!strcmp(value, "OFF")) {
		core_request("tx", "off");
		return 0;
	}
	if (core_request("tx", "on"))
		return 0;
	snprintf(why, room, "this mode does not transmit");
	return -1;
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
	.choices = "USB/LSB/CW/CWR/AM/2TONE",
	.value = STARTING_MODE, .apply = apply_mode,
};

static struct control audio = {
	.group = "RADIO", .name = "AUDIO", .kind = CONTROL_NUMBER,
	.min = 0, .max = VOLUME_MAX, .step = 1,
	.value = STARTING_VOLUME, .apply = apply_audio,
};

static struct control if_gain = {
	.group = "RADIO", .name = "IF", .kind = CONTROL_NUMBER,
	.min = 0, .max = GAIN_MAX, .step = 1,
	.value = STARTING_IF, .apply = apply_if,
};

static struct control drive = {
	.group = "RADIO", .name = "DRIVE", .kind = CONTROL_NUMBER,
	.min = 0, .max = DRIVE_MAX, .step = 1,
	.value = STARTING_DRIVE, .apply = apply_drive,
};

static struct control mic = {
	.group = "RADIO", .name = "MIC", .kind = CONTROL_NUMBER,
	.min = 0, .max = GAIN_MAX, .step = 1,
	.value = STARTING_MIC, .apply = apply_mic,
};

static struct control tx = {
	.group = "RADIO", .name = "TX", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "OFF", .apply = apply_tx,
};

int radio_open(void) {
	if (control_register(&freq) < 0 || control_register(&band) < 0
			|| control_register(&mode) < 0 || control_register(&audio) < 0
			|| control_register(&if_gain) < 0 || control_register(&drive) < 0
			|| control_register(&mic) < 0 || control_register(&tx) < 0)
		return -1;
	core_request("r1:freq", freq.value);
	apply_mode(NULL, mode.value, NULL, 0);
	core_request("r1:volume", audio.value);
	core_request("r1:gain", if_gain.value);
	core_request("tx_power", drive.value);
	core_request("tx_gain", mic.value);
	return 0;
}
