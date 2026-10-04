// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_cw.h — what of CW's modem the rest of the radio uses. See
// modem_cw.c.

#pragma once

int cw_rx_take(char *text, int room);
int cw_rx_take_spectrum(char *text, int room);
