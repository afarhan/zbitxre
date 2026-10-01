// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_cw_keyer.c — the CW keyer, with no radio.
//
// The keyer is given the two contacts sample by sample, as closures laid
// out in milliseconds, and what it keys is written down as marks: where
// each began and how long it was, in samples. A dot is 1.2 seconds over the
// speed, so at 20 words a minute it is 5760 samples, and everything the
// keyer sends must be exact to the sample.
//
// The checks: a dot, a dash and the space after each at four speeds; a tap
// far shorter than a dot still sends a whole dot; a paddle held repeats;
// a tap of the other paddle during an element's mark, or during its space,
// is sent after it, and so is a second tap of the same paddle; a contact
// that bounces as it closes sends one element; both paddles held take
// turns, the dot first; a squeeze let go during an element ends with that
// element in iambic A and with one more in iambic B; Ultimatic repeats the
// paddle that closed last; the bug makes dots from one paddle and follows
// the other as a key; the straight key is followed to the sample, and is
// not keyed by the dash contact; the envelope rises from nothing to full
// in 5 ms without a step or a dip, falls the same way, and picks up from
// where it is when the key goes down again during the fall; and the keyer
// says it is busy until the last of the fall.
//
// Exits 0 when every check passes.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "cw_keyer.h"

#define SAMPLES_PER_MS (CW_KEYER_RATE_HZ / 1000)
#define DOT_AT_20_WPM 5760
#define STAYS_OPEN 192
#define RISE 480
#define MARKS_MAX 64
#define CLOSURES_END (-1)
#define LARGEST_STEP 0.006f

// a contact is closed from from_ms up to to_ms, in tenths of a millisecond
struct closure {
	int from_tenths;
	int to_tenths;
};

struct mark {
	int start;
	int length;
};

static struct mark marks[MARKS_MAX];
static int mark_count;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static int is_closed_at(const struct closure *closures, int sample) {
	int tenths_per_sample_scaled = sample * 10;
	int i;

	for (i = 0; closures[i].from_tenths != CLOSURES_END; i++) {
		if (tenths_per_sample_scaled >= closures[i].from_tenths * SAMPLES_PER_MS
				&& tenths_per_sample_scaled < closures[i].to_tenths * SAMPLES_PER_MS)
			return 1;
	}
	return 0;
}

// runs the keyer for some milliseconds and writes down the marks
static void run(enum cw_keyer_mode mode, int wpm, int ms,
		const struct closure *dot, const struct closure *dash) {
	int was_down = 0;
	int is_down;
	int sample;

	cw_keyer_reset();
	cw_keyer_set_mode(mode);
	cw_keyer_set_wpm(wpm);
	mark_count = 0;
	for (sample = 0; sample < ms * SAMPLES_PER_MS; sample++) {
		is_down = cw_keyer_is_key_down(is_closed_at(dot, sample),
			is_closed_at(dash, sample));
		if (is_down && !was_down && mark_count < MARKS_MAX) {
			marks[mark_count].start = sample;
			marks[mark_count].length = 0;
			mark_count++;
		}
		if (is_down)
			marks[mark_count - 1].length++;
		was_down = is_down;
	}
}

// whether the marks are these, as dots (1) and dashes (3) of a dot's
// length, each a dot's space after the one before
static int marks_are(int dot_samples, const char *elements) {
	int expected_start = marks[0].start;
	int length;
	int i;

	for (i = 0; elements[i] != 0; i++) {
		if (i >= mark_count)
			return 0;
		length = dot_samples;
		if (elements[i] == '-')
			length = dot_samples * 3;
		if (marks[i].length != length || marks[i].start != expected_start)
			return 0;
		expected_start += length + dot_samples;
	}
	return mark_count == i;
}

static const struct closure never[] = { { CLOSURES_END, 0 } };
static const struct closure always[] = { { 0, 100000 }, { CLOSURES_END, 0 } };

static void check_elements(void) {
	static const struct closure tap[] = { { 0, 50 }, { CLOSURES_END, 0 } };
	static const struct closure held[] = { { 0, 2500 }, { CLOSURES_END, 0 } };
	static const struct closure bounce[] = {
		{ 0, 3 }, { 6, 8 }, { 11, 200 }, { CLOSURES_END, 0 } };
	static const int speeds[] = { 10, 20, 30, 40 };
	int dot;
	int ok = 1;
	int i;

	for (i = 0; i < 4; i++) {
		dot = CW_KEYER_RATE_HZ * 12 / 10 / speeds[i];
		run(CW_KEYER_IAMBIC, speeds[i], 1000, tap, never);
		ok = ok && marks_are(dot, ".");
		run(CW_KEYER_IAMBIC, speeds[i], 1000, never, tap);
		ok = ok && marks_are(dot, "-");
	}
	expect(ok, "a tap of 5 ms sends a whole dot, or a whole dash, at 10, 20, 30 and 40 WPM");
	run(CW_KEYER_IAMBIC, 20, 1000, held, never);
	expect(marks_are(DOT_AT_20_WPM, "..."),
		"a paddle held for 250 ms sends three dots, a dot's space between them");
	run(CW_KEYER_IAMBIC, 20, 1000, bounce, never);
	expect(marks_are(DOT_AT_20_WPM, "."),
		"a contact that bounces as it closes sends one dot");
}

