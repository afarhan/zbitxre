// board.c — the zbitx board: what each of the Pi's pins does.
//
// Eight pins matter to the radio. Two switch power: one to the receiver's
// amplifiers and one to the transmitter's. Four choose the low-pass filter
// that follows the power amplifier, one filter for each stretch of the
// spectrum, and at most one of them is ever on. Two are inputs from the
// jacks.
//
//     pin  header  name on the schematic   what it does
//      15    10    RX_LINE                 receiver power, on when high
//      23    16    TX_LINE                 transmitter power, on when high
//       7    26    R-D                     filter for everything below 5.5 MHz
//       8    24    R-C                     filter from 5.5 to 10.5 MHz
//      25    22    R-B                     filter from 10.5 to 18.5 MHz
//      24    18    R-A                     filter from 18.5 to 30 MHz
//       4     7    PTT                     ring of the KEY jack, ring of the MIC jack
//       5    29    R-DASH                  tip of the KEY jack
//
// The filters keep the letters the schematic gives them.
//
// The inputs are pulled up and a closed contact grounds them, so a line reads
// low when its key or button is down. With a straight key only the tip
// closes. With a paddle the tip is one lever and the ring the other; which
// lever is the dot is the keyer's business, not the board's.
//
// Nothing here waits or decides an order. Going to transmit means turning the
// receiver off, releasing the filters, selecting one, and turning the
// transmitter on, with time for the relays between the steps; that order and
// its timing belong to whoever calls these.
//
// board_open() leaves the radio receiving: transmitter off, filters released,
// receiver on. Each pin is given its level before it becomes an output, so
// the transmitter cannot turn on in between.

#include <stddef.h>

#include "gpio.h"
#include "board.h"

struct filter {
	uint32_t below_hz;
	int pin;
};

static const struct filter filters[BOARD_FILTERS] = {
	{ 5500000u, BOARD_PIN_FILTER_D },
	{ 10500000u, BOARD_PIN_FILTER_C },
	{ 18500000u, BOARD_PIN_FILTER_B },
	{ BOARD_HIGHEST_HZ, BOARD_PIN_FILTER_A },
};

static int make_output_at(int pin, int level) {
	gpio_write(pin, level);
	return gpio_make_output(pin);
}

int board_open(void) {
	int i;

	if (gpio_open() < 0)
		return -1;
	if (make_output_at(BOARD_PIN_TRANSMITTER, 0) < 0)
		return -1;
	for (i = 0; i < BOARD_FILTERS; i++) {
		if (make_output_at(filters[i].pin, 0) < 0)
			return -1;
	}
	if (gpio_make_input(BOARD_PIN_PTT, GPIO_PULL_UP) < 0)
		return -1;
	if (gpio_make_input(BOARD_PIN_KEY, GPIO_PULL_UP) < 0)
		return -1;
	return make_output_at(BOARD_PIN_RECEIVER, 1);
}

void board_close(void) {
	board_transmitter_power(0);
	board_release_filters();
	board_receiver_power(1);
	gpio_close();
}

void board_receiver_power(int on) {
	gpio_write(BOARD_PIN_RECEIVER, on);
}

void board_transmitter_power(int on) {
	gpio_write(BOARD_PIN_TRANSMITTER, on);
}

int board_filter_pin_for(uint32_t hz) {
	int i;

	for (i = 0; i < BOARD_FILTERS; i++) {
		if (hz < filters[i].below_hz)
			return filters[i].pin;
	}
	return filters[BOARD_FILTERS - 1].pin;
}

void board_release_filters(void) {
	int i;

	for (i = 0; i < BOARD_FILTERS; i++)
		gpio_write(filters[i].pin, 0);
}

void board_select_filter(uint32_t hz) {
	board_release_filters();
	gpio_write(board_filter_pin_for(hz), 1);
}

int board_ptt_closed(void) {
	return gpio_read(BOARD_PIN_PTT) == 0;
}

int board_key_closed(void) {
	return gpio_read(BOARD_PIN_KEY) == 0;
}
