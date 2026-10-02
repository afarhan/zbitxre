// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// radio_clock.h — what time it is, for the radio: from the kernel when the
// kernel's clock can be trusted, and from the operator when it cannot. See
// radio_clock.c.

#pragma once

#include <time.h>

enum radio_clock_source {
	RADIO_CLOCK_NOT_SET,
	RADIO_CLOCK_SET_BY_HAND,
	RADIO_CLOCK_RTC,
	RADIO_CLOCK_NTP,
};

// what the radio's clock is worked out from; the tests give their own
struct radio_clock_readings {
	int is_ntp_synced;
	long long rtc_s;
	long long system_s;
	long long since_start_s;
};
typedef void (*radio_clock_read_fn)(struct radio_clock_readings *readings);

int radio_clock_open(void);
enum radio_clock_source radio_clock_now(time_t *now);
void radio_clock_set_by_hand(time_t utc);
void radio_clock_poll(long now_ms);
void radio_clock_set_reading(radio_clock_read_fn read);
