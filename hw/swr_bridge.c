// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// swr_bridge.c — the sbitx's SWR bridge, read through its ATtiny85.
//
// On the sbitx the bridge's two voltages, the forward and the reflected,
// go to an ATtiny85 on the board, which measures them and answers on the
// I2C bus the Si5351 is on, at address 8. This is read_power() of
// zbitxv2's sbitx.c, less what it worked out from the reading: four bytes
// are read from register 0, the forward voltage and then the reflected,
// each sixteen bits with the low byte first, in the ATtiny's own counts.
//
// The zbitx has no ATtiny. Its bridge goes to the front panel's own ADC,
// and front_panel/meter.cpp reads it there; board.h's is_bridge_on_i2c
// says which a board has.
//
// The read is a transfer on the bus, and hw/i2c.c says what that costs and
// why it is not for the thread that carries the audio.
//
// swr_bridge_read() returns 0 with the two counts, or -1 when the ATtiny
// does not answer.

#include <stdint.h>

#include "i2c.h"
#include "swr_bridge.h"

#define BRIDGE_ADDRESS 0x08
#define BRIDGE_REGISTER 0
#define BRIDGE_BYTES 4
#define BITS_A_BYTE 8

int swr_bridge_read(int *forward, int *reflected) {
	uint8_t bytes[BRIDGE_BYTES];

	if (i2c_read(BRIDGE_ADDRESS, BRIDGE_REGISTER, bytes, BRIDGE_BYTES) < 0)
		return -1;
	*forward = (int16_t)(bytes[0] | (bytes[1] << BITS_A_BYTE));
	*reflected = (int16_t)(bytes[2] | (bytes[3] << BITS_A_BYTE));
	return 0;
}
