// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_cw_text.c — text sent as CW: the code of each character, and the
// lengths of the marks and the spaces, to the sample. It needs no radio.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cw_keyer.h"
#include "cw_text.h"

#define SAMPLES_A_MS 96
#define DOT_AT_20_WPM (CW_KEYER_RATE_HZ * 12 / 10 / 20)
#define RUNS_MAX 64
#define SENT_MAX 128
#define SAMPLES_MAX (CW_KEYER_RATE_HZ * 30)
#define RING_ROOM 512

// what was keyed, as runs of the key down and up, each so many samples
static int run_is_down[RUNS_MAX];
static int run_samples[RUNS_MAX];
static int runs;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

// keys until nothing is left to send, noting each run after the first mark
// begins; the last run, the space after the last mark, is noted too
static void key_it_all(void) {
	int is_down;
	long n;

	runs = 0;
	for (n = 0; n < SAMPLES_MAX && (cw_text_is_busy() || n == 0); n++) {
		is_down = cw_text_is_key_down();
		if (runs == 0 && !is_down)
			continue;
		if (runs > 0 && run_is_down[runs - 1] == is_down) {
			run_samples[runs - 1]++;
			continue;
		}
		if (runs == RUNS_MAX)
			return;
		run_is_down[runs] = is_down;
		run_samples[runs] = 1;
		runs++;
	}
}

// the marks as dots and dashes, with a space where the gap is three dots
// and " / " where it is seven
static void marks_as_text(char *sent) {
	int at = 0;
	int i;

	for (i = 0; i < runs && at < SENT_MAX - 4; i++) {
		if (run_is_down[i] && run_samples[i] == DOT_AT_20_WPM)
			sent[at++] = '.';
		else if (run_is_down[i] && run_samples[i] == 3 * DOT_AT_20_WPM)
			sent[at++] = '-';
		else if (run_is_down[i])
			sent[at++] = '?';
		else if (run_samples[i] == 3 * DOT_AT_20_WPM && i < runs - 1)
			sent[at++] = ' ';
		else if (run_samples[i] == 7 * DOT_AT_20_WPM && i < runs - 1) {
			memcpy(sent + at, " / ", 3);
			at += 3;
		}
		else if (run_samples[i] != DOT_AT_20_WPM && i < runs - 1)
			sent[at++] = '?';
	}
	sent[at] = 0;
}

static void send(const char *text, char *sent) {
	cw_text_reset();
	cw_text_set_wpm(20);
	cw_text_put(text);
	key_it_all();
	marks_as_text(sent);
}

int main(void) {
	static char long_text[2 * RING_ROOM];
	char sent[SENT_MAX];
	int put;
	int i;

	send("cq", sent);
	expect(strcmp(sent, "-.-. --.-") == 0,
		"cq is keyed -.-. --.- with marks and spaces exact to the sample");
	send("E e", sent);
	expect(strcmp(sent, ". / .") == 0,
		"a space in the text is seven dots, and a capital is sent as its small letter");
	send("a#b", sent);
	expect(strcmp(sent, ".- -...") == 0, "a character with no code is passed over");
	send("5nn [", sent);
	expect(strcmp(sent, "..... -. -. / .-.-.") == 0, "figures, and [ as AR");
	expect(!cw_text_is_busy(), "when all is sent it is no longer busy");

	cw_text_reset();
	cw_text_put("ttttt");
	for (i = 0; i < 4 * DOT_AT_20_WPM; i++)
		cw_text_is_key_down();
	cw_text_abort();
	expect(!cw_text_is_busy() && !cw_text_is_key_down(),
		"aborted in the middle, the key comes up and nothing is left waiting");

	memset(long_text, 'e', sizeof(long_text) - 1);
	cw_text_reset();
	put = cw_text_put(long_text);
	expect(put == RING_ROOM, "more text than there is room for is taken as far as the room goes");

	// characters keyed elsewhere, on a telephone's keyer
	cw_text_reset();
	cw_text_put_code("-.-", 0);
	cw_text_put_code(".", 0);
	key_it_all();
	marks_as_text(sent);
	expect(strcmp(sent, "-.- .") == 0,
		"keyed characters are sent as their dots and dashes, three dots apart at the least");

	cw_text_reset();
	cw_text_put_code(".", 0);
	cw_text_put_code("-", 500);
	key_it_all();
	expect(runs == 4 && run_samples[1] == 500 * SAMPLES_A_MS,
		"one keyed 500 ms after the last begins 500 ms after it, to the sample");

	cw_text_reset();
	cw_text_put_code(".", 0);
	cw_text_put_code(".", 60);
	key_it_all();
	expect(runs == 4 && run_samples[1] == 3 * DOT_AT_20_WPM,
		"one keyed sooner than three dots after the last still waits three dots");

	cw_text_reset();
	cw_text_put_code(".", 0);
	for (i = 0; i < 1000 * SAMPLES_A_MS; i++)
		cw_text_is_key_down();
	cw_text_put_code("-", 500);
	expect(cw_text_is_key_down(),
		"one that comes after its time has passed begins at once");

	cw_text_reset();
	expect(!cw_text_put_code("", 0) && !cw_text_put_code(".x-", 0)
		&& !cw_text_put_code(".............", 0) && !cw_text_is_busy(),
		"nothing, what is not dots and dashes, and more than twelve of them are refused");
	cw_text_put("a");
	cw_text_put_code("-...", 0);
	cw_text_put("c");
	key_it_all();
	marks_as_text(sent);
	expect(strcmp(sent, ".- -... -.-.") == 0, "text and keyed characters are sent in the order they came");

	memset(long_text, 'e', sizeof(long_text) - 1);
	long_text[RING_ROOM - 3] = 0;
	cw_text_reset();
	cw_text_put(long_text);
	expect(!cw_text_put_code("--", 0) && cw_text_put("ee") == 2,
		"a keyed character there is not room for whole is not taken in part");

	// what is typed takes a macro's place
	cw_text_reset();
	cw_text_put("ttttt");
	for (i = 0; i < 5 * DOT_AT_20_WPM; i++)
		cw_text_is_key_down();
	cw_text_begin_afresh();
	cw_text_put("e");
	key_it_all();
	marks_as_text(sent);
	expect(strcmp(sent, ".") == 0,
		"begun afresh in the middle of a text, what is put after is sent and nothing of what waited");

	cw_text_reset();
	cw_text_put("t");
	cw_text_begin_afresh();
	cw_text_put("a");
	cw_text_put("n");
	key_it_all();
	marks_as_text(sent);
	expect(strcmp(sent, ".- -.") == 0, "and what is put after that follows it");

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("CW text: all checks passed\n");
	return EXIT_SUCCESS;
}
