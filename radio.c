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
//   MODE    USB, LSB, CW, CWR, AM, FT8, 2TONE or RADE2.
//   AUDIO   the volume, 0 to 100: 0 is silence, and 1 to 100 are spread
//           over the 40 dB of the sound card's range that are of use;
//           hw/audio.c says how.
//   IF      the gain of the sound card's input, 0 to 100, which is where
//           the second IF comes in.
//   DRIVE   the transmitter's drive, 0 to 100. The core scales it for the
//           band the radio is on.
//   MIC     the gain of the sound card's input from the microphone, 0 to
//           100. It is in the group VOICE, so that the panel and the
//           desktop's window show it in USB, LSB and AM and in no other
//           mode: it is of no use in CW or 2TONE.
//   AGC     how fast the receiver's gain follows the signal: OFF, SLOW, MED
//           or FAST, as in zbitxv2, which starts with SLOW.
//   TX      OFF or ON: whether the radio is transmitting. Setting it ON
//           goes to transmit, if the mode in use can transmit: USB, LSB,
//           CW, CWR and 2TONE can; AM cannot yet, and setting TX ON in it
//           is refused. Changing the mode or the band goes back to
//           receive first.
//
//   MYCALLSIGN, MYGRID   the station's callsign and its grid, as text, in a
//           group of their own, STATION. The macros put them in. They are
//           not yet remembered from one start to the next.
//
//   CLOCK, TIME_SET   the radio's time, in a group of their own, SYSTEM.
//           CLOCK is the time in UTC, to the minute, and where it comes
//           from: "2026-10-02 13:47 NTP", RTC or SET, or "NOT SET". The
//           radio says it afresh each minute, and it cannot be set.
//           TIME_SET is the time the operator gives when the radio has
//           none: minutes since the start of 2026, in UTC, taken as the
//           start of that minute at the moment it is set. hw/radio_clock.c
//           says where the time comes from and why it is kept as it is.
//
//   POWER, SWR, BATTERY   what is measured as the radio transmits, in a
//           group of their own, METER, so that no list of the radio's
//           controls shows them and they are not kept from one start to
//           the next. On the zbitx the panel reads the SWR bridge and the
//           battery, as zbitx's first panel did, and sends them five times
//           a second, when they change: the forward power in tenths of a
//           watt, the SWR in tenths, 13 for 1.3, and the battery in
//           hundredths of a volt. front_panel/meter.cpp says how they are
//           measured. On the sbitx the radio reads the bridge itself, and
//           meter.c says how; it has SMETER too, the signal's strength as
//           the radio receives. They are here, in the table, so that every
//           client is told of them as of any control; nothing in the radio
//           acts on them.
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
// A band has four such memories, its stack, as in zbitxv2, and is at one
// of them. STACK, 0 to 3, says which, and setting it goes to another:
// where the operator is now is kept in the one being left. The author's
// panel steps it when the button of the band already in use is pressed.
// The three places after the first begin much as zbitxv2's do: the bottom
// of the band and a second frequency for CW, and one for the digital modes
// in USB. They are not all zbitxv2's own figures, some of which sat on a
// band's upper edge or past it; the author is to set them as he wants them.
// STACK is in a group of its own, BAND, so that a client's list of the
// radio's controls does not show it. The places are kept from one start to
// the next: radio_save_bands() and radio_load_band().
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
#include <time.h>

#include "sdr.h"
#include "control.h"
#include "radio_clock.h"
#include "modem.h"
#include "radio.h"

#define LOWEST_HZ 500000
#define HIGHEST_HZ 30000000
#define VOLUME_MAX 100
#define STARTING_HZ "7088000"
#define STARTING_BAND "40M"
#define STARTING_MODE "LSB"
#define SIDEBAND_CHANGES_AT_HZ 10000000
#define STARTING_VOLUME "60"
#define STARTING_IF "60"
#define STARTING_DRIVE "40"
#define STARTING_MIC "40"
// zbitxv2 starts with the AGC slow
#define STARTING_AGC "SLOW"
#define GAIN_MAX 100
#define DRIVE_MAX 100
#define REQUEST_MAX 64
#define ANSWER_MAX 100

