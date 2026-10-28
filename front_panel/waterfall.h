// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// waterfall.h — the spectrum and the waterfall. See waterfall.cpp.

#pragma once

#include <stdint.h>

#define WATERFALL_POINTS 144
#define WATERFALL_ALL_ROWS 96

// where the graph and the waterfall are, and the corner at their top left
// that is left unpainted, for main.cpp to show the span in. Above them is
// the status line, STATUS_TOP, 16 high, which in CW has the decoded text
// running through it; and under them the bottom row
#define STATUS_TOP 96
#define STATUS_LINE_HEIGHT 16
#define WATERFALL_LEFT 48
#define WATERFALL_TOP (STATUS_TOP + STATUS_LINE_HEIGHT)
#define WATERFALL_WIDTH 288
#define WATERFALL_HEIGHT 160
#define WATERFALL_CORNER_WIDTH 40
#define WATERFALL_CORNER_ROWS 16
#define WATERFALL_NO_POINT (-1)

void waterfall_init(void);
void waterfall_update(const uint8_t *points);
void waterfall_modulation(const uint8_t *points);
void waterfall_slice(void);
void waterfall_redraw(void);
void waterfall_set_painting(bool on);
void waterfall_set_passband(int low_point, int high_point, int pitch_point);
bool waterfall_is_painting(void);
void waterfall_set_rows(int fall_rows);
void waterfall_to_serial(void);
void waterfall_report(void);
