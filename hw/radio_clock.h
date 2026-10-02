// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// radio_clock.h — what time it is, for the radio: from the kernel when the
// kernel's clock can be trusted, and from the operator when it cannot. See
// radio_clock.c.

#pragma once

#include <time.h>

// 2026-01-01 00:00:00 UTC: no zbitx with this program existed before it
#define RADIO_CLOCK_EARLIEST 1767225600LL

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

enum radio_clock_source radio_clock_now(time_t *now);
void radio_clock_set_by_hand(time_t utc);
void radio_clock_set_reading(radio_clock_read_fn read);