#define NO_BAND (-1)
#define BAND_LINE "BAND."

#define SECONDS_A_MINUTE 60
// TIME_SET runs to the end of the year 2100
#define TIME_SET_MAX_MINUTES 39445919
#define TIME_SAID_EVERY_MS 1000
#define POWER_MAX_TENTHS 999
#define SWR_MAX_TENTHS 999
#define BATTERY_MAX_HUNDREDTHS 9999

static long next_time_said_ms;

#define STACK_DEPTH 4

struct band_place {
	int hz;
	char mode[CONTROL_VALUE_MAX];
};

struct band {
	const char *name;
	int low_hz;
	int high_hz;
	int place;
	struct band_place stack[STACK_DEPTH];
};

// each band's first place is where it has always opened; the header says
// where the three after it come from
static struct band bands[] = {
	{ "80M",  3500000,  4000000, 0,
		{ {  3700000, "LSB" }, {  3500000, "CW" }, {  3574000, "LSB" }, {  3600000, "CW" } } },
	{ "60M",  5250000,  5450000, 0,
		{ {  5357000, "USB" }, {  5251500, "CW" }, {  5354000, "USB" }, {  5360000, "USB" } } },
	{ "40M",  7000000,  7300000, 0,
		{ {  7088000, "LSB" }, {  7000000, "CW" }, {  7040000, "CW" }, {  7074000, "USB" } } },
	{ "30M", 10100000, 10150000, 0,
		{ { 10136000, "USB" }, { 10100000, "CW" }, { 10110000, "CW" }, { 10150000, "USB" } } },
	{ "20M", 14000000, 14350000, 0,
		{ { 14200000, "USB" }, { 14000000, "CW" }, { 14040000, "CW" }, { 14074000, "USB" } } },
	{ "17M", 18068000, 18168000, 0,
		{ { 18130000, "USB" }, { 18068000, "CW" }, { 18100000, "CW" }, { 18110000, "USB" } } },
	{ "15M", 21000000, 21450000, 0,
		{ { 21300000, "USB" }, { 21100000, "CW" }, { 21040000, "CW" }, { 21074000, "USB" } } },
	{ "12M", 24890000, 24990000, 0,
		{ { 24950000, "USB" }, { 24890000, "CW" }, { 24910000, "CW" }, { 24915000, "USB" } } },
	{ "10M", 28000000, 29700000, 0,
		{ { 28500000, "USB" }, { 28000000, "CW" }, { 28040000, "CW" }, { 28074000, "USB" } } },
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
	struct band_place *place;

	if (band == NO_BAND)
		return;
	place = &bands[band].stack[bands[band].place];
	place->hz = control_get_int("FREQ");
	snprintf(place->mode, sizeof(place->mode), "%s", control_get("MODE"));
}

// which place of its stack the band in use is at, for the STACK control
static void tell_the_place(int band) {
	char place[CONTROL_VALUE_MAX];

	if (band == NO_BAND)
		return;
	snprintf(place, sizeof(place), "%d", bands[band].place);
	control_update("STACK", place);
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
	int was_lower = atoi(control_get("FREQ")) < SIDEBAND_CHANGES_AT_HZ;

	(void)c;
	(void)why;
	(void)room;
	if (band != find_band(control_get("BAND"))) {
		save_band_state();
		control_update("BAND", band_name(band));
		tell_the_place(band);
	}
	core_request("r1:freq", value);
	// RADE2 is on the sideband of the band, which the core chooses when it
	// is asked for the mode: it is asked again when the frequency has
	// crossed from the one to the other
	if (!strcmp(control_get("MODE"), "RADE2")
			&& was_lower != (atoi(value) < SIDEBAND_CHANGES_AT_HZ))
		core_request("r1:mode", "RADE2");
	return 0;
}

