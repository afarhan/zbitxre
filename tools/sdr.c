// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// sdr.c — the radio, run from a terminal, with its front panel.
//
// This opens the radio, tunes it, and then does what it is told, from three
// places at once: the terminal, the front panel, and clients on the network.
//
// The radio's settings are controls, in control.c's table: the radio's own,
// registered by radio.c, and each modem's. Every change goes through
// control_set(), wherever it comes from, and is passed on to everyone who
// did not make it: to each network client, to the front panel, and printed
// on the terminal. So a band chosen on the desktop moves the panel's dial,
// and a turn of the panel's knob moves the desktop's.
//
// Clients connect on port 8081. server.c says what they say.
//
// From the terminal it takes a setting as a client sends it, "FREQ 7074000",
// or a request in the words of sdr_request(), "r1:agc=FAST", which goes to
// the core as it is. A frequency or volume set that way, r1:freq= or
// r1:volume=, goes through the controls like any other change.
//
// From the panel it takes the frequency and the volume, over the link that
// hw/panel.c keeps. When the panel says HELLO, which it does when it starts,
// it is told the frequency and the volume.
//
// Four times a second it sends the panel a line of the spectrum, for the
// panel's graph and waterfall. The line is 240 points across 25 kHz, the
// tuned frequency in the middle, as zbitxv2 shows it: each point is the
// strongest of the two or three bins it stands for, in dB, with 30 added so
// that the noise sits a little above the bottom. spectrum_plot runs from
// the highest frequency down, so the line is turned round to have the
// lowest frequency first.
//
// Ten times a second, while a network client has asked for it, it sends the
// same spectrum at the full resolution of the bins, 534 points, one to a
// bin. server.c says how.
//
// Some words are the program's own. "report" prints how the sound card and
// the link have been doing. "panel=off" stops the radio polling the panel
// and "panel=on" starts it again. "spectrum=off" stops the spectrum being
// sent and "spectrum=on" starts it again. They are there so that the noise
// each makes in the receiver can be told by ear from the rest. "quit", or
// the end of the input, closes the radio.
//
// "vfo=off" and "bfo=off" switch off the radio's first oscillator and its
// second, and "vfo=on" and "bfo=on" switch them on again. They are for
// finding where a noise gets into the receiver. With both off, nothing from
// the antenna or the first IF can reach the sound card, so a noise that is
// still heard is getting in at the second IF or after it. While an
// oscillator is off, tuning switches it on again, since tuning sets it.
// Switching one on resets the chip's PLLs, as the radio does when it starts.
//
// "measure=5" measures for five seconds how strong what reaches the sound
// card is, within 3 kHz either side of the tuned frequency, and then prints
// it in dB. It looks at the spectrum twenty times a second and averages the
// power in each bin. It prints the mean over the bins, and the middle one
// of them by size, which a few steady carriers do not move. With both
// oscillators off, what is measured is what the radio makes for itself, and
// that is how the panel's hash is given a number. The number is for
// comparing one setting with another; it is not referred to any standard.
//
//     sdr [hz] [mode] [volume]
//
// Without arguments it opens as radio.c opens, on 7088 kHz, LSB. The
// passband follows the mode.
//
// The program waits on the terminal for a few milliseconds at a time and
// looks to the panel in between. hw/panel.c decides whether it is time to
// poll.
//
// The first run takes a while to start, because the transforms are timed to
// find the fastest way of doing them. What it learns is kept in data/ and
// later runs start at once.

#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "board.h"
#include "panel.h"
#include "control.h"
#include "radio.h"
#include "server.h"
#include "si5351.h"
#include "sdr.h"

#define BFO_CLOCK       1
#define VFO_CLOCK       2

#define LINE_MAX_LENGTH 128
#define RESPONSE_MAX    100

#define TERMINAL        0
#define WAIT_ON_TERMINAL_MS 5
#define MS_PER_SECOND   1000
#define NS_PER_MS       1000000

#define FREQ_IS         "r1:freq="
#define VOLUME_IS       "r1:volume="

