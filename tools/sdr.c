// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// sdr.c — the radio, run from a terminal, with its front panel.
//
// This opens the radio, tunes it, and then does what it is told, from two
// places at once: the terminal and the front panel.
//
// From the terminal it takes one request to a line, in the words of
// sdr_request(): r1:freq=7040000, r1:mode=CW, r1:low=300, r1:high=3000,
// r1:agc=FAST, r1:gain=60, r1:volume=80, rx_pitch=600, smeter=0. It prints
// the answer to each.
//
// From the panel it takes the frequency and the volume, over the link that
// hw/panel.c keeps. Turning the panel's knob tunes the radio. What the panel
// sends is printed as it arrives.
//
// The radio is the one that remembers. It tells the panel the frequency and
// the volume when the panel says HELLO, which the panel does when it starts,
// and whenever they are changed from the terminal, so that the panel's dial
// shows what the radio is tuned to.
//
// Some words are the program's own. "report" prints how the sound card and
// the link have been doing. "panel=off" stops the radio polling the panel
// and "panel=on" starts it again, so that the noise the link makes in the
// receiver can be told by ear from the rest. "quit", or the end of the
// input, closes the radio.
//
//     sdr [hz] [mode] [volume]
//
// Without arguments it opens on 7088 kHz, LSB, at a volume of 75, with a
// passband of 300 to 3000 Hz. The passband does not follow the mode; ask for
// r1:low and r1:high after changing it.
//
// The program waits on the terminal for a few milliseconds at a time and
// looks to the panel in between. hw/panel.c decides whether it is time to
// poll.
//
// The first run takes a while to start, because the transforms are timed to
// find the fastest way of doing them. What it learns is kept in data/ and
// later runs start at once.

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "board.h"
#include "panel.h"
#include "sdr.h"

#define DEFAULT_HZ      "7088000"
#define DEFAULT_MODE    "LSB"
#define DEFAULT_VOLUME  "75"
#define DEFAULT_LOW     "300"
#define DEFAULT_HIGH    "3000"
#define LINE_MAX_LENGTH 128
#define RESPONSE_MAX    100

#define TERMINAL        0
#define WAIT_ON_TERMINAL_MS 5
#define MS_PER_SECOND   1000
#define NS_PER_MS       1000000

#define FREQ_IS         "r1:freq="
#define VOLUME_IS       "r1:volume="

static int hz_now;
static int volume_now;
static int is_done;

static long now_ms(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * MS_PER_SECOND + now.tv_nsec / NS_PER_MS;
}

static void ask(const char *key, const char *value) {
	char request[LINE_MAX_LENGTH];
	char response[RESPONSE_MAX];

	snprintf(request, sizeof(request), "%s=%s", key, value);
	sdr_request(request, response);
	printf("%s: %s\n", request, response);
}

static void ask_number(const char *key_is, int value) {
	char request[LINE_MAX_LENGTH];
	char response[RESPONSE_MAX];

	snprintf(request, sizeof(request), "%s%d", key_is, value);
	sdr_request(request, response);
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

static void tell_the_panel(void) {
	panel_set(LINK_FREQ, hz_now);
	panel_set(LINK_VOLUME, volume_now);
}

static void on_panel_took(int control, int32_t value) {
	if (control == LINK_HELLO) {
		printf("panel: HELLO, version %d\n", (int)value);
		tell_the_panel();
	}
	if (control == LINK_FREQ) {
		hz_now = value;
		ask_number(FREQ_IS, hz_now);
		printf("panel: FREQ %d\n", hz_now);
	}
	if (control == LINK_VOLUME) {
		volume_now = value;
		ask_number(VOLUME_IS, volume_now);
		printf("panel: VOLUME %d\n", volume_now);
	}
	fflush(stdout);
}

static int begins_with(const char *line, const char *with) {
	return strncmp(line, with, strlen(with)) == 0;
}

static void note_what_the_terminal_changed(const char *line) {
	if (begins_with(line, FREQ_IS)) {
		hz_now = atoi(line + strlen(FREQ_IS));
		panel_set(LINK_FREQ, hz_now);
	}
	if (begins_with(line, VOLUME_IS)) {
		volume_now = atoi(line + strlen(VOLUME_IS));
		panel_set(LINK_VOLUME, volume_now);
	}
}

static void obey_a_line(char *line) {
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
	sdr_request(line, response);
	printf("%s\n", response);
	note_what_the_terminal_changed(line);
}

static void take_from_the_terminal(void) {
	static char line[LINE_MAX_LENGTH];
	static int length;
	char c;
	int got;

	got = read(TERMINAL, &c, 1);
	while (got == 1) {
		if (c == '\n' || c == '\r') {
			line[length] = 0;
			length = 0;
			obey_a_line(line);
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

static void obey(void) {
	struct pollfd terminal;

	terminal.fd = TERMINAL;
	terminal.events = POLLIN;
	while (!is_done) {
		panel_poll(now_ms(), on_panel_took);
		terminal.revents = 0;
		if (poll(&terminal, 1, WAIT_ON_TERMINAL_MS) <= 0)
			continue;
		if (terminal.revents & (POLLIN | POLLHUP))
			take_from_the_terminal();
	}
}

int main(int argc, char **argv) {
	const char *hz;
	const char *mode;
	const char *volume;

	hz = DEFAULT_HZ;
	mode = DEFAULT_MODE;
	volume = DEFAULT_VOLUME;
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

	ask("r1:freq", hz);
	ask("r1:mode", mode);
	ask("r1:low", DEFAULT_LOW);
	ask("r1:high", DEFAULT_HIGH);
	ask("r1:volume", volume);
	fflush(stdout);

	hz_now = atoi(hz);
	volume_now = atoi(volume);
	panel_open();
	tell_the_panel();

	obey();
	report();
	teardown();
	return 0;
}
