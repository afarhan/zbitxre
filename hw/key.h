// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// key.h — the key jack's two contacts, each closure and opening with its
// time. See key.c.

#pragma once

#include <stdint.h>

#include "board.h"

#define KEY_TIP  0
#define KEY_RING 1

struct key_event {
	int contact;
	int is_closed;
	int64_t us;
};

int key_open(const struct board *board);
void key_close(void);
int key_take_event(struct key_event *event);
int64_t key_now_us(void);