static void check_memory(void) {
	static const struct closure dash_tap[] = { { 0, 200 }, { CLOSURES_END, 0 } };
	static const struct closure dot_in_mark[] = { { 500, 600 }, { CLOSURES_END, 0 } };
	static const struct closure dot_in_space[] = { { 2000, 2100 }, { CLOSURES_END, 0 } };
	static const struct closure dot_twice[] = {
		{ 0, 100 }, { 800, 900 }, { CLOSURES_END, 0 } };
	static const enum cw_keyer_mode modes[] = {
		CW_KEYER_IAMBIC, CW_KEYER_IAMBICB, CW_KEYER_ULTIMATIC };
	int ok = 1;
	int i;

	for (i = 0; i < 3; i++) {
		run(modes[i], 20, 1000, dot_in_mark, dash_tap);
		ok = ok && marks_are(DOT_AT_20_WPM, "-.");
		run(modes[i], 20, 1000, dot_in_space, dash_tap);
		ok = ok && marks_are(DOT_AT_20_WPM, "-.");
		run(modes[i], 20, 1000, dot_twice, never);
		ok = ok && marks_are(DOT_AT_20_WPM, "..");
	}
	expect(ok, "a tap during an element's mark or its space is sent after it, "
		"in iambic A, B and Ultimatic");
}

static void check_squeeze(void) {
	static const struct closure both[] = { { 0, 5000 }, { CLOSURES_END, 0 } };
	static const struct closure let_go[] = { { 0, 2000 }, { CLOSURES_END, 0 } };
	static const struct closure dash_first[] = { { 0, 500 }, { CLOSURES_END, 0 } };
	static const struct closure dot_tap[] = { { 800, 1000 }, { CLOSURES_END, 0 } };
	static const struct closure dot_long[] = { { 0, 5000 }, { CLOSURES_END, 0 } };
	static const struct closure dash_later[] = { { 1000, 5000 }, { CLOSURES_END, 0 } };

	run(CW_KEYER_IAMBIC, 20, 1000, both, both);
	expect(marks_are(DOT_AT_20_WPM, ".-.-"), "both paddles held take turns, the dot first");
	run(CW_KEYER_IAMBIC, 20, 1000, let_go, let_go);
	expect(marks_are(DOT_AT_20_WPM, ".-"),
		"iambic A: a squeeze let go during the dash ends with the dash");
	run(CW_KEYER_IAMBICB, 20, 1000, let_go, let_go);
	expect(marks_are(DOT_AT_20_WPM, ".-."),
		"iambic B: the same squeeze is followed by one more dot");
	run(CW_KEYER_IAMBIC, 20, 1000, dot_tap, dash_first);
	expect(marks_are(DOT_AT_20_WPM, "-."),
		"iambic A still sends a dot tapped during the dash");
	run(CW_KEYER_ULTIMATIC, 20, 1000, dot_long, dash_later);
	expect(marks_are(DOT_AT_20_WPM, ".--"),
		"Ultimatic repeats the paddle that closed last");
}

static void check_bug_and_straight(void) {
	static const struct closure dots[] = { { 0, 2500 }, { CLOSURES_END, 0 } };
	static const struct closure dash[] = { { 5000, 6500 }, { CLOSURES_END, 0 } };
	static const struct closure key[] = { { 1000, 1370 }, { CLOSURES_END, 0 } };

	run(CW_KEYER_BUG, 20, 1000, dots, dash);
	expect(mark_count == 4 && marks[0].length == DOT_AT_20_WPM
		&& marks[2].length == DOT_AT_20_WPM
		&& marks[3].start == 500 * SAMPLES_PER_MS
		&& marks[3].length == 150 * SAMPLES_PER_MS + STAYS_OPEN,
		"bug: three dots from one paddle, and the other followed as a key");
	run(CW_KEYER_STRAIGHT, 20, 1000, key, never);
	expect(mark_count == 1 && marks[0].start == 100 * SAMPLES_PER_MS
		&& marks[0].length == 37 * SAMPLES_PER_MS + STAYS_OPEN,
		"straight key: down at the sample it closes, up 2 ms after it opens");
	run(CW_KEYER_STRAIGHT, 20, 1000, never, always);
	expect(mark_count == 0, "the straight key is not keyed by the dash contact");
}

static void check_envelope(void) {
	float before = 0;
	float now = 0;
	float largest_step = 0;
	int is_monotonic = 1;
	int is_busy_through_the_fall = 1;
	int i;

	cw_keyer_reset();
	for (i = 0; i < RISE; i++) {
		now = cw_keyer_shape(1);
		if (now < before)
			is_monotonic = 0;
		if (now - before > largest_step)
			largest_step = now - before;
		before = now;
	}
	expect(is_monotonic && now == 1.0f && largest_step < LARGEST_STEP,
		"the envelope rises from nothing to full in 5 ms, with no step and no dip");
	for (i = 0; i < RISE / 2; i++)
		now = cw_keyer_shape(0);
	before = now;
	now = cw_keyer_shape(1);
	expect(before > 0.1f && before < 0.9f && now > before && now - before < LARGEST_STEP,
		"keyed again during the fall, it rises from where the fall had got to");
	for (i = 0; i < RISE * 2; i++) {
		if (now > 0 && !cw_keyer_is_busy())
			is_busy_through_the_fall = 0;
		now = cw_keyer_shape(0);
	}
	expect(now == 0.0f && is_busy_through_the_fall && !cw_keyer_is_busy(),
		"it falls to nothing, and the keyer is busy until it has");
}

int main(void) {
	check_elements();
	check_memory();
	check_squeeze();
	check_bug_and_straight();
	check_envelope();
	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("CW keyer: all checks passed\n");
	return EXIT_SUCCESS;
}
