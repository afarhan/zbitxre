// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// knob.h — a rotary encoder on two of the Pi's pins. See knob.c.

#pragma once

struct knob {
	int a_pin;
	int b_pin;
	int seen_state;
	int prev_state;
	int history;
};

int knob_open(struct knob *k, int a_pin, int b_pin);
int knob_read(struct knob *k);
