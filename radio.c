// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// radio.c — the radio as its operator sees it: a frequency and the way it
// is tuned, a band, a mode, a volume, the IF gain and the drive.
//
// core_sdr.c is the first level: it does what it is told, a request at a
// time, and remembers nothing an operator would call a setting. This is the
// second level. Its controls are registered in the table of controls,
// control.c, in the groups RADIO and TUNING, and each one's apply turns a new value into
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
//   IF      the gain of the sound card's input, 0 to 100, which is where
//           the second IF comes in.
//   DRIVE   the transmitter's drive, 0 to 100. There is no transmitter yet,
//           so it is only kept.
//
// In the group TUNING, the frequency's own controls:
//
//   STEP       how far a turn of the knob tunes: 10H, 100H, 500H, 1K or
//              10K.
//   VFO        A or B. Each keeps a frequency of its own. Choosing the
//              other notes the frequency on the one left, and goes to the
//              other's. One not yet used opens where the other is.
//   RIT        OFF or ON. When it is on, the receiver listens at FREQ +
//              RIT_DELTA, while the transmitter stays on FREQ.
//   RIT_DELTA  the receiver's offset, -25000 to 25000 Hz.
//   SPLIT      OFF or ON. When it is on, the radio is to transmit on the
//              other VFO. Turning it on goes to VFO A, as zbitxv2 did, so
//              that it receives on A and transmits on B. There is no
//              transmitter yet, so it changes nothing else.
//   RX_FREQ    a reading: where the receiver listens, FREQ, or FREQ +
//              RIT_DELTA while RIT is on.
//   TX_FREQ    a reading: where the transmitter is to be, FREQ, or the
//              other VFO's frequency while SPLIT is on.
//
// TUNING WITH THE KNOB
//
// Every client tunes the same way, so the way is kept here and not in the
// clients. A client tells the radio how many steps its knob, or its mouse
// wheel, has turned, with radio_tune(); the protocol's "tune 1". The radio
// moves FREQ by that many STEPs, onto a multiple of STEP, as zbitxv2 did;
// or, while RIT is on, it moves RIT_DELTA by them instead, and FREQ stays
// where it is. What moved is told to every client, the one that turned
// included, since it did not know where it would end. To show the
// frequency, a client shows RX_FREQ, and TX_FREQ beside it when the two
// differ, which is so with RIT or SPLIT on; then every client shows RIT and
// SPLIT alike.
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
#define RIT_MAX_HZ 25000
#define VFO_A 0
#define VFO_B 1
#define VFOS 2
#define HZ_PER_K 1000
#define STARTING_HZ "7088000"
#define STARTING_BAND "40M"
#define STARTING_MODE "LSB"
#define STARTING_VOLUME "60"
#define STARTING_IF "60"
#define STARTING_DRIVE "40"
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

static int vfo_hz[VFOS];
static int vfo_in_use = VFO_A;