#define SPECTRUM_EVERY_MS 250
#define SPECTRUM_FIRST_BIN 1269
#define SPECTRUM_BINS   534
#define SPECTRUM_ADDED_DB 30
#define SPECTRUM_HIGHEST 255
#define SPECTRUM_HZ_PER_BIN 46.875
#define CLIENT_SPECTRUM_EVERY_MS 100

// what is measured: 3 kHz either side of the tuned frequency, which is bin
// 1536 of the spectrum, a bin being 46.875 Hz
#define MEASURE_FOR     "measure="
#define MEASURED_FIRST_BIN 1472
#define MEASURED_BINS   128
#define MEASURE_EVERY_MS 50
#define SMALLEST_POWER  1e-30

static int is_done;
static int is_sending_spectrum = 1;
static long next_spectrum_ms;
static uint8_t spectrum_line[LINK_SPECTRUM_POINTS];
static long next_client_spectrum_ms;
static uint8_t client_spectrum_line[SPECTRUM_BINS];

static double measured_power[MEASURED_BINS];
static int measured_times;
static int is_measuring;
static long next_measure_ms;
static long measure_until_ms;

static long now_ms(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * MS_PER_SECOND + now.tv_nsec / NS_PER_MS;
}

static void report(void) {
	struct audio_report sound;
	struct panel_report link;

	audio_get_report(&sound);
	printf("blocks %lu, gaps %lu, dropped %lu, overflows %lu, "
		"longest wait %ld us, queued %ld frames\n",
		sound.blocks, sound.playback_gaps, sound.blocks_dropped,
		sound.capture_overruns, sound.longest_wait_us, sound.queued_frames);
	if (!sound.is_realtime)
		printf("the audio thread did not get real-time priority\n");
	if (!sound.is_running)
		printf("the audio thread has stopped\n");

	panel_get_report(&link);
	printf("panel: polls %lu, answers %lu, no answer %lu, spoiled %lu, "
		"repeated %lu, words %lu, not taken %lu\n",
		link.polls, link.answers, link.no_answers, link.frames_spoiled,
		link.frames_repeated, link.words, link.words_not_taken);
	if (!link.is_polling)
		printf("the panel is not being polled\n");
	else if (!link.is_present)
		printf("the panel does not answer\n");
}

static int max_bin_level(int first_bin, int after_last_bin) {
	int strongest = spectrum_plot[first_bin];
	int bin;

	for (bin = first_bin + 1; bin < after_last_bin; bin++) {
		if (spectrum_plot[bin] > strongest)
			strongest = spectrum_plot[bin];
	}
	return strongest;
}

static uint8_t spectrum_level(int bin) {
	int level = spectrum_plot[bin] + SPECTRUM_ADDED_DB;

	if (level < 0)
		level = 0;
	if (level > SPECTRUM_HIGHEST)
		level = SPECTRUM_HIGHEST;
	return (uint8_t)level;
}

static void spectrum_line_fill(void) {
	int first_bin;
	int after_last_bin;
	int level;
	int point;

	for (point = 0; point < LINK_SPECTRUM_POINTS; point++) {
		first_bin = SPECTRUM_FIRST_BIN
			+ (point * SPECTRUM_BINS) / LINK_SPECTRUM_POINTS;
		after_last_bin = SPECTRUM_FIRST_BIN
			+ ((point + 1) * SPECTRUM_BINS) / LINK_SPECTRUM_POINTS;
		level = max_bin_level(first_bin, after_last_bin) + SPECTRUM_ADDED_DB;
		if (level < 0)
			level = 0;
		if (level > SPECTRUM_HIGHEST)
			level = SPECTRUM_HIGHEST;
		spectrum_line[LINK_SPECTRUM_POINTS - 1 - point] = (uint8_t)level;
	}
}

static void spectrum_send(long now) {
	if (!is_sending_spectrum || now < next_spectrum_ms)
		return;
	next_spectrum_ms = now + SPECTRUM_EVERY_MS;
	spectrum_line_fill();
	panel_set_bytes(LINK_SPECTRUM, spectrum_line, sizeof(spectrum_line));
}

