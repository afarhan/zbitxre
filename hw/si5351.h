// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// si5351.h — the Si5351 clock generator. See si5351.c.

#pragma once

#include <stdint.h>

#define SI5351_ADDRESS 0x60
#define SI5351_CLOCKS 3
#define SI5351_MIN_HZ 500000u
#define SI5351_MAX_HZ 125000000u

#define SI5351_STATUS_STARTING      0x80
#define SI5351_STATUS_PLL_B_NO_LOCK 0x40
#define SI5351_STATUS_PLL_A_NO_LOCK 0x20

int si5351_init(void);
int si5351_reset_plls(void);
void si5351_set_crystal(uint32_t hz);
int si5351_set_freq(int clock, uint32_t hz);
int si5351_clock_off(int clock);
int si5351_read_status(uint8_t *status);
int si5351_write_failures(void);
