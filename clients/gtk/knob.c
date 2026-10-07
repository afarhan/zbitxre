// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// knob.c — a rotary encoder on two of the Pi's pins, read by looking.
//
// The sbitx has two of them on its front, wired straight to the Pi; which
// pins, zbitx_gui.c says, and what a turn does. This is zbitxv2's
// enc_read(), from its sbitx_gtk.c, with its names where they are kept. It
// is the window's and is not built into the radio program.
//
// An encoder has two contacts, A and B, that close to ground one after the
// other as it turns, so the pair goes through four states in one order when
// turned one way and in the opposite order the other way. knob_read() is
// called often, a thousand times a second or more, and looks at the pair.
// Each change of state that is a step one way adds to history and each the
// other way takes from it. When history has passed KNOB_SPEED either way,
// the knob has turned a notch: knob_read() gives 1 or -1, and history
// starts again. Otherwise it gives 0.
//
// A contact bounces as it closes. zbitxv2 waited a millisecond and looked
// again; here a new state is taken only when it is seen on two calls
// running, so that nothing waits.
//
// The pins are made inputs with their pull-ups on. gpio_open() must have
// been called.

#include "gpio.h"
#include "knob.h"

// ENC_FAST in zbitxv2
#define KNOB_SPEED 1
#define A_IS_HIGH 1
#define B_IS_HIGH 2

static int enc_state(const struct knob *k) {
	int state = 0;

	if (gpio_read(k->a_pin))
		state += A_IS_HIGH;
	if (gpio_read(k->b_pin))
		state += B_IS_HIGH;
	return state;
}

int knob_open(struct knob *k, int a_pin, int b_pin) {
	k->a_pin = a_pin;
	k->b_pin = b_pin;
	if (gpio_make_input(a_pin, GPIO_PULL_UP) < 0)
		return -1;
	if (gpio_make_input(b_pin, GPIO_PULL_UP) < 0)
		return -1;
	k->prev_state = enc_state(k);
	k->seen_state = k->prev_state;
	k->history = 0;
	return 0;
}

int knob_read(struct knob *k) {
	int newState = enc_state(k);
	int was_seen = k->seen_state;

	k->seen_state = newState;
	if (newState != was_seen || newState == k->prev_state)
		return 0;

	//these transitions point to the encoder being rotated anti-clockwise
	if ((k->prev_state == 0 && newState == 2)
			|| (k->prev_state == 2 && newState == 3)
			|| (k->prev_state == 3 && newState == 1)
			|| (k->prev_state == 1 && newState == 0))
		k->history--;
	//these transitions point to the encoder being rotated clockwise
	if ((k->prev_state == 0 && newState == 1)
			|| (k->prev_state == 1 && newState == 3)
			|| (k->prev_state == 3 && newState == 2)
			|| (k->prev_state == 2 && newState == 0))
		k->history++;
	k->prev_state = newState;
	if (k->history > KNOB_SPEED) {
		k->history = 0;
		return 1;
	}
	if (k->history < -KNOB_SPEED) {
		k->history = 0;
		return -1;
	}
	return 0;
}