static void client_spectrum_send(long now) {
	int bin;

	if (now < next_client_spectrum_ms || !server_wants_spectrum())
		return;
	next_client_spectrum_ms = now + CLIENT_SPECTRUM_EVERY_MS;
	for (bin = 0; bin < SPECTRUM_BINS; bin++)
		client_spectrum_line[SPECTRUM_BINS - 1 - bin]
			= spectrum_level(SPECTRUM_FIRST_BIN + bin);
	server_send_spectrum((int)(SPECTRUM_BINS * SPECTRUM_HZ_PER_BIN),
		client_spectrum_line, SPECTRUM_BINS);
}

static void measure_start(int seconds, long now) {
	int i;

	for (i = 0; i < MEASURED_BINS; i++)
		measured_power[i] = 0;
	measured_times = 0;
	next_measure_ms = now;
	measure_until_ms = now + seconds * MS_PER_SECOND;
	is_measuring = 1;
}

static int compare_doubles(const void *a, const void *b) {
	double first = *(const double *)a;
	double second = *(const double *)b;

	if (first < second)
		return -1;
	if (first > second)
		return 1;
	return 0;
}

static void print_measurement(void) {
	double mean = 0;
	int i;

	for (i = 0; i < MEASURED_BINS; i++) {
		measured_power[i] /= measured_times;
		mean += measured_power[i];
	}
	mean /= MEASURED_BINS;
	qsort(measured_power, MEASURED_BINS, sizeof(double), compare_doubles);
	printf("measured: mean %.1f dB, middle %.1f dB, over %d bins and %d looks\n",
		10 * log10(mean + SMALLEST_POWER),
		10 * log10(measured_power[MEASURED_BINS / 2] + SMALLEST_POWER),
		MEASURED_BINS, measured_times);
	fflush(stdout);
}

static void measure_update(long now) {
	int i;

	if (!is_measuring || now < next_measure_ms)
		return;
	if (now >= measure_until_ms) {
		is_measuring = 0;
		print_measurement();
		return;
	}
	next_measure_ms = now + MEASURE_EVERY_MS;
	for (i = 0; i < MEASURED_BINS; i++)
		measured_power[i] += (double)fft_bins[MEASURED_FIRST_BIN + i]
			* fft_bins[MEASURED_FIRST_BIN + i];
	measured_times++;
}

static void on_control_changed(const struct control *c, int origin) {
	server_broadcast(c, origin);
	if (origin != ORIGIN_PANEL && !strcmp(c->name, "FREQ"))
		panel_set(LINK_FREQ, atoi(c->value));
	if (origin != ORIGIN_PANEL && !strcmp(c->name, "AUDIO"))
		panel_set(LINK_VOLUME, atoi(c->value));
	printf("%s %s\n", c->name, c->value);
	fflush(stdout);
}

static void panel_send_settings(void) {
	panel_set(LINK_FREQ, control_get_int("FREQ"));
	panel_set(LINK_VOLUME, control_get_int("AUDIO"));
}

static void set_setting(const char *name, const char *value, int origin) {
	char why[CONTROL_WHY_MAX];

	if (control_set(name, value, origin, why, sizeof(why)) < 0)
		printf("ERROR %s %s\n", name, why);
}

static void on_panel_record(int control, int32_t value) {
	char text[CONTROL_VALUE_MAX];

	snprintf(text, sizeof(text), "%d", (int)value);
	if (control == LINK_HELLO) {
		printf("panel: HELLO, version %d\n", (int)value);
		panel_send_settings();
	}
	if (control == LINK_FREQ)
		set_setting("FREQ", text, ORIGIN_PANEL);
	if (control == LINK_VOLUME)
		set_setting("AUDIO", text, ORIGIN_PANEL);
	fflush(stdout);
}

static int begins_with(const char *line, const char *with) {
	return strncmp(line, with, strlen(with)) == 0;
}