// the radio goes to the place the band's stack is at: its frequency, and
// its mode if that is another
static void go_to_the_place(int band) {
	const struct band_place *place = &bands[band].stack[bands[band].place];
	char hz[CONTROL_VALUE_MAX];

	snprintf(hz, sizeof(hz), "%d", place->hz);
	core_request("r1:freq", hz);
	control_update("FREQ", hz);
	if (strcmp(control_get("MODE"), place->mode)) {
		apply_mode(NULL, place->mode, NULL, 0);
		control_update("MODE", place->mode);
	}
}

static int apply_band(struct control *c, const char *value, char *why, int room) {
	int band = find_band(value);

	(void)c;
	(void)why;
	(void)room;
	stop_transmitting();
	save_band_state();
	go_to_the_place(band);
	tell_the_place(band);
	return 0;
}

// another place in the stack of the band in use: where the operator is now
// is kept in the place being left
static int apply_stack(struct control *c, const char *value, char *why, int room) {
	int band = find_band(control_get("BAND"));

	(void)c;
	if (band == NO_BAND) {
		snprintf(why, room, "the radio is not on a band");
		return -1;
	}
	stop_transmitting();
	save_band_state();
	bands[band].place = atoi(value);
	go_to_the_place(band);
	return 0;
}

// The bands' places are kept from one start to the next, with the rest of
// the settings; settings.c says when. Each band has a line:
//
//     BAND.40M=1 7088000:LSB 7000000:CW 7040000:CW 7074000:USB
//
// the place it is at, and the frequency and the mode of each of its places.
// The place the radio is at now is brought up to date first, since it is
// otherwise noted only when the band or the place is left.
void radio_save_bands(FILE *file) {
	int band;
	int place;

	save_band_state();
	for (band = 0; band < BANDS; band++) {
		fprintf(file, "%s%s=%d", BAND_LINE, bands[band].name, bands[band].place);
		for (place = 0; place < STACK_DEPTH; place++)
			fprintf(file, " %d:%s", bands[band].stack[place].hz,
				bands[band].stack[place].mode);
		fprintf(file, "\n");
	}
}

// takes a band's line, if it is one and all of it is right: every frequency
// within the band, and every mode one the radio has. Says whether it did
int radio_load_band(const char *name, const char *value) {
	struct band_place stack[STACK_DEPTH];
	const char *at = value;
	int used;
	int place;
	int band;
	int i;

	if (strncmp(name, BAND_LINE, strlen(BAND_LINE)))
		return 0;
	band = find_band(name + strlen(BAND_LINE));
	if (band == NO_BAND)
		return 0;
	if (sscanf(at, "%d%n", &place, &used) != 1 || place < 0 || place >= STACK_DEPTH)
		return 0;
	at += used;
	for (i = 0; i < STACK_DEPTH; i++) {
		if (sscanf(at, " %d:%31s%n", &stack[i].hz, stack[i].mode, &used) != 2)
			return 0;
		at += used;
		if (stack[i].hz < bands[band].low_hz || stack[i].hz > bands[band].high_hz
				|| modem_find(stack[i].mode) == NULL)
			return 0;
	}
	memcpy(bands[band].stack, stack, sizeof(stack));
	bands[band].place = place;
	if (band == find_band(control_get("BAND")))
		tell_the_place(band);
	return 1;
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

static int apply_agc(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	core_request("r1:agc", value);
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

static struct control stack = {
	.group = "BAND", .name = "STACK", .kind = CONTROL_NUMBER,
	.min = 0, .max = STACK_DEPTH - 1, .step = 1,
	.value = "0", .apply = apply_stack,
};

static struct control mode = {
	.group = "RADIO", .name = "MODE", .kind = CONTROL_CHOICE,
	.choices = "USB/LSB/CW/CWR/AM/FT8/2TONE/RADE2",
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
	.group = "VOICE", .name = "MIC", .kind = CONTROL_NUMBER,
	.min = 0, .max = GAIN_MAX, .step = 1,
	.value = STARTING_MIC, .apply = apply_mic,
};

static struct control agc = {
	.group = "RADIO", .name = "AGC", .kind = CONTROL_CHOICE,
	.choices = "OFF/SLOW/MED/FAST",
	.value = STARTING_AGC, .apply = apply_agc,
};

static struct control my_callsign = {
	.group = "STATION", .name = "MYCALLSIGN", .kind = CONTROL_TEXT,
};

static struct control my_grid = {
	.group = "STATION", .name = "MYGRID", .kind = CONTROL_TEXT,
};

// the radio's time is said afresh when it is asked for and each second
// after; control_update() tells the clients only when it has changed, which
// is each minute, or when where the time comes from changes
static void say_the_time(void) {
	static const char *const from[] = { "", "SET", "RTC", "NTP" };
	char text[CONTROL_VALUE_MAX];
	char when[CONTROL_VALUE_MAX];
	struct tm utc;
	time_t now;
	enum radio_clock_source source = radio_clock_now(&now);

	if (source == RADIO_CLOCK_NOT_SET)
		snprintf(text, sizeof(text), "NOT SET");
	else {
		gmtime_r(&now, &utc);
		strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &utc);
		snprintf(text, sizeof(text), "%.20s %s", when, from[source]);
	}
	control_update("CLOCK", text);
}

static int apply_time_set(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	radio_clock_set_by_hand((time_t)(RADIO_CLOCK_EARLIEST
		+ atoll(value) * SECONDS_A_MINUTE));
	next_time_said_ms = 0;
	return 0;
}

static int apply_clock(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)value;
	snprintf(why, room, "the clock is set with TIME_SET");
	return -1;
}

