// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// meter.c — what the radio measures of itself: how strong the signal is
// while it receives, and on a board whose SWR bridge the Pi reads, the
// power and the SWR while it transmits.
//
// The readings are controls in the group METER, which radio.c describes:
// no list of the radio's controls shows that group and nothing in it is
// kept from one start to the next, but every client is told of a reading
// as of any control, when it changes. meter_poll() is called each time
// round the radio's loop and looks five times a second.
//
// SMETER
//
// The signal's strength is the core's, its calculate_s_meter(), which is
// W2JON's from zbitxv2: asked for with "smeter", it is the S units times a
// hundred, plus the dB left over above the last whole unit, so 704 is S7
// and 4 dB. SMETER is that number, as zbitxv2's clients had it. It is 0
// while the radio transmits.
//
// POWER AND SWR
//
// On the zbitx the front panel reads the bridge and says the power and the
// SWR; panel_controls.c takes them, and this file leaves them alone. On
// the sbitx an ATtiny85 on the board reads the bridge and the Pi asks it,
// hw/swr_bridge.c; board.h's is_bridge_on_i2c says which a board has.
//
// What is made of the ATtiny's two counts is read_power()'s of zbitxv2's
// sbitx.c. The forward count, times 40 and over the bridge's compensation,
// is the forward voltage in tenths of a volt; its square over 400 is the
// power in tenths of a watt, the bridge being scaled for 40 watts. The SWR,
// in tenths, is ten times (forward + reflected) / (forward - reflected),
// and 10.0 when the reflected is no less than the forward. Below three
// watts the bridge's diodes do not give a reading to trust, and the SWR is
// said to be 1.0, as zbitxv2's window did. With no power there is no SWR,
// 0, as the zbitx's panel says it; and both are 0 while the radio
// receives.
//
// zbitxv2 let the operator trim the bridge's compensation from 100, and
// cut the drive back when the power went too high, its ALC. Neither is
// here yet: the compensation is 100, and the power is only shown.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "control.h"
#include "meter.h"
#include "sdr.h"
#include "swr_bridge.h"

#define METER_EVERY_MS 200
#define SMETER_MAX 9999
#define ANSWER_MAX 100
#define BRIDGE_FULL_SCALE 40
#define BRIDGE_COMPENSATION 100
#define POWER_DIVISOR 400
#define POWER_MAX_TENTHS 999
#define POWER_TRUSTED_TENTHS 30
#define SWR_NONE 0
#define SWR_MATCHED 10
#define SWR_TOO_HIGH 100

static long next_look_ms;

static struct control smeter = {
	.group = "METER", .name = "SMETER", .kind = CONTROL_NUMBER,
	.min = 0, .max = SMETER_MAX, .step = 1, .value = "0",
};

static void update(const char *name, int value) {
	char text[CONTROL_VALUE_MAX];

	snprintf(text, sizeof(text), "%d", value);
	control_update(name, text);
}

static void read_smeter(void) {
	char request[] = "smeter=0";
	char answer[ANSWER_MAX];
	int strength;

	sdr_request(request, answer);
	strength = atoi(answer);
	if (strength < 0)
		strength = 0;
	if (strength > SMETER_MAX)
		strength = SMETER_MAX;
	update("SMETER", strength);
}

static void read_power(void) {
	int forward;
	int reflected;
	int tenths_of_a_volt;
	int power;
	int swr;

	if (swr_bridge_read(&forward, &reflected) < 0)
		return;
	if (reflected >= forward)
		swr = SWR_TOO_HIGH;
	else
		swr = (10 * (forward + reflected)) / (forward - reflected);
	if (swr > SWR_TOO_HIGH)
		swr = SWR_TOO_HIGH;
	tenths_of_a_volt = (forward * BRIDGE_FULL_SCALE) / BRIDGE_COMPENSATION;
	power = (tenths_of_a_volt * tenths_of_a_volt) / POWER_DIVISOR;
	if (power > POWER_MAX_TENTHS)
		power = POWER_MAX_TENTHS;
	if (power < POWER_TRUSTED_TENTHS)
		swr = SWR_MATCHED;
	if (power <= 0)
		swr = SWR_NONE;
	update("POWER", power);
	update("SWR", swr);
}

int meter_open(void) {
	return control_register(&smeter);
}

void meter_poll(long now_ms) {
	int is_transmitting = !strcmp(control_get("TX"), "ON");
	int is_bridge_ours = board_in_use()->is_bridge_on_i2c;

	if (now_ms < next_look_ms)
		return;
	next_look_ms = now_ms + METER_EVERY_MS;
	if (is_transmitting) {
		update("SMETER", 0);
		if (is_bridge_ours)
			read_power();
		return;
	}
	read_smeter();
	if (is_bridge_ours) {
		update("POWER", 0);
		update("SWR", SWR_NONE);
	}
}
