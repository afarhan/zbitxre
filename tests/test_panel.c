// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_panel.c — the link to the front panel, on the radio.
//
// Stop the radio program first; this test uses the bus the radio uses.
//
// It finds the bus by looking for the Si5351, as the radio does, and then
// polls the panel for a while as the radio would. It prints each control
// the panel sends as it arrives, so turning the knob shows here. At the end
// it says how the link did.
//
// When the panel says HELLO, the test gives it a frequency and a volume, as
// the radio will, so the panel's screen should show 7.123.400 and 42.
//
//     test_panel [seconds]
//
// It passes if the panel answered, said HELLO, and no more than one answer
// in a hundred was lost or spoiled. The Si5351 is not touched.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "i2c.h"
#include "si5351.h"
#include "panel.h"

#define SECONDS 10
#define MS_PER_SECOND 1000
#define NS_PER_MS 1000000
#define BETWEEN_LOOKS_US 1000
#define GIVEN_FREQ 7123400
#define GIVEN_VOLUME 42
#define WORST_LOSS_PERCENT 1
#define PERCENT 100

static int hellos;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static long now_ms(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * MS_PER_SECOND + now.tv_nsec / NS_PER_MS;
}

static void on_took(int control, int32_t value) {
	if (control == LINK_HELLO) {
		hellos++;
		printf("          panel: HELLO, version %d\n", (int)value);
		panel_set(LINK_FREQ, GIVEN_FREQ);
		panel_set(LINK_VOLUME, GIVEN_VOLUME);
		return;
	}
	if (control == LINK_FREQ)
		printf("          panel: FREQ %d\n", (int)value);
	else if (control == LINK_VOLUME)
		printf("          panel: VOLUME %d\n", (int)value);
	else
		printf("          panel: control %d is %d\n", control, (int)value);
	fflush(stdout);
}

int main(int argc, char **argv) {
	struct panel_report report;
	unsigned long bad;
	long ends_ms;
	int seconds;
	int bus;

	seconds = SECONDS;
	if (argc > 1)
		seconds = atoi(argv[1]);

	bus = i2c_open_bus_of(SI5351_ADDRESS);
	if (bus < 0) {
		perror("no bus with the Si5351 on it");
		return EXIT_FAILURE;
	}
	printf("bus %d, polling the panel for %d seconds; turn its knob\n",
		bus, seconds);

	panel_open();
	ends_ms = now_ms() + seconds * MS_PER_SECOND;
	while (now_ms() < ends_ms) {
		panel_poll(now_ms(), on_took, NULL);
		usleep(BETWEEN_LOOKS_US);
	}
	panel_get_report(&report);
	i2c_close();

	printf("          polls %lu, answers %lu, no answer %lu, spoiled %lu, "
		"repeated %lu\n", report.polls, report.answers, report.no_answers,
		report.frames_spoiled, report.frames_repeated);
	printf("          words %lu, not taken %lu, frames taken %lu\n",
		report.words, report.words_not_taken, report.frames_taken);

	bad = report.no_answers + report.frames_spoiled + report.words_not_taken;
	expect(report.answers > 0, "the panel answers");
	expect(hellos > 0, "the panel said HELLO");
	expect(bad * PERCENT <= report.polls * WORST_LOSS_PERCENT,
		"no more than one transfer in a hundred went wrong");

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("panel link: all checks passed\n");
	return EXIT_SUCCESS;
}
