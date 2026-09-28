// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// testcard.cpp — what the panel shows for a moment as it starts, to tell
// whether the display is wired, turned and coloured as the firmware thinks.
//
// Two rows of four bars. Left to right they are red, green, blue and white.
// The top row is filled by ili_fill_rect() and the bottom row is sent a
// pixel at a time by ili_push_pixels(), which is how LVGL's drawing gets to
// the display. The two rows must look the same.
//
// A small white square is put in the top left corner of the screen. If it
// is anywhere else, the display is turned the wrong way.

#include <Arduino.h>
#include "ili9488.h"
#include "testcard.h"

#define BARS 4
#define BAR_WIDTH (ILI_W / BARS)
#define ROW_HEIGHT (ILI_H / 2)
#define CORNER_SIZE 20
#define SHOWN_FOR_MS 4000

#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define WHITE 0xFFFF
#define BLACK 0x0000

static const uint16_t colours[BARS] = { RED, GREEN, BLUE, WHITE };
static uint16_t line[BAR_WIDTH];

static void bar_a_pixel_at_a_time(int x, int y, uint16_t colour) {
	int i;
	int row;

	for (i = 0; i < BAR_WIDTH; i++)
		line[i] = colour;
	for (row = 0; row < ROW_HEIGHT; row++) {
		ili_set_window(x, y + row, BAR_WIDTH, 1);
		ili_push_pixels(line, BAR_WIDTH);
	}
}

void testcard_show(void) {
	int i;

	for (i = 0; i < BARS; i++) {
		ili_fill_rect(i * BAR_WIDTH, 0, BAR_WIDTH, ROW_HEIGHT, colours[i]);
		bar_a_pixel_at_a_time(i * BAR_WIDTH, ROW_HEIGHT, colours[i]);
	}
	ili_fill_rect(0, 0, CORNER_SIZE, CORNER_SIZE, WHITE);
	ili_fill_rect(CORNER_SIZE, 0, CORNER_SIZE, CORNER_SIZE, BLACK);
	delay(SHOWN_FOR_MS);
}
