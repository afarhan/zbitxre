// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// board.h — a radio board: which of the Pi's pins does what. See board.c.

#pragma once

#include <stdint.h>

#define BOARD_FILTERS_MAX 8
#define BOARD_NO_PIN      (-1)
#define BOARD_NO_BAND     0u

struct board_filter {
	const char *name;
	int pin;
	uint32_t below_hz;
};

struct board {
	const char *radio;
	const char *name;
	int transmitter_pin;
	int receiver_pin;
	int ptt_pin;
	int key_pin;
	uint32_t bfo_hz;
	int centre_bin;
	int is_filter_in_receive;
	int is_power_before_filter;
	int is_bridge_on_i2c;
	struct board_filter filter[BOARD_FILTERS_MAX + 1];
};

extern const struct board board_zbitx_1;
extern const struct board board_sbitx_v3;

const struct board *board_find(const char *radio);
const struct board *board_named_in(const char *settings_file);
int board_open(const struct board *which);
void board_close(void);
const struct board *board_in_use(void);
int board_filters(void);

void board_receiver_power(int on);
void board_transmitter_power(int on);
const struct board_filter *board_filter_for(uint32_t hz);
void board_select_filter(uint32_t hz);
void board_release_filters(void);
int board_ptt_closed(void);
int board_key_closed(void);
