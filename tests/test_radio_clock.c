// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_radio_clock.c — the radio's clock: which time it takes, the
// kernel's or the operator's. It needs no radio; the readings the clock
// works from are given here.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "radio_clock.h"

// 2026-10-02 13:47:00 UTC
#define REAL_TIME 1790948820LL
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
	time_t now;

	radio_clock_set_reading(give);

	given.is_ntp_synced = 0;
	given.rtc_s = YEAR_2000 + 500;
	given.system_s = STALE_TIME;
	given.since_start_s = 500;
	expect(radio_clock_now(&now) == RADIO_CLOCK_NOT_SET,
		"with the chip at the year 2000 and no network, the time is not set, however likely the kernel's looks");

	radio_clock_set_by_hand((time_t)REAL_TIME);
	expect(radio_clock_now(&now) == RADIO_CLOCK_SET_BY_HAND && now == REAL_TIME,
		"the operator gives the time, and that is the time from the moment it was given");
	given.since_start_s += 3600;
	given.system_s += 3600;
	given.rtc_s += 3600;
	expect(radio_clock_now(&now) == RADIO_CLOCK_SET_BY_HAND && now == REAL_TIME + 3600,
		"an hour later it is an hour on");
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

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("radio clock: all checks passed\n");
	return EXIT_SUCCESS;
}
