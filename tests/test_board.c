// test_board.c — the zbitx board's pins, on the radio.
//
// Stop the radio program first; this test owns the pins while it runs.
//
// The transmitter's power line is never raised here. The test only confirms
// that it is low, because raising it powers the amplifier and that is not for
// a test to do unasked. The receiver's power and the four filter lines are
// switched and read back from the pins themselves.
//
// For each of several frequencies the test selects the filter and checks that
// exactly one filter line is high and that it is the right one.
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

struct band {
	uint32_t hz;
	int pin;
	const char *name;
};

static const struct band bands[] = {
	{ 1900000u, BOARD_PIN_FILTER_D, "1.9 MHz takes filter D" },
	{ 3600000u, BOARD_PIN_FILTER_D, "3.6 MHz takes filter D" },
	{ 5350000u, BOARD_PIN_FILTER_D, "5.35 MHz takes filter D" },
	{ 7100000u, BOARD_PIN_FILTER_C, "7.1 MHz takes filter C" },
	{ 10120000u, BOARD_PIN_FILTER_C, "10.12 MHz takes filter C" },
	{ 14200000u, BOARD_PIN_FILTER_B, "14.2 MHz takes filter B" },
	{ 18100000u, BOARD_PIN_FILTER_B, "18.1 MHz takes filter B" },
	{ 21200000u, BOARD_PIN_FILTER_A, "21.2 MHz takes filter A" },
	{ 24900000u, BOARD_PIN_FILTER_A, "24.9 MHz takes filter A" },
	{ 28500000u, BOARD_PIN_FILTER_A, "28.5 MHz takes filter A" },
};

#define BANDS ((int)(sizeof(bands) / sizeof(bands[0])))

static const int filter_pins[BOARD_FILTERS] = {
	BOARD_PIN_FILTER_A,
	BOARD_PIN_FILTER_B,
	BOARD_PIN_FILTER_C,
	BOARD_PIN_FILTER_D,
};

static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static int filters_high(void) {
	int count;
	int i;

	count = 0;
	for (i = 0; i < BOARD_FILTERS; i++)
		count += gpio_read(filter_pins[i]);
	return count;
}

static void check_power_lines(void) {
	expect(gpio_read(BOARD_PIN_TRANSMITTER) == 0, "the transmitter is off");
	expect(gpio_read(BOARD_PIN_RECEIVER) == 1, "the receiver is on after opening");

	board_receiver_power(0);
	usleep(SETTLE_US);
	expect(gpio_read(BOARD_PIN_RECEIVER) == 0, "the receiver switches off");

	board_receiver_power(1);
	usleep(SETTLE_US);
	expect(gpio_read(BOARD_PIN_RECEIVER) == 1, "the receiver switches on");
	expect(gpio_read(BOARD_PIN_TRANSMITTER) == 0, "the transmitter is still off");
}

static void check_filters(void) {
	int ok;
	int i;

	expect(filters_high() == 0, "no filter is selected after opening");
	for (i = 0; i < BANDS; i++) {
		board_select_filter(bands[i].hz);
		usleep(SETTLE_US);
		ok = filters_high() == 1;
		if (ok)
			ok = gpio_read(bands[i].pin) == 1;
		expect(ok, bands[i].name);
	}
	board_release_filters();
	usleep(SETTLE_US);
	expect(filters_high() == 0, "releasing the filters leaves none selected");
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
		board_receiver_power(1);
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

	if (board_open() < 0) {
		perror("cannot reach the pins");
		return 1;
	}

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
