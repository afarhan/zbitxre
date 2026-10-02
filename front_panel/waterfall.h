// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// waterfall.h — the spectrum and the waterfall. See waterfall.cpp.

#pragma once

#include <stdint.h>

#define WATERFALL_POINTS 144
#define WATERFALL_ALL_ROWS 96

void waterfall_init(void);
void waterfall_update(const uint8_t *points);
void waterfall_slice(void);
void waterfall_redraw(void);
void waterfall_set_painting(bool on);
void waterfall_set_rows(int fall_rows);
void waterfall_to_serial(void);
void waterfall_report(void);
