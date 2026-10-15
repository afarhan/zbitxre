// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_sbitxv3.c — that a radio named sbitxv3 in hw_settings.ini is run as
// the sbitx's third version is, and not as a zbitx.
//
// It needs no radio; it is built with tests/hw_stand_ins.c. The radio is
// named sbitxv3 and set up, and the pins are watched while it is tuned and
// while it goes to transmit and back. hw/board.c says what the sbitx's
// board is and how it differs:
//
// It has one T/R line and no line for the receiver, so the zbitx's RX_LINE
// pin must never be driven.
//
// Its low-pass filters are in the receiver's path. One must be in while it
// receives, the one for the frequency, and it must change when the
// frequency goes to another filter's band.
//
// Going to transmit, the filters are let go, the T/R line is raised, and
// then the filter is put in: the T/R line moves before the filter does.
// Coming back, the T/R line falls only after the filters are out, and the
// filter is put in again for the receiver.
//
// And it works at bin 512 with the BFO at 40.035 MHz, which the first
// oscillator's frequency shows.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "audio.h"
#include "board.h"
#include "gpio.h"
#include "sdr.h"
#include "hw_stand_ins.h"

#define ANSWER_MAX 100
#define ZBITX_RX_LINE 15
#define HZ_PER_BIN 46.875

static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static void ask(char *request) {
	char answer[ANSWER_MAX];

	sdr_request(request, answer);
}

static int filters_high(const struct board *board) {
	int count = 0;
	int i;

	for (i = 0; i < board_filters(); i++)
		count += gpio_read(board->filter[i].pin);
	return count;
}

static int pin_of_filter(uint32_t hz) {
	return board_filter_for(hz)->pin;
}

int main(void) {
	const struct board *board;
	char answer[ANSWER_MAX];
	long expected_hz;
	int pin_40m;
	int pin_20m;

	printf("sbitx v3:\n");
	hw_stand_in_radio("sbitxv3");
	if (setup() < 0) {
		printf("the radio did not open\n");
		return EXIT_FAILURE;
	}
	board = board_in_use();
	expect(board == &board_sbitx_v3, "the name sbitxv3 gives the sbitx's board");
	expect(board_find("zbitx") == &board_zbitx_1 && board_find("nothing") == NULL,
		"zbitx gives the zbitx's, and a name no board has gives none");
	expect(gpio_read(board->transmitter_pin) == 0, "it starts receiving");

	ask("r1:mode=LSB");
	ask("r1:freq=7100000");
	pin_40m = pin_of_filter(7100000);
	pin_20m = pin_of_filter(14200000);
	expect(gpio_read(pin_40m) == 1 && filters_high(board) == 1,
		"receiving on 7.1 MHz, the filter for it is in, and only that one");
	expected_hz = 7100000 + 40035000 - (long)(HZ_PER_BIN * 512);
	expect((long)oscillator_frequency() == expected_hz,
		"the oscillator is where bin 512 and a BFO of 40.035 MHz put it");

	ask("r1:freq=14200000");
	expect(pin_20m != pin_40m && gpio_read(pin_20m) == 1 && filters_high(board) == 1,
		"tuned to 14.2 MHz, the filter changes to that band's");

	ask("tx=on");
	expect(gpio_read(board->transmitter_pin) == 1, "asked to transmit, the T/R line is high");
	expect(gpio_read(pin_20m) == 1 && filters_high(board) == 1,
		"and the band's filter is in");
	expect(hw_stand_in_pin_changed_at(board->transmitter_pin)
			< hw_stand_in_pin_changed_at(pin_20m),
		"the T/R line went high before the filter was put in");

	ask("tx=off");
	expect(gpio_read(board->transmitter_pin) == 0, "back to receive, the T/R line is low");
	expect(gpio_read(pin_20m) == 1 && filters_high(board) == 1,
		"and the filter is in again for the receiver");
	expect(hw_stand_in_pin_changed_at(board->transmitter_pin)
			< hw_stand_in_pin_changed_at(pin_20m),
		"put in after the T/R line fell");
	expect(hw_stand_in_pin_changed_at(ZBITX_RX_LINE) == 0,
		"the zbitx's RX_LINE pin was never driven");

	// the mode VOIP has no transmitter: whatever asks, the T/R line stays
	// down. modems/modem_voip.c says why this is what keeps the finals off
	ask("r1:mode=VOIP");
	sdr_request("tx=on", answer);
	expect(!strcmp(answer, "error") && gpio_read(board->transmitter_pin) == 0,
		"in VOIP, asked to transmit, it says no and the T/R line stays low");
	ask("r1:mode=LSB");

	teardown();
	if (checks_failed) {
		printf("sbitx v3: %d checks FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("sbitx v3: all checks passed\n");
	return EXIT_SUCCESS;
}
