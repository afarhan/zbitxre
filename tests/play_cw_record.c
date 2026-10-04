// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// play_cw_record.c — a recording made on the radio with "record=", played
// through the receiver and both CW decoders on the computer, for working on
// the decoders with signals heard off the air.
//
//     build/play_cw_record data/record_20261004_022624.raw [pitch] [wpm]
//
// It needs no radio; it is built with tests/hw_stand_ins.c, as the tests
// are. The receiver is set up for the zbitx's first board and put in CW, as
// the radio was. The recording is the sound card's blocks as they came,
// both channels, and they are given to the receiver one by one through
// hw_stand_in_block(), as fast as the computer goes.
//
// The pitch and the speed are read from the note the radio wrote beside the
// recording, record_<time>.txt, unless they are given. A different pitch
// listens to a different station in the recording: the pitch is where in
// the passband the decoders listen.
//
// What each decoder reads is printed as it is read, a line at a time with
// the time into the recording: the Goertzel filter's, which the panel
// shows, and the spectrum's, which the radio's terminal prints; modem_cw.c
// says how the two differ.
//
// The radio moves its first oscillator by the pitch in CW, so a station on
// the dial lands the pitch above the board's centre bin. The recording was
// made that way, with the oscillator moved, so it is played as it is, with
// no moving of its own: the pitch given here should be the radio's, for
// the stations to be where the decoders look.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "board.h"
#include "control.h"
#include "modem.h"
#include "sdr.h"
#include "hw_stand_ins.h"
#include "modems/modem_cw.h"

#define CHANNELS 2
#define SAMPLE_RATE 96000.0
#define ANSWER_MAX 100
#define WHY_ROOM 80
#define NOTE_ROOM 256
#define VALUE_ROOM 16
#define LINE_MAX 70
#define READ_ROOM 256
#define NOTE_SUFFIX_LENGTH 4

struct reader {
	const char *name;
	int (*take)(char *text, int room);
	char line[LINE_MAX + 1];
	int count;
	double line_began;
};

static void ask(char *request) {
	char answer[ANSWER_MAX];

	sdr_request(request, answer);
}

static void set(const char *name, const char *value) {
	char why[WHY_ROOM];

	control_set(name, value, ORIGIN_NONE, why, sizeof(why));
}

// "pitch 600 Hz", "wpm 20" from the note beside the recording
static void from_the_note(const char *raw_path, char *pitch, char *wpm) {
	char path[NOTE_ROOM];
	char line[NOTE_ROOM];
	char *at;
	FILE *note;
	size_t length = strlen(raw_path);

	if (length < NOTE_SUFFIX_LENGTH || length >= sizeof(path))
		return;
	snprintf(path, sizeof(path), "%.*s.txt", (int)(length - NOTE_SUFFIX_LENGTH), raw_path);
	note = fopen(path, "r");
	if (note == NULL)
		return;
	while (fgets(line, sizeof(line), note) != NULL) {
		at = strstr(line, "pitch ");
		if (at != NULL)
			snprintf(pitch, VALUE_ROOM, "%d", atoi(at + strlen("pitch ")));
		at = strstr(line, "wpm ");
		if (at != NULL)
			snprintf(wpm, VALUE_ROOM, "%d", atoi(at + strlen("wpm ")));
	}
	fclose(note);
}

static void print_line(struct reader *r) {
	if (r->count == 0)
		return;
	r->line[r->count] = 0;
	printf("%7.1f s  %-8s %s\n", r->line_began, r->name, r->line);
	r->count = 0;
}

// what a decoder has read since the last look, added to its line
static void gather(struct reader *r, double seconds) {
	char read[READ_ROOM];
	int count = r->take(read, sizeof(read));
	int i;

	for (i = 0; i < count; i++) {
		if (r->count == 0)
			r->line_began = seconds;
		r->line[r->count++] = read[i];
		if (r->count == LINE_MAX)
			print_line(r);
	}
}

int main(int argc, char **argv) {
	static int32_t samples[AUDIO_BLOCK_FRAMES * CHANNELS];
	static int32_t from_receiver[AUDIO_BLOCK_FRAMES];
	static int32_t from_microphone[AUDIO_BLOCK_FRAMES];
	static int32_t to_speaker[AUDIO_BLOCK_FRAMES];
	static int32_t to_transmitter[AUDIO_BLOCK_FRAMES];
	struct reader readers[] = {
		{ "Goertzel", cw_rx_take, "", 0, 0 },
		{ "spectrum", cw_rx_take_spectrum, "", 0, 0 },
	};
	char pitch[VALUE_ROOM] = "600";
	char wpm[VALUE_ROOM] = "20";
	struct audio_block block;
	const struct modem *cw;
	double seconds = 0;
	long blocks = 0;
	FILE *file;
	int frame;
	int i;

	if (argc < 2) {
		printf("play_cw_record <recording.raw> [pitch] [wpm]\n");
		return EXIT_FAILURE;
	}
	from_the_note(argv[1], pitch, wpm);
	if (argc > 2)
		snprintf(pitch, sizeof(pitch), "%s", argv[2]);
	if (argc > 3)
		snprintf(wpm, sizeof(wpm), "%s", argv[3]);
	file = fopen(argv[1], "rb");
	if (file == NULL) {
		printf("%s cannot be read\n", argv[1]);
		return EXIT_FAILURE;
	}
	if (setup(&board_zbitx_1) < 0) {
		printf("the receiver did not open\n");
		return EXIT_FAILURE;
	}
	set("WPM", wpm);
	set("PITCH", pitch);
	ask("r1:freq=7030000");
	ask("r1:mode=CW");
	cw = modem_find("CW");
	if (cw != NULL && cw->select != NULL)
		cw->select();
	ask("r1:mode=CW");
	printf("%s: pitch %s Hz, wpm %s\n", argv[1], pitch, wpm);

	while (fread(samples, sizeof(int32_t), AUDIO_BLOCK_FRAMES * CHANNELS, file)
			== AUDIO_BLOCK_FRAMES * CHANNELS) {
		for (frame = 0; frame < AUDIO_BLOCK_FRAMES; frame++) {
			from_receiver[frame] = samples[frame * CHANNELS];
			from_microphone[frame] = samples[frame * CHANNELS + 1];
		}
		block.from_receiver = from_receiver;
		block.from_microphone = from_microphone;
		block.to_speaker = to_speaker;
		block.to_transmitter = to_transmitter;
		block.frames = AUDIO_BLOCK_FRAMES;
		hw_stand_in_block(&block);
		blocks++;
		seconds = blocks * AUDIO_BLOCK_FRAMES / SAMPLE_RATE;
		for (i = 0; i < 2; i++)
			gather(&readers[i], seconds);
	}
	for (i = 0; i < 2; i++)
		print_line(&readers[i]);
	fclose(file);
	printf("%ld blocks, %.1f seconds\n", blocks, seconds);
	return EXIT_SUCCESS;
}