static void core_request(const char *key, const char *value) {
	char request[REQUEST_MAX];
	char answer[ANSWER_MAX];

	snprintf(request, sizeof(request), "%s=%s", key, value);
	sdr_request(request, answer);
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

static int vfo_index(const char *name) {
	if (!strcmp(name, "B"))
		return VFO_B;
	return VFO_A;
}

// the transmitter's frequency: the dial's, or while SPLIT is on the other
// VFO's, which, if that VFO has not been used, is the dial's too
static int transmit_hz(int dial_hz) {
	int other = VFO_B;

	if (strcmp(control_get("SPLIT"), "ON"))
		return dial_hz;
	if (vfo_in_use == VFO_B)
		other = VFO_A;
	if (vfo_hz[other] == 0)
		return dial_hz;
	return vfo_hz[other];
}

// the receiver is tuned to the dial's frequency, moved by RIT_DELTA while
// RIT is on, and RX_FREQ and TX_FREQ are told where the two are
static void receiver_tune(int dial_hz) {
	char hz[CONTROL_VALUE_MAX];
	int receive_hz = dial_hz;

	if (!strcmp(control_get("RIT"), "ON"))
		receive_hz += control_get_int("RIT_DELTA");
	snprintf(hz, sizeof(hz), "%d", receive_hz);
	core_request("r1:freq", hz);
	control_update("RX_FREQ", hz);
	snprintf(hz, sizeof(hz), "%d", transmit_hz(dial_hz));
	control_update("TX_FREQ", hz);
}

// a new frequency may be in another band; if it is, the band left notes
// where it was left, and BAND is told of the new one
static void follow_band(int hz) {
	int band = band_for_freq(hz);

	if (band != find_band(control_get("BAND"))) {
		save_band_state();
		control_update("BAND", band_name(band));
	}
}

static void select_modem(const char *mode) {
	const struct modem *m = modem_find(mode);

	if (m != NULL && m->select != NULL)
		m->select();
}

static int apply_mode(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	select_modem(value);
	core_request("r1:mode", value);
	return 0;
}

static int apply_freq(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	follow_band(atoi(value));
	receiver_tune(atoi(value));
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
	receiver_tune(bands[band].hz);
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

static struct control vfo;

static int apply_vfo(struct control *c, const char *value, char *why, int room) {
	int going_to = vfo_index(value);
	char hz[CONTROL_VALUE_MAX];

	(void)c;
	(void)why;
	(void)room;
	vfo_hz[vfo_in_use] = control_get_int("FREQ");
	if (vfo_hz[going_to] == 0)
		vfo_hz[going_to] = control_get_int("FREQ");
	vfo_in_use = going_to;
	snprintf(hz, sizeof(hz), "%d", vfo_hz[going_to]);
	follow_band(vfo_hz[going_to]);
	receiver_tune(vfo_hz[going_to]);
	control_update("FREQ", hz);
	return 0;
}

//control.c keeps the new value only after apply returns, and
//receiver_tune() reads RIT and RIT_DELTA from the controls, so the new
//value is kept here first
static int apply_rit(struct control *c, const char *value, char *why, int room) {
	(void)why;
	(void)room;
	snprintf(c->value, sizeof(c->value), "%s", value);
	receiver_tune(control_get_int("FREQ"));
	return 0;
}

static int apply_split(struct control *c, const char *value, char *why, int room) {
	(void)why;
	(void)room;
	snprintf(c->value, sizeof(c->value), "%s", value);
	if (!strcmp(value, "ON") && strcmp(control_get("VFO"), "A")) {
		apply_vfo(&vfo, "A", NULL, 0);
		control_update("VFO", "A");
		return 0;
	}
	receiver_tune(control_get_int("FREQ"));
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

static struct control if_gain = {
	.group = "RADIO", .name = "IF", .kind = CONTROL_NUMBER,
	.min = 0, .max = GAIN_MAX, .step = 1,
	.value = STARTING_IF, .apply = apply_if,
};

static struct control drive = {
	.group = "RADIO", .name = "DRIVE", .kind = CONTROL_NUMBER,
	.min = 0, .max = DRIVE_MAX, .step = 1,
	.value = STARTING_DRIVE,
};

static struct control step = {
	.group = "TUNING", .name = "STEP", .kind = CONTROL_CHOICE,
	.choices = "10H/100H/500H/1K/10K",
	.value = "1K",
};

static struct control vfo = {
	.group = "TUNING", .name = "VFO", .kind = CONTROL_CHOICE,
	.choices = "A/B",
	.value = "A", .apply = apply_vfo,
};

static struct control rit = {
	.group = "TUNING", .name = "RIT", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "OFF", .apply = apply_rit,
};

static struct control rit_delta = {
	.group = "TUNING", .name = "RIT_DELTA", .kind = CONTROL_NUMBER,
	.min = -RIT_MAX_HZ, .max = RIT_MAX_HZ, .step = 1,
	.value = "0", .apply = apply_rit,
};

static struct control split = {
	.group = "TUNING", .name = "SPLIT", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "OFF", .apply = apply_split,
};

static struct control rx_freq = {
	.group = "TUNING", .name = "RX_FREQ", .kind = CONTROL_READING,
	.value = STARTING_HZ,
};

static struct control tx_freq = {
	.group = "TUNING", .name = "TX_FREQ", .kind = CONTROL_READING,
	.value = STARTING_HZ,
};

static struct control *controls[] = {
	&freq, &band, &mode, &audio, &if_gain, &drive,
	&step, &vfo, &rit, &rit_delta, &split, &rx_freq, &tx_freq,
};

// STEP in Hz: 10H is 10, 1K is 1000
static int step_hz(void) {
	const char *text = control_get("STEP");
	int hz = atoi(text);

	if (strchr(text, 'K') != NULL)
		hz *= HZ_PER_K;
	return hz;
}

static void number_text(char *text, int value) {
	snprintf(text, CONTROL_VALUE_MAX, "%d", value);
}

// the change is made as the radio's own, so that everyone is told of it,
// the one who turned included
int radio_tune(int steps, char *why, int room) {
	char text[CONTROL_VALUE_MAX];
	int step = step_hz();
	int hz;

	if (!strcmp(control_get("RIT"), "ON")) {
		hz = control_get_int("RIT_DELTA") + steps * step;
		if (hz > RIT_MAX_HZ)
			hz = RIT_MAX_HZ;
		if (hz < -RIT_MAX_HZ)
			hz = -RIT_MAX_HZ;
		number_text(text, hz);
		return control_set("RIT_DELTA", text, ORIGIN_NONE, why, room);
	}
	hz = (control_get_int("FREQ") / step + steps) * step;
	if (hz > HIGHEST_HZ)
		hz = HIGHEST_HZ;
	if (hz < LOWEST_HZ)
		hz = LOWEST_HZ;
	number_text(text, hz);
	return control_set("FREQ", text, ORIGIN_NONE, why, room);
}

#define CONTROLS ((int)(sizeof(controls) / sizeof(controls[0])))

int radio_open(void) {
	int i;

	for (i = 0; i < CONTROLS; i++) {
		if (control_register(controls[i]) < 0)
			return -1;
	}
	receiver_tune(atoi(freq.value));
	apply_mode(NULL, mode.value, NULL, 0);
	core_request("r1:volume", audio.value);
	core_request("r1:gain", if_gain.value);
	return 0;
}
