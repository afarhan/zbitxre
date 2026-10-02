// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// waterfall.h — the spectrum and the waterfall. See waterfall.cpp.

#pragma once

#include <stdint.h>

#define WATERFALL_POINTS 144
#define WATERFALL_ALL_ROWS 96

// where the graph and the waterfall are, and the corner at their top left
// that is left unpainted, for main.cpp to show the span in
#define WATERFALL_LEFT 48
#define WATERFALL_TOP 96
#define WATERFALL_WIDTH 288
#define WATERFALL_HEIGHT 160
#define WATERFALL_CORNER_WIDTH 40
#define WATERFALL_CORNER_ROWS 16

void waterfall_init(void);
void waterfall_update(const uint8_t *points);
void waterfall_slice(void);
void waterfall_redraw(void);
void waterfall_set_painting(bool on);
void waterfall_set_rows(int fall_rows);
void waterfall_to_serial(void);
void waterfall_report(void);