static void handle_command(char *line) {
	char response[RESPONSE_MAX];

	if (line[0] == 0)
		return;
	if (strcmp(line, "quit") == 0) {
		is_done = 1;
		return;
	}
	if (strcmp(line, "report") == 0) {
		report();
		return;
	}
	if (strcmp(line, "panel=off") == 0) {
		panel_set_polling(0);
		printf("ok\n");
		return;
	}
	if (strcmp(line, "panel=on") == 0) {
		panel_set_polling(1);
		printf("ok\n");
		return;
	}
	if (strcmp(line, "spectrum=off") == 0) {
		is_sending_spectrum = 0;
		printf("ok\n");
		return;
	}
	if (strcmp(line, "spectrum=on") == 0) {
		is_sending_spectrum = 1;
		printf("ok\n");
		return;
	}
	if (begins_with(line, MEASURE_FOR)) {
		measure_start(atoi(line + strlen(MEASURE_FOR)), now_ms());
		printf("ok\n");
		return;
	}
	if (strcmp(line, "vfo=off") == 0) {
		si5351_clock_off(VFO_CLOCK);
		printf("ok\n");
		return;
	}
	if (strcmp(line, "vfo=on") == 0) {
		si5351_set_freq(VFO_CLOCK, oscillator_frequency());
		si5351_reset_plls();
		printf("ok\n");
		return;
	}
	if (strcmp(line, "bfo=off") == 0) {
		si5351_clock_off(BFO_CLOCK);
		printf("ok\n");
		return;
	}
	if (strcmp(line, "bfo=on") == 0) {
		si5351_set_freq(BFO_CLOCK, bfo_freq);
		si5351_reset_plls();
		printf("ok\n");
		return;
	}
	if (begins_with(line, FREQ_IS)) {
		set_setting("FREQ", line + strlen(FREQ_IS), ORIGIN_TERMINAL);
		return;
	}
	if (begins_with(line, VOLUME_IS)) {
		set_setting("AUDIO", line + strlen(VOLUME_IS), ORIGIN_TERMINAL);
		return;
	}
	if (strchr(line, '=') == NULL && strchr(line, ' ') != NULL) {
		char *value = strchr(line, ' ');
		*value = 0;
		set_setting(line, value + 1, ORIGIN_TERMINAL);
		return;
	}
	sdr_request(line, response);
	printf("%s\n", response);
}

static void read_terminal(void) {
	static char line[LINE_MAX_LENGTH];
	static int length;
	char c;
	int got;

	got = read(TERMINAL, &c, 1);
	while (got == 1) {
		if (c == '\n' || c == '\r') {
			line[length] = 0;
			length = 0;
			handle_command(line);
			fflush(stdout);
			return;
		}
		if (length < LINE_MAX_LENGTH - 1)
			line[length++] = c;
		got = read(TERMINAL, &c, 1);
	}
	if (got == 0)
		is_done = 1;
}

static void main_loop(void) {
	struct pollfd polls[1 + SERVER_POLLS];
	int count;

	while (!is_done) {
		spectrum_send(now_ms());
		client_spectrum_send(now_ms());
		measure_update(now_ms());
		panel_poll(now_ms(), on_panel_record);

		polls[0].fd = TERMINAL;
		polls[0].events = POLLIN;
		polls[0].revents = 0;
		count = 1 + server_get_pollfds(polls + 1);
		if (poll(polls, count, WAIT_ON_TERMINAL_MS) <= 0)
			continue;
		server_handle_events(polls + 1, count - 1);
		if (polls[0].revents & (POLLIN | POLLHUP))
			read_terminal();
	}
}

int main(int argc, char **argv) {
	const char *hz;
	const char *mode;
	const char *volume;

	hz = NULL;
	mode = NULL;
	volume = NULL;
	if (argc > 1)
		hz = argv[1];
	if (argc > 2)
		mode = argv[2];
	if (argc > 3)
		volume = argv[3];

	if (setup(&board_zbitx_1) < 0) {
		printf("the radio did not open\n");
		return 1;
	}
	printf("radio open on the %s\n", board_zbitx_1.name);

	panel_open();
	radio_open();
	control_on_change(on_control_changed);
	if (hz != NULL)
		set_setting("FREQ", hz, ORIGIN_TERMINAL);
	if (mode != NULL)
		set_setting("MODE", mode, ORIGIN_TERMINAL);
	if (volume != NULL)
		set_setting("AUDIO", volume, ORIGIN_TERMINAL);
	panel_send_settings();

	if (server_open(SERVER_PORT) < 0)
		printf("clients cannot connect: port %d is not free\n", SERVER_PORT);
	else
		printf("clients connect on port %d\n", SERVER_PORT);
	fflush(stdout);

	main_loop();
	report();
	server_close();
	teardown();
	return 0;
}
