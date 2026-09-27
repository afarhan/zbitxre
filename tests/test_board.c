// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_board.c — a board's pins, on the radio.
//
// Stop the radio program first; this test owns the pins while it runs.
//
// The checks are made from the board's own table, so they follow whatever
// lines that board has and the test names no pin and counts no filter.
//
// The transmitter's power line is never raised here. The test only confirms
// that it is low, because raising it powers the amplifier and that is not for
// a test to do unasked. If the board has a receiver line it is switched off
// and on and read back from the pin.
//
// For each filter that serves a band, the test asks for a frequency in the
// middle of that band and checks that the filter's line is high and every
// other filter line is low. A filter line with no band must stay low
// throughout.
//
// The two inputs are read and reported. With nothing pressed both are open.
//
// Last, it times a read and a write of a pin, since both are meant to be
// cheap enough for the audio thread.
//
//     test_board               run the checks
//     test_board watch [secs]  print the two inputs whenever one changes
//
// Exits 0 when every check passes.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "board.h"
#include "gpio.h"

#define SETTLE_US        2000
#define TIMED_ACCESSES   200000
#define NS_PER_SECOND    1000000000L
#define WATCH_SECONDS    20
#define WATCH_EVERY_US   1000
#define US_PER_SECOND    1000000L
#define HZ_PER_KHZ       1000u
#define WHAT_MAX         96

static const struct board *board;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static int filter_lines_high(void) {
	int count;
	int i;

	count = 0;
	for (i = 0; i < board_filters(); i++)
		count += gpio_read(board->filter[i].pin);
	return count;
}

static void check_power_lines(void) {
	expect(gpio_read(board->transmitter_pin) == 0, "the transmitter is off");
	if (board->receiver_pin == BOARD_NO_PIN) {
		printf("          this board has no receiver line\n");
		return;
	}
	expect(gpio_read(board->receiver_pin) == 1,
		"the receiver is on after opening");

	board_receiver_power(0);
	usleep(SETTLE_US);
	expect(gpio_read(board->receiver_pin) == 0, "the receiver switches off");

	board_receiver_power(1);
	usleep(SETTLE_US);
	expect(gpio_read(board->receiver_pin) == 1, "the receiver switches on");
	expect(gpio_read(board->transmitter_pin) == 0,
		"the transmitter is still off");
}

static void check_one_filter(const struct board_filter *filter,
		uint32_t band_starts_hz) {
	char what[WHAT_MAX];
	uint32_t hz;
	int ok;

	hz = band_starts_hz + (filter->below_hz - band_starts_hz) / 2;
	board_select_filter(hz);
	usleep(SETTLE_US);
	ok = filter_lines_high() == 1;
	if (ok)
		ok = gpio_read(filter->pin) == 1;
	snprintf(what, sizeof(what), "%u kHz takes filter %s and no other",
		(unsigned)(hz / HZ_PER_KHZ), filter->name);
	expect(ok, what);
}

static void check_filters(void) {
	const struct board_filter *filter;
	char what[WHAT_MAX];
	uint32_t band_starts_hz;
	int i;

	expect(filter_lines_high() == 0, "no filter is selected after opening");

	band_starts_hz = 0;
	for (i = 0; i < board_filters(); i++) {
		filter = &board->filter[i];
		if (filter->below_hz == BOARD_NO_BAND)
			continue;
		check_one_filter(filter, band_starts_hz);
		band_starts_hz = filter->below_hz;
	}

	filter = board_filter_for(band_starts_hz + HZ_PER_KHZ);
	board_select_filter(band_starts_hz + HZ_PER_KHZ);
	usleep(SETTLE_US);
	expect(filter != NULL && filter_lines_high() == 1
		&& gpio_read(filter->pin) == 1,
		"a frequency above the last edge takes the last filter");

	board_release_filters();
	usleep(SETTLE_US);
	expect(filter_lines_high() == 0,
		"releasing the filters leaves none selected");

	for (i = 0; i < board_filters(); i++) {
		filter = &board->filter[i];
		if (filter->below_hz != BOARD_NO_BAND)
			continue;
		snprintf(what, sizeof(what),
			"filter line %s, which has no band, is low", filter->name);
		expect(gpio_read(filter->pin) == 0, what);
	}
}

static void check_inputs(void) {
	expect(!board_ptt_closed(), "the PTT line is open (nothing pressed)");
	expect(!board_key_closed(), "the key line is open (nothing pressed)");
}

static long elapsed_ns(const struct timespec *from, const struct timespec *to) {
	long seconds;

	seconds = to->tv_sec - from->tv_sec;
	return seconds * NS_PER_SECOND + (to->tv_nsec - from->tv_nsec);
}

static void time_accesses(void) {
	struct timespec before;
	struct timespec after;
	long read_ns;
	long write_ns;
	int closed;
	int i;

	closed = 0;
	clock_gettime(CLOCK_MONOTONIC, &before);
	for (i = 0; i < TIMED_ACCESSES; i++)
		closed += board_key_closed();
	clock_gettime(CLOCK_MONOTONIC, &after);
	read_ns = elapsed_ns(&before, &after) / TIMED_ACCESSES;

	clock_gettime(CLOCK_MONOTONIC, &before);
	for (i = 0; i < TIMED_ACCESSES; i++)
		board_transmitter_power(0);
	clock_gettime(CLOCK_MONOTONIC, &after);
	write_ns = elapsed_ns(&before, &after) / TIMED_ACCESSES;

	printf("          reading a pin takes %ld ns, writing one %ld ns\n",
		read_ns, write_ns);
	expect(closed == 0, "the key line stayed open through the timing");
}

static void watch(int seconds) {
	long remaining;
	int ptt;
	int key;
	int ptt_before;
	int key_before;

	printf("watching the PTT and key lines for %d seconds\n", seconds);
	ptt_before = -1;
	key_before = -1;
	remaining = seconds * US_PER_SECOND;
	while (remaining > 0) {
		ptt = board_ptt_closed();
		key = board_key_closed();
		if (ptt != ptt_before || key != key_before) {
			printf("  PTT %d   key %d\n", ptt, key);
			fflush(stdout);
		}
		ptt_before = ptt;
		key_before = key;
		usleep(WATCH_EVERY_US);
		remaining -= WATCH_EVERY_US;
	}
}

int main(int argc, char **argv) {
	int seconds;

	if (board_open(&board_zbitx_1) < 0) {
		perror("cannot reach the pins");
		return 1;
	}
	board = board_in_use();
	printf("board: %s, %d filter lines\n", board->name, board_filters());

	if (argc > 1 && strcmp(argv[1], "watch") == 0) {
		seconds = WATCH_SECONDS;
		if (argc > 2)
			seconds = atoi(argv[2]);
		watch(seconds);
		board_close();
		return 0;
	}

	check_power_lines();
	check_filters();
	check_inputs();
	time_accesses();
	board_close();

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return 1;
	}
	printf("board pins: all checks passed\n");
	return 0;
}
