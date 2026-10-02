// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// radio_clock.c — what time it is, for the radio: from the kernel when the
// kernel's clock can be trusted, and from the operator when it cannot.
//
// WHY THE RADIO HAS A CLOCK OF ITS OWN
//
// FT8 and the logbook need the real time. The zbitx has a clock chip, a
// DS3231, with a cell to keep it going while the radio is off, and the
// kernel sets its own clock from it at the start; when the network is up,
// NTP sets the kernel's clock and the kernel keeps the chip right, every
// eleven minutes, by itself. So with a good chip or a network, the
// kernel's clock is the time and there is nothing to do.
//
// But a chip whose cell is flat or missing starts from the year 2000, and
// then, with no network, the kernel's clock is wrong and the radio is of no
// use for FT8 or for logging. That happened on the author's own radio. So
// the operator can give the time, on the panel.
//
// The author's rules for that:
//
//   The chip is not written and the kernel's clock is not set. The kernel
//   does both by itself when the network comes up, so it would be
//   pointless; and it would need rights the radio program does not have.
//
//   The time the operator gives is kept as how far it is from the time
//   since the computer started, which runs steadily whatever the kernel's
//   clock does. While the radio program runs, that keeps the time. It is
//   not kept from one run to the next.
//
//   The kernel's clock, when it has been synchronised from the network, or
//   was read from a chip that had the time, comes before what the operator
//   gave.
//
// HOW THE KERNEL'S CLOCK IS JUDGED
//
// It has been synchronised from the network if adjtimex() says so: an NTP
// daemon clears the kernel's "unsynchronised" flag when it has the time.
//
// It was read from a chip that had the time if the chip now reads later
// than RADIO_CLOCK_EARLIEST, the start of 2026, before which no zbitx with
// this program existed, and agrees with the kernel's clock to within
// RTC_AGREES_WITHIN_S. A chip that lost its power counts on from 2000.
// The kernel's clock alone cannot be judged by its date: Raspberry Pi OS
// puts back, at the start, the time it had when it was last shut down,
// which looks likely and is wrong by however long the radio was off.
//
// This file has no controls: it is among the hardware's files because it
// is the clock chip and the kernel's clock that it reads. radio.c has the
// two controls that show the time and take it from the operator, CLOCK and
// TIME_SET.
//
// For the thread that owns the controls.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timex.h>
#include <time.h>

#include "radio_clock.h"

#define RTC_SINCE_EPOCH "/sys/class/rtc/rtc0/since_epoch"
#define RTC_AGREES_WITHIN_S 5
#define NO_RTC (-1LL)

static void read_the_kernel(struct radio_clock_readings *r) {
	struct timex kernel;
	struct timespec since_start;
	FILE *file;

	memset(&kernel, 0, sizeof(kernel));
	r->is_ntp_synced = adjtimex(&kernel) >= 0 && !(kernel.status & STA_UNSYNC);
	r->rtc_s = NO_RTC;
	file = fopen(RTC_SINCE_EPOCH, "r");
	if (file != NULL) {
		if (fscanf(file, "%lld", &r->rtc_s) != 1)
			r->rtc_s = NO_RTC;
		fclose(file);
	}
	r->system_s = (long long)time(NULL);
	clock_gettime(CLOCK_BOOTTIME, &since_start);
	r->since_start_s = (long long)since_start.tv_sec;
}

static radio_clock_read_fn read_readings = read_the_kernel;
static int is_set_by_hand;
static long long by_hand_less_since_start_s;

void radio_clock_set_reading(radio_clock_read_fn read) {
	read_readings = read;
}

enum radio_clock_source radio_clock_now(time_t *now) {
	struct radio_clock_readings r;
	long long apart;

	read_readings(&r);
	*now = (time_t)r.system_s;
	if (r.is_ntp_synced)
		return RADIO_CLOCK_NTP;
	apart = r.rtc_s - r.system_s;
	if (apart < 0)
		apart = -apart;
	if (r.rtc_s >= RADIO_CLOCK_EARLIEST && apart <= RTC_AGREES_WITHIN_S)
		return RADIO_CLOCK_RTC;
	if (is_set_by_hand) {
		*now = (time_t)(by_hand_less_since_start_s + r.since_start_s);
		return RADIO_CLOCK_SET_BY_HAND;
	}
	*now = 0;
	return RADIO_CLOCK_NOT_SET;
}

void radio_clock_set_by_hand(time_t utc) {
	struct radio_clock_readings r;

	read_readings(&r);
	by_hand_less_since_start_s = (long long)utc - r.since_start_s;
	is_set_by_hand = 1;
}
