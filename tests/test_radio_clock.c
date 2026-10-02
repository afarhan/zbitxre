// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_radio_clock.c — the radio's clock: which time it takes, the
// kernel's or the operator's. It needs no radio; the readings the clock
// works from are given here.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "radio_clock.h"

#define WHY_MAX 80
// 2026-10-02 13:47:00 UTC, and the minutes from the start of 2026 to it
#define REAL_TIME 1790948820LL
#define REAL_MINUTES 395387
#define YEAR_2000 946684811LL
// what Raspberry Pi OS puts back at the start: the time of the last shut down
#define STALE_TIME (REAL_TIME - 86400)

static struct radio_clock_readings given;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static void give(struct radio_clock_readings *r) {
	*r = given;
}

int main(void) {
	char why[WHY_MAX];
	time_t now;

	radio_clock_set_reading(give);

	given.is_ntp_synced = 0;
	given.rtc_s = YEAR_2000 + 500;
	given.system_s = STALE_TIME;
	given.since_start_s = 500;
	expect(radio_clock_open() == 0, "the clock's controls are registered");
	expect(radio_clock_now(&now) == RADIO_CLOCK_NOT_SET,
		"with the chip at the year 2000 and no network, the time is not set, however likely the kernel's looks");
	expect(strcmp(control_get("CLOCK"), "NOT SET") == 0, "and CLOCK says so");

	expect(control_set("TIME_SET", "395387", ORIGIN_NONE, why, sizeof(why)) == 0,
		"the operator gives the time, in minutes since the start of 2026");
	expect(radio_clock_now(&now) == RADIO_CLOCK_SET_BY_HAND && now == REAL_TIME,
		"and that is the time, from the moment it was given");
	given.since_start_s += 3600;
	given.system_s += 3600;
	given.rtc_s += 3600;
	radio_clock_poll(1000);
	expect(radio_clock_now(&now) == RADIO_CLOCK_SET_BY_HAND && now == REAL_TIME + 3600,
		"an hour later it is an hour on");
	expect(strcmp(control_get("CLOCK"), "2026-10-02 14:47 SET") == 0,
		"CLOCK shows it in UTC, as set by hand");
	given.system_s += 7200;
	expect(radio_clock_now(&now) == RADIO_CLOCK_SET_BY_HAND && now == REAL_TIME + 3600,
		"the kernel's clock jumping does not move it");

	given.is_ntp_synced = 1;
	given.system_s = REAL_TIME + 5000;
	expect(radio_clock_now(&now) == RADIO_CLOCK_NTP && now == REAL_TIME + 5000,
		"once the network has set the kernel's clock, that comes before what the operator gave");

	given.is_ntp_synced = 0;
	given.rtc_s = REAL_TIME + 5002;
	expect(radio_clock_now(&now) == RADIO_CLOCK_RTC && now == REAL_TIME + 5000,
		"so does the kernel's clock when the chip has the time and agrees with it");
	given.rtc_s = REAL_TIME - 40000;
	expect(radio_clock_now(&now) == RADIO_CLOCK_SET_BY_HAND,
		"a chip that disagrees with the kernel's clock is not taken");

	expect(control_set("CLOCK", "2030-01-01 00:00 SET", ORIGIN_NONE, why, sizeof(why)) < 0,
		"CLOCK itself cannot be set");
	expect(control_set("TIME_SET", "39445920", ORIGIN_NONE, why, sizeof(why)) < 0,
		"nor a time after the year 2100");

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("radio clock: all checks passed\n");
	return EXIT_SUCCESS;
}
