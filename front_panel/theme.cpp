// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// theme.cpp — the panel's colours, dark or light.
//
// The dark theme is the panel as it always was: white and cyan on black,
// with amber for what the knob is on. The light theme, the author's asking
// of 28 October 2026, is black on white, for a panel used in daylight, with
// a dark teal for the controls' names, deep orange for what the knob is on
// and pale amber for a row marked or a control taken up. TX keeps its red
// and VoIP its blue in both, a little lighter on white so that the black
// letters on them can be read.
//
// Every colour the panel paints is here; the files that paint name them
// through the theme in use, theme->box and the rest. The graph and the
// waterfall are painted by waterfall.cpp itself, and their colours are as
// the display takes them; the waterfall's shades are worked out there,
// from dark to bright on black and from white to dark on white.
//
// The theme is kept in the panel's own flash, with the touch screen's
// calibration (block.cpp), and not on the radio, since it is the panel's
// glass and nothing else's. It is chosen once, as the panel starts, before
// anything is drawn. Changing it restarts the panel: every screen is made
// once and keeps the colours it was made with, and painting them all again
// in the other colours would need the colour of every part of every screen
// kept twice over, for little use.
//
// A panel that was never given a theme is dark: the word in the block that
// says light is a number, THEME_LIGHT_MARK, that the flash of a panel from
// before has nowhere in that place.

#include <Arduino.h>
#include "ui.h"
#include "theme.h"

#define THEME_LIGHT_MARK 0x4C494748

static const struct theme dark = {
	.kind = THEME_DARK,
	.name = "Dark",
	.screen = 0x000000,
	.box = 0x101820,
	.edge = 0x405060,
	.title = 0x00FFFF,
	.subtitle = 0x90A0B0,
	.value = 0xFFFFFF,
	.dim = 0x506070,
	.focus = 0xFFC000,
	.taken_up = 0x604800,
	.other_row = 0x1C2834,
	.switched_on = 0xB02020,
	.voip_on = 0x2050C0,
	.calling = 0x60E060,
	.sent = 0xFF6060,
	.hz = 0x00FFFF,
	.empty_hz = 0x4A5A68,
	.bar = 0x90A0B0,
	.prompt = 0x406070,
	.succeeded = 0x00E060,
	.cancelled = 0xFFC000,
	.failed = 0xFF4040,
	.graph_back = 0x0000,
	.graph_line = 0xFFE0,
	.graph_under = 0x0167,
	.tuned_line = 0x7BEF,
	.passband_fill = 0x18C6,
	.pitch_line = 0xC000,
};

static const struct theme light = {
	.kind = THEME_LIGHT,
	.name = "Light",
	.screen = 0xFFFFFF,
	.box = 0xF2F4F7,
	.edge = 0xB0BAC4,
	.title = 0x006A80,
	.subtitle = 0x50606E,
	.value = 0x000000,
	.dim = 0xA8B0B8,
	.focus = 0xE06000,
	.taken_up = 0xFFD98A,
	.other_row = 0xE6EBF0,
	.switched_on = 0xF04848,
	.voip_on = 0x6A98F4,
	.calling = 0x007A00,
	.sent = 0xC00000,
	.hz = 0x006A80,
	.empty_hz = 0xA8B0B8,
	.bar = 0x8090A0,
	.prompt = 0x90A0B0,
	.succeeded = 0x008A30,
	.cancelled = 0xC06000,
	.failed = 0xD00000,
	.graph_back = 0xFFFF,
	.graph_line = 0x0010,
	.graph_under = 0xC69F,
	.tuned_line = 0x7BEF,
	.passband_fill = 0xE75D,
	.pitch_line = 0xC000,
};

const struct theme *theme = &dark;

void theme_choose(){
	theme = &dark;
	if (block.theme == THEME_LIGHT_MARK)
		theme = &light;
	Serial.printf("theme %s\n", theme->name);
}

enum theme_kind theme_other(){
	if (theme->kind == THEME_LIGHT)
		return THEME_DARK;
	return THEME_LIGHT;
}

//kept in the flash, for the next start
void theme_keep(enum theme_kind kind){
	block.theme = 0;
	if (kind == THEME_LIGHT)
		block.theme = THEME_LIGHT_MARK;
	block_write();
}
