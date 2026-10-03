// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_ft8.h — what of FT8's modem ft8_qso.c uses. See modem_ft8.c.

#pragma once

#include <time.h>

double ft8_time_now(void);
time_t ft8_searched_slot(void);
int ft8_tx_arm(const char *message, int pitch_hz, time_t slot_start);
void ft8_tx_cancel(void);
int ft8_tx_is_busy(void);
