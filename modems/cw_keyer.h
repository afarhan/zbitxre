// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// cw_keyer.h — the CW keyer: paddles or a key in, a keyed and shaped
// envelope out, a sample at a time. See cw_keyer.c.

#pragma once

#define CW_KEYER_RATE_HZ 96000

// the names are those of the CW_INPUT control
enum cw_keyer_mode {
	CW_KEYER_STRAIGHT,
	CW_KEYER_IAMBIC,
	CW_KEYER_IAMBICB,
	CW_KEYER_BUG,
	CW_KEYER_ULTIMATIC,
};

void cw_keyer_reset(void);
void cw_keyer_set_mode(enum cw_keyer_mode mode);
void cw_keyer_set_wpm(int wpm);
int cw_keyer_is_key_down(int is_dot_closed, int is_dash_closed);
int cw_keyer_is_busy(void);
float cw_keyer_shape(int is_key_down);