static struct control clock_control = {
	.group = "SYSTEM", .name = "CLOCK", .kind = CONTROL_TEXT,
	.value = "NOT SET", .apply = apply_clock,
};

static struct control time_set = {
	.group = "SYSTEM", .name = "TIME_SET", .kind = CONTROL_NUMBER,
	.min = 0, .max = TIME_SET_MAX_MINUTES, .step = 1,
	.value = "0", .apply = apply_time_set,
};

static struct control tx = {
	.group = "RADIO", .name = "TX", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "OFF", .apply = apply_tx,
};

static struct control power = {
	.group = "METER", .name = "POWER", .kind = CONTROL_NUMBER,
	.min = 0, .max = POWER_MAX_TENTHS, .step = 1, .value = "0",
};

static struct control swr = {
	.group = "METER", .name = "SWR", .kind = CONTROL_NUMBER,
	.min = 0, .max = SWR_MAX_TENTHS, .step = 1, .value = "0",
};

static struct control battery = {
	.group = "METER", .name = "BATTERY", .kind = CONTROL_NUMBER,
	.min = 0, .max = BATTERY_MAX_HUNDREDTHS, .step = 1, .value = "0",
};

int radio_open(void) {
	if (control_register(&freq) < 0 || control_register(&band) < 0
			|| control_register(&stack) < 0
			|| control_register(&mode) < 0 || control_register(&audio) < 0
			|| control_register(&if_gain) < 0 || control_register(&drive) < 0
			|| control_register(&mic) < 0 || control_register(&agc) < 0
			|| control_register(&tx) < 0 || control_register(&my_callsign) < 0
			|| control_register(&my_grid) < 0 || control_register(&clock_control) < 0
			|| control_register(&time_set) < 0 || control_register(&power) < 0
			|| control_register(&swr) < 0 || control_register(&battery) < 0)
		return -1;
	core_request("r1:freq", freq.value);
	apply_mode(NULL, mode.value, NULL, 0);
	core_request("r1:volume", audio.value);
	core_request("r1:gain", if_gain.value);
	core_request("tx_power", drive.value);
	core_request("tx_gain", mic.value);
	core_request("r1:agc", agc.value);
	say_the_time();
	return 0;
}

// what the radio does by itself as time passes: it says the time
void radio_poll(long now_ms) {
	if (now_ms < next_time_said_ms)
		return;
	next_time_said_ms = now_ms + TIME_SAID_EVERY_MS;
	say_the_time();
}
