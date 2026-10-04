// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_cw_decoder.c — CW sent by a station is read back as text by the
// receiver's decoder, modem_cw.c's.
//
// It needs no radio; it is built with tests/hw_stand_ins.c, as test_am.c
// is. The receiver is set up for the zbitx's first board and put in CW,
// and is given a station on the frequency it is tuned to: a carrier keyed
// in Morse, with edges of a few milliseconds as a transmitter shapes them,
// and a little noise. In CW the radio moves its first oscillator by the
// pitch, so that a station on the dial is heard at the pitch; the stood-in
// sound card has no oscillator before it, so the station is put where the
// moved oscillator would put it, the pitch above the board's centre bin. What the decoder reads is then taken, as the radio
// takes it, with cw_rx_take(), and has to be what was sent.
//
// The station sends at 20 words a minute, the speed WPM is set to, and
// then at 25 with WPM left at 20, which the decoder has to follow by the
// length of the dashes it hears.
//
// There are two decoders, side by side, which the author is comparing:
// zbitxv2's, with its Goertzel filter, read with cw_rx_take(), and the
// reader, cw_reader.c, from the spectrum, read with cw_rx_take_reader();
// modem_cw.c says how they differ. Both are given the same stations, and
// what each reads is printed, so that the two can be compared; the checks
// are the reader's.
//
// The noise is as a band's is, about 30 dB under the station in a bin of
// the spectrum. With the noise far lower, the station and the stronger
// neighbour below are so far over it that the window's sidelobes spread
// them over every bin near, which no band does. Then the harder cases, a station 60 Hz off the
// pitch, a station with another four times as strong 200 Hz from it, and a
// station at 35 words a minute, are given to both, and printed; the
// spectrum's decoder has to read them, which the Goertzel filter's does
// not.

#include <math.h>
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

#define BLOCK 1024
#define SAMPLE_RATE 96000.0
#define HZ_PER_BIN 46.875
#define LEVEL 2.0e6
#define NOISE 2.0e6
#define EDGE_S 0.005
#define DOT_UNITS 1
#define DASH_UNITS 3
#define LETTER_GAP_UNITS 3
#define WORD_GAP_UNITS 7
#define SECONDS_A_DOT_AT_1_WPM 1.2
#define PITCH_HZ 600
#define OFF_PITCH_HZ 60
#define NEIGHBOUR_HZ 200
#define NEIGHBOUR_TIMES 4
#define LEAD_IN_S 1.0
#define TAIL_S 2.0
#define ANSWER_MAX 100
#define READ_ROOM 512
#define WHY_ROOM 80

struct morse {
	char c;
	const char *code;
};

