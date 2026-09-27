// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// board.c — a radio board: which of the Pi's pins does what.
//
// Thanks to hermes-radio-daemon (Rafael Diniz, Rhizomatica). The zbitx's pins
// and the order they are set up in follow its zbitx profile in radio.h and
// sbitx/sbitx_gpio.c, Copyright (C) 2023-2024 Rhizomatica.
//
// Boards differ, so a board is a table, and nothing here counts on how many
// lines a board has. A board has a line that powers its transmitter. It may
// have one that powers its receiver; the original sbitx has none, only the
// transmitter's. It has low-pass filters to follow the power amplifier, as
// many as its designer fitted. And it has two inputs from the jacks.
//
// To support another board, add its table here and name it in board.h. A
// table's list of filters ends at the first one with no name.
//
// A filter serves the frequencies below its edge, down to the edge of the
// filter listed before it, so the filters are listed from the lowest edge up.
// At most one filter line is ever high. A frequency above the last edge takes
// the last filter. A filter line with no band is wired but serves nothing on
// this board: it is held low with the others and never selected.
//
// The inputs are pulled up and a closed contact grounds them, so a line reads
// low when its key or button is down. With a straight key only the tip
// closes. With a paddle the tip is one lever and the ring the other; which
// lever is the dot is the keyer's business, not the board's.
//
// Nothing here waits or decides an order. Going to transmit is a sequence of
// these calls with time for the relays between them; the order and the
// timing belong to whoever calls.
//
// board_open() leaves the radio receiving: transmitter off, filters released,
// receiver on if there is a line for it. Each pin is given its level before
// it becomes an output, so the transmitter cannot turn on in between.
//
// The zbitx, first board. It is hw=4 in zbitxv2's hw_settings.ini.
//
//     pin  header  name on the schematic   what it does
//      23    16    TX_LINE                 transmitter power, on when high
//      15    10    RX_LINE                 receiver power, on when high
//       7    26    R-D                     filter below 5.5 MHz
//       8    24    R-C                     filter from 5.5 to 10.5 MHz
//      25    22    R-B                     filter from 10.5 to 18.5 MHz
//      24    18    R-A                     filter from 18.5 to 30 MHz
//      12    32                            a filter line with no band
//       4     7    PTT                     ring of the KEY and MIC jacks
//       5    29    R-DASH                  tip of the KEY jack
//
// Its band edges are zbitxv2's. hermes-radio-daemon draws them at 5.7, 8,
// 18.5 and 35 MHz, which puts the 30 m band on filter B instead of C.

#include <stddef.h>

#include "gpio.h"
#include "board.h"

const struct board board_zbitx_1 = {
	.name = "zbitx, first board",
	.transmitter_pin = 23,
	.receiver_pin = 15,
	.ptt_pin = 4,
	.key_pin = 5,
	.filter = {
		{ "D", 7, 5500000u },
		{ "C", 8, 10500000u },
		{ "B", 25, 18500000u },
		{ "A", 24, 30000000u },
		{ "E", 12, BOARD_NO_BAND },
	},
};

static const struct board *board;
static int filters;

static int make_output_at(int pin, int level) {
	gpio_write(pin, level);
	return gpio_make_output(pin);
}

static int count_filters(const struct board *which) {
	int count;

	count = 0;
	while (count < BOARD_FILTERS_MAX && which->filter[count].name != NULL)
		count++;
	return count;
}

int board_open(const struct board *which) {
	int i;

	if (which == NULL)
		return -1;
	if (gpio_open() < 0)
		return -1;
	board = which;
	filters = count_filters(which);

	if (make_output_at(board->transmitter_pin, 0) < 0)
		return -1;
	for (i = 0; i < filters; i++) {
		if (make_output_at(board->filter[i].pin, 0) < 0)
			return -1;
	}
	if (gpio_make_input(board->ptt_pin, GPIO_PULL_UP) < 0)
		return -1;
	if (gpio_make_input(board->key_pin, GPIO_PULL_UP) < 0)
		return -1;
	if (board->receiver_pin == BOARD_NO_PIN)
		return 0;
	return make_output_at(board->receiver_pin, 1);
}

void board_close(void) {
	if (board == NULL)
		return;
	board_transmitter_power(0);
	board_release_filters();
	board_receiver_power(1);
	gpio_close();
	board = NULL;
}

const struct board *board_in_use(void) {
	return board;
}

int board_filters(void) {
	if (board == NULL)
		return 0;
	return filters;
}

void board_receiver_power(int on) {
	if (board == NULL || board->receiver_pin == BOARD_NO_PIN)
		return;
	gpio_write(board->receiver_pin, on);
}

void board_transmitter_power(int on) {
	if (board == NULL)
		return;
	gpio_write(board->transmitter_pin, on);
}

const struct board_filter *board_filter_for(uint32_t hz) {
	const struct board_filter *last;
	int i;

	if (board == NULL)
		return NULL;
	last = NULL;
	for (i = 0; i < filters; i++) {
		if (board->filter[i].below_hz == BOARD_NO_BAND)
			continue;
		last = &board->filter[i];
		if (hz < last->below_hz)
			return last;
	}
	return last;
}

void board_release_filters(void) {
	int i;

	if (board == NULL)
		return;
	for (i = 0; i < filters; i++)
		gpio_write(board->filter[i].pin, 0);
}

void board_select_filter(uint32_t hz) {
	const struct board_filter *chosen;

	board_release_filters();
	chosen = board_filter_for(hz);
	if (chosen == NULL)
		return;
	gpio_write(chosen->pin, 1);
}

int board_ptt_closed(void) {
	if (board == NULL)
		return 0;
	return gpio_read(board->ptt_pin) == 0;
}

int board_key_closed(void) {
	if (board == NULL)
		return 0;
	return gpio_read(board->key_pin) == 0;
}
