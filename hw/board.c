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
// A board also fixes two numbers the radio works from. The BFO frequency is
// where the second oscillator sits. The centre bin says how far from the BFO
// the radio works: the wanted signal is placed that many bins below the BFO,
// each bin being 46.875 Hz, and that is where it passes through the crystal
// filter. Both are the board's nominal values; a radio's own calibration may
// replace them.
//
// WHY THE ZBITX'S CENTRE BIN IS 600
//
// The middle of the band the sound card sees is bin 512, which is 24 kHz
// from the BFO, and the radio first worked there. It does not any more, and
// the reason is in the transmitter.
//
// The modulator does not suppress the BFO enough. At bin 512 the BFO lay
// inside the crystal filter's passband, so what leaked from the modulator
// went straight through the filter and out as a carrier.
//
// At bin 600 the radio works 28.125 kHz from the BFO. The BFO is then 4.125
// kHz further from where the radio works, off the passband and on the skirt
// of the filter, and the filter takes out what the modulator lets through.
//
// So this number is not a tuning convenience and must not be set back to
// the middle. A board with a different filter or a better modulator may
// want a different one, which is why it is the board's and not the radio's.
//
// WHY ONE POWER LINE GOES OFF BEFORE THE OTHER COMES ON
//
// These radios are bidirectional: the same mixers, filters and amplifiers
// carry the signal one way on receive and the other way on transmit, and the
// two power lines decide which way. The circuits hold their charge for a
// while after their line goes low. If the receiver's line is raised while the
// capacitors of the transmitter's circuits are still charged, both directions
// have gain at once and the radio oscillates by itself.
//
// So going to receive, the transmitter's line is switched off first, then
// there is a wait long enough for its capacitors to discharge fully, and only
// then is the receiver's line switched on. The wait may be made longer. It
// must not be made shorter or left out to make the radio switch faster.
//
// WHICH BOARD THE RADIO IS
//
// The radio's own file says, data/hw_settings.ini, as it did in sbitx.c:
//
//     radio=zbitx
//
// Each table has the name it goes by there, and board_find() gives the
// table of a name; board_named_in() reads the file for the name and gives
// its table. The program is the same for every radio and is not built for
// one; core_sdr.c reads the file before it touches a pin. A file from
// zbitxv2 has no name but has hw=4, its number for the zbitx, and that is
// taken for the name. A file that says neither names no board.
//
// To support another board, add its table here, name it in board.h and put
// it in the list board_find() looks through. A table's list of filters ends
// at the first one with no name.
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
// these calls with time between them, which the boards that switch with
// relays need and the ones with PIN diodes do not; the order and the timing
// belong to whoever calls.
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
// Where zbitxv2 and hermes-radio-daemon differ about the radio itself, the
// radio follows zbitxv2. The band edges are one such place: hermes-radio-daemon
// draws them at 5.7, 8, 18.5 and 35 MHz, which would put the 30 m band on
// filter B instead of C.

//
// The sbitx, third version, radio=sbitxv3. From sbitx.c, whose numbers for
// the pins are wiringPi's; they are given here as the Pi's own, as the
// zbitx's are. The pins turn out to be the zbitx's, less one: there is no
// RX_LINE. One line, TX_LINE, is the T/R line: high, the board transmits,
// low, it receives.
//
//     pin  header  name in sbitx.c   what it does
//      23    16    TX_LINE           transmit when high, receive when low
//       7    26    LPF_D             the filter put in below 5.5 MHz
//       8    24    LPF_C             the one put in from there to 10.5 MHz
//      25    22    LPF_B             from there to 18.5 MHz
//      24    18    LPF_A             from there to 30 MHz
//       4     7    PTT               ring of the KEY and MIC jacks
//       5    29    DASH              tip of the KEY jack
//
// The frequencies are where sbitx.c's set_lpf_40mhz() changes from one
// filter to the next. They are not the filters' own cut-off frequencies,
// which are the board's and are not written here.
//
// It differs from the zbitx in how it is run, and the table says so:
//
// The filters are in the receiver's path too. The antenna reaches the
// receiver through the low-pass filter, so one is selected whenever the
// radio is receiving, and a new one whenever the frequency moves to another
// filter's band. On the zbitx the filters are the transmitter's alone and
// are let go on receive. is_filter_in_receive says which.
//
// Going to transmit, sbitx.c's tr_switch_v2() lets the filters go, raises
// the T/R line, waits, and only then puts the filter in; the zbitx has its
// filter in before the transmitter is powered. is_power_before_filter keeps
// the sbitx's own order.
//
// It works at the middle of what the sound card sees, bin 512, 24 kHz from
// the BFO, with the BFO at 40.035 MHz, as sbitx.c has it.
//
// Its two knobs are wired to the Pi as well, but they are no business of
// this file or of the radio program: the window on the radio's screen reads
// them, and clients/gtk/zbitx_gui.c has their pins.
//
// The sbitx has no filter line E in use; sbitx.c names a pin for it, the
// zbitx's, and only the zbitx's routine drives it. It is left out here.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpio.h"
#include "board.h"

const struct board board_zbitx_1 = {
	.radio = "zbitx",
	.name = "zbitx, first board",
	.transmitter_pin = 23,
	.receiver_pin = 15,
	.ptt_pin = 4,
	.key_pin = 5,
	.bfo_hz = 40048000u,
	.centre_bin = 600,
	.filter = {
		{ "D", 7, 5500000u },
		{ "C", 8, 10500000u },
		{ "B", 25, 18500000u },
		{ "A", 24, 30000000u },
		{ "E", 12, BOARD_NO_BAND },
	},
};

const struct board board_sbitx_v3 = {
	.radio = "sbitxv3",
	.name = "sbitx, third version",
	.transmitter_pin = 23,
	.receiver_pin = BOARD_NO_PIN,
	.ptt_pin = 4,
	.key_pin = 5,
	.bfo_hz = 40035000u,
	.centre_bin = 512,
	.is_filter_in_receive = 1,
	.is_power_before_filter = 1,
	.filter = {
		{ "D", 7, 5500000u },
		{ "C", 8, 10500000u },
		{ "B", 25, 18500000u },
		{ "A", 24, 30000000u },
	},
};

static const struct board *const boards[] = {
	&board_zbitx_1,
	&board_sbitx_v3,
	NULL
};

static const struct board *board;
static int filters;

const struct board *board_find(const char *radio) {
	int i;

	if (radio == NULL)
		return NULL;
	for (i = 0; boards[i] != NULL; i++) {
		if (!strcmp(boards[i]->radio, radio))
			return boards[i];
	}
	return NULL;
}

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

#define SETTINGS_LINE_MAX 200
#define HW_OF_ZBITX 4

const struct board *board_named_in(const char *settings_file) {
	char line[SETTINGS_LINE_MAX];
	char name[SETTINGS_LINE_MAX];
	char value[SETTINGS_LINE_MAX];
	const struct board *by_name = NULL;
	const struct board *by_number = NULL;
	FILE *pf = fopen(settings_file, "r");

	if (pf == NULL)
		return NULL;
	while (fgets(line, sizeof(line), pf)) {
		if (sscanf(line, " %[^= ] = %s", name, value) != 2)
			continue;
		if (!strcmp(name, "radio"))
			by_name = board_find(value);
		if (!strcmp(name, "hw") && atoi(value) == HW_OF_ZBITX)
			by_number = &board_zbitx_1;
	}
	fclose(pf);
	if (by_name != NULL)
		return by_name;
	return by_number;
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