static const struct morse morse[] = {
	{'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
	{'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
	{'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
	{'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
	{'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
	{'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"},
	{'3', "...--"}, {'4', "....-"}, {'5', "....."}, {'6', "-...."},
	{'7', "--..."}, {'8', "---.."}, {'9', "----."},
};
#define MORSE_COUNT ((int)(sizeof(morse) / sizeof(morse[0])))

static int checks_failed;
static double seconds;
static double carrier_hz;
static double neighbour_hz;
static double neighbour_level;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static void ask(char *request) {
	char answer[ANSWER_MAX];

	sdr_request(request, answer);
}

// the station's level, 0 to 1, at a time, from when its key went down and
// up: it rises and falls over EDGE_S
static double envelope(double t, double down_at, double up_at) {
	if (t < down_at || t > up_at + EDGE_S)
		return 0;
	if (t < down_at + EDGE_S)
		return (t - down_at) / EDGE_S;
	if (t > up_at)
		return 1.0 - (t - up_at) / EDGE_S;
	return 1.0;
}

// so many seconds of the station, the key down from down_at to up_at
static void play(double until, double down_at, double up_at) {
	static int32_t from_receiver[BLOCK];
	static int32_t from_microphone[BLOCK];
	static int32_t to_speaker[BLOCK];
	static int32_t to_transmitter[BLOCK];
	struct audio_block block;
	double noise;
	int i;

	while (seconds < until) {
		for (i = 0; i < BLOCK; i++) {
			noise = NOISE * (2.0 * rand() / RAND_MAX - 1.0);
			from_receiver[i] = (int32_t)(LEVEL * envelope(seconds, down_at, up_at)
				* sin(2 * M_PI * carrier_hz * seconds) + noise
				+ neighbour_level * sin(2 * M_PI * neighbour_hz * seconds));
			seconds += 1.0 / SAMPLE_RATE;
		}
		block.from_receiver = from_receiver;
		block.from_microphone = from_microphone;
		block.to_speaker = to_speaker;
		block.to_transmitter = to_transmitter;
		block.frames = BLOCK;
		hw_stand_in_block(&block);
	}
}

static const char *code_of(char c) {
	int i;

	for (i = 0; i < MORSE_COUNT; i++) {
		if (morse[i].c == c)
			return morse[i].code;
	}
	return NULL;
}

// the text is sent at the speed given, after a second of silence and
// followed by one
static void send(const char *text, int wpm) {
	double unit = SECONDS_A_DOT_AT_1_WPM / wpm;
	double length;
	const char *code;

	play(seconds + LEAD_IN_S, 0, 0);
	for (; *text != 0; text++) {
		if (*text == ' ') {
			play(seconds + (WORD_GAP_UNITS - LETTER_GAP_UNITS) * unit, 0, 0);
			continue;
		}
		code = code_of(*text);
		for (; code != NULL && *code != 0; code++) {
			length = DOT_UNITS * unit;
			if (*code == '-')
				length = DASH_UNITS * unit;
			play(seconds + length, seconds, seconds + length);
			play(seconds + DOT_UNITS * unit, 0, 0);
		}
		play(seconds + (LETTER_GAP_UNITS - DOT_UNITS) * unit, 0, 0);
	}
	play(seconds + TAIL_S, 0, 0);
}

static void set(const char *name, const char *value) {
	char why[WHY_ROOM];

	control_set(name, value, ORIGIN_NONE, why, sizeof(why));
}

// what has been read, with the spaces at its ends and the doubled ones
// between words taken out
static void tidy(char *text, int room, const char *raw) {
	int from;
	int to = 0;

	for (from = 0; raw[from] != 0 && to < room - 1; from++) {
		if (raw[from] == ' ' && (to == 0 || text[to - 1] == ' '))
			continue;
		text[to++] = raw[from];
	}
	while (to > 0 && text[to - 1] == ' ')
		to--;
	text[to] = 0;
}

// what each decoder has read, tidied, and printed
static void read_back(char *by_tone, char *by_spectrum, int room) {
	char raw[READ_ROOM];

	cw_rx_take(raw, sizeof(raw));
	tidy(by_tone, room, raw);
	cw_rx_take_reader(raw, sizeof(raw));
	tidy(by_spectrum, room, raw);
	printf("          Goertzel: [%s]\n", by_tone);
	printf("          reader:   [%s]\n", by_spectrum);
}

int main(void) {
	const struct board *board = &board_zbitx_1;
	const struct modem *cw;
	char text[READ_ROOM];
	char spectrum[READ_ROOM];

	printf("CW decoder:\n");
	if (setup(board) < 0) {
		printf("the receiver did not open\n");
		return EXIT_FAILURE;
	}
	carrier_hz = HZ_PER_BIN * board->centre_bin + PITCH_HZ;
	set("WPM", "20");
	set("PITCH", "600");
	ask("r1:freq=7030000");
	ask("r1:mode=CW");
	cw = modem_find("CW");
	if (cw != NULL && cw->select != NULL)
		cw->select();
	ask("r1:mode=CW");
	ask("r1:agc=OFF");

	send("VVV CQ CQ DE VU2ESE K", 20);
	read_back(text, spectrum, sizeof(text));
	expect(strstr(text, "CQ CQ DE VU2ESE K") != NULL && strstr(spectrum, "CQ CQ DE VU2ESE K") != NULL,
		"a station at the speed set is read by both, every letter and the spaces");

	send("VVV TEST DE W1AW 73", 25);
	read_back(text, spectrum, sizeof(text));
	expect(strstr(text, "TEST DE W1AW 73") != NULL && strstr(spectrum, "TEST DE W1AW 73") != NULL,
		"a faster station is read by both, its dashes followed");

	play(seconds + 3.0, 0, 0);
	read_back(text, spectrum, sizeof(text));
	expect(spectrum[0] == 0, "and noise alone is read as nothing by the reader");

	printf("the harder cases, to compare:\n");
	printf("        a station 60 Hz off the pitch:\n");
	carrier_hz += OFF_PITCH_HZ;
	send("VVV CQ DE VU2ESE K", 20);
	read_back(text, spectrum, sizeof(text));
	carrier_hz -= OFF_PITCH_HZ;
	expect(strstr(spectrum, "CQ DE VU2ESE K") != NULL, "the reader reads it");

	printf("        another four times as strong, 200 Hz away:\n");
	neighbour_hz = carrier_hz + NEIGHBOUR_HZ;
	neighbour_level = NEIGHBOUR_TIMES * LEVEL;
	send("VVV CQ DE VU2ESE K", 20);
	read_back(text, spectrum, sizeof(text));
	neighbour_level = 0;
	expect(strstr(spectrum, "CQ DE VU2ESE K") != NULL, "the reader reads it");

	printf("        at 35 words a minute:\n");
	set("WPM", "30");
	send("VVV CQ DE VU2ESE K", 35);
	read_back(text, spectrum, sizeof(text));
	expect(strstr(spectrum, "CQ DE VU2ESE K") != NULL, "the reader reads it");

	if (checks_failed > 0) {
		printf("CW decoder: %d checks FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("CW decoder: all checks passed\n");
	return EXIT_SUCCESS;
}
