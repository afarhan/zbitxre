// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// radio.h — the radio as its operator sees it. See radio.c.

#pragma once

#include <stdio.h>

int radio_open(void);
void radio_poll(long now_ms);
void radio_save_bands(FILE *file);
int radio_load_band(const char *name, const char *value);
