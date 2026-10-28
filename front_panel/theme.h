// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// theme.h — the panel's colours, dark or light. See theme.cpp.

#pragma once

#include <stdint.h>

enum theme_kind {
	THEME_DARK,
	THEME_LIGHT,
};

//the colours LVGL paints with are 0xRRGGBB; the graph's, which
//waterfall.cpp paints itself, are as the display takes them, 5 bits of
//red, 6 of green, 5 of blue
struct theme {
	enum theme_kind kind;
	const char *name;
	uint32_t screen;
	uint32_t box;
	uint32_t edge;
	uint32_t title;
	uint32_t subtitle;
	uint32_t value;
	uint32_t dim;
	uint32_t focus;
	uint32_t taken_up;
	uint32_t other_row;
	uint32_t switched_on;
	uint32_t voip_on;
	uint32_t calling;
	uint32_t sent;
	uint32_t hz;
	uint32_t empty_hz;
	uint32_t bar;
	uint32_t prompt;
	uint32_t succeeded;
	uint32_t cancelled;
	uint32_t failed;
	uint16_t graph_back;
	uint16_t graph_line;
	uint16_t graph_under;
	uint16_t tuned_line;
	uint16_t passband_fill;
	uint16_t pitch_line;
};

extern const struct theme *theme;

void theme_choose();
enum theme_kind theme_other();
void theme_keep(enum theme_kind kind);
