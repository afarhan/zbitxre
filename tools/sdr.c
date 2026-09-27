// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// sdr.c — the radio, run from a terminal.
//
// This opens the radio, tunes it, and then does what it is told, one request
// to a line, in the words of sdr_request(): r1:freq=7040000, r1:mode=CW,
// r1:low=300, r1:high=3000, r1:agc=FAST, r1:gain=60, r1:volume=80,
// rx_pitch=600, smeter=0. It prints the answer to each.
//
// Two more words are the program's own. "report" prints how the sound card
// has been doing. "quit", or the end of the input, closes the radio.
//
//     sdr [hz] [mode] [volume]
//
// Without arguments it opens on 7040 kHz, LSB, at a volume of 75, with a
// passband of 300 to 3000 Hz. The passband does not follow the mode; ask for
// r1:low and r1:high after changing it.
//
// The first run takes a while to start, because the transforms are timed to
// find the fastest way of doing them. What it learns is kept in data/ and
// later runs start at once.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "board.h"
#include "sdr.h"

#define DEFAULT_HZ      "7040000"
#define DEFAULT_MODE    "LSB"
#define DEFAULT_VOLUME  "75"
#define DEFAULT_LOW     "300"
#define DEFAULT_HIGH    "3000"
#define LINE_MAX_LENGTH 128
#define RESPONSE_MAX    100

static void ask(const char *key, const char *value) {
	char request[LINE_MAX_LENGTH];
	char response[RESPONSE_MAX];

	snprintf(request, sizeof(request), "%s=%s", key, value);
	sdr_request(request, response);
	printf("%s: %s\n", request, response);
}

static void report(void) {
	struct audio_report now;

	audio_get_report(&now);
	printf("blocks %lu, gaps %lu, dropped %lu, overflows %lu, "
		"longest wait %ld us, queued %ld frames\n",
		now.blocks, now.playback_gaps, now.blocks_dropped,
		now.capture_overruns, now.longest_wait_us, now.queued_frames);
	if (!now.is_realtime)
		printf("the audio thread did not get real-time priority\n");
	if (!now.is_running)
		printf("the audio thread has stopped\n");
}

static void strip_the_line_end(char *line) {
	line[strcspn(line, "\r\n")] = 0;
}

static void obey(void) {
	char line[LINE_MAX_LENGTH];
	char response[RESPONSE_MAX];

	while (fgets(line, sizeof(line), stdin) != NULL) {
		strip_the_line_end(line);
		if (line[0] == 0)
			continue;
		if (strcmp(line, "quit") == 0)
			return;
		if (strcmp(line, "report") == 0) {
			report();
			fflush(stdout);
			continue;
		}
		sdr_request(line, response);
		printf("%s\n", response);
		fflush(stdout);
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

	obey();
	report();
	teardown();
	return 0;
}
