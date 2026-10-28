// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// waterfall.cpp — the spectrum and the waterfall, below the controls.
//
// The radio sends a line of the spectrum a few times a second: 144 points,
// the lowest frequency first, each the strength in dB above the bottom of
// the display. The frequency the radio is tuned to is in the middle.
//
// The top of the space shows the newest line as a graph, with a line down
// the middle for the tuned frequency. Below it is the waterfall: every line
// that came, the newest at the top, the strength shown as a colour from
// blue through green and yellow to red.
//
// WHAT IS KEPT
//
// It is 288 pixels wide, six widgets' width, with a column of buttons on its left and the list of
// controls beside it; main.cpp has the whole layout.
//
// The waterfall is kept as the points themselves, a byte each, 96 lines
// of 144. They are kept in a ring, so that a new line costs nothing to put
// in; only where the newest line is, is moved. A point is two pixels wide
// on the screen.
//
// The corner at the top left, 40 by 16, is not painted here: main.cpp
// shows the span there.
//
// HOW IT IS PAINTED
//
// This is drawn straight to the display and not through LVGL. LVGL would
// want the whole picture in memory as pixels, which is more memory than the
// Pico has. LVGL is told nothing of this part of the screen and leaves it
// alone once it has painted it black.
//
// The display cannot scroll this way up, so a new line means sending the
// whole of the graph and the waterfall again: 46,080 pixels, three bytes
// each. That takes longer than the knob and the touch screen can be kept
// waiting. So it is painted a few rows at a time, one slice to each turn
// of loop(), and the screen shows the new line wiping down from the top.
//
// A line that arrives while the last is still being painted waits its
// turn. If another arrives before it is taken up, it replaces it.
//
// THE HASH, AND WHY THE WATERFALL IS NO DEEPER THAN IT IS
//
// Painting the display puts a hash in the receiver. It gets in on the line
// from the second IF to the sound card, and it is there for as long as the
// painting of a line lasts. The less there is to paint, the shorter each
// burst of it.
//
// The waterfall was 160 lines deep at first. The author listened to it at
// several depths, and with the display's port at 24 and at 6 MHz. At 24 MHz
// with the graph and 48 lines of waterfall the hash was gone. With 96 lines
// it was barely to be heard above the radio's own static, the antenna being
// out. A slower port measured quieter on average, but its bursts are four
// times as long, and the ear minds the length.
//
// The author settled on 96 lines. With an antenna in, the band's own noise
// is above what is left of the hash. WATERFALL_ALL_ROWS is that depth, and
// it is not to be raised to fill the screen without listening again.
//
// The passband, the part of the spectrum the receiver hears, is shaded in
// the graph, behind the spectrum's line, and in CW the pitch, where a
// station tuned in is heard, is a red line in the graph and a dotted one
// down the waterfall: dotted, since the station tuned in is right under it
// and a solid line would hide it. main.cpp says where they are, as points
// of the line, with waterfall_set_passband(); the graph is painted afresh
// when they move.
//
// WHILE THE RADIO TRANSMITS
//
// The radio sends, in the spectrum's place, the envelope of what is being
// sent, as it sends it to the window and the web page, and the graph shows
// it as the window does (draw_modulation() in clients/gtk/zbitx_gui.c): a
// yellow bar at each point, as far above the middle as below it, 255 for
// half the graph's height, over a dark blue with a grey line across the
// middle. It lets the operator see a voice, or 2TONE, flattening at the top
// when it is driven too hard. Only the graph is painted for it; the
// waterfall stands still until the spectrum comes again, as the window's
// does. The author asked for the window and the panel to show the same,
// on 28 October 2026, after the panel had shown nothing in transmit.
//
// waterfall_set_painting(false) stops the painting and leaves everything
// else going, so that what the painting puts in the receiver can be told by
// ear from what the link puts there. waterfall_set_rows() paints the graph
// and only so many rows of the waterfall below it.

#include <Arduino.h>
#include "ili9488.h"
#include "waterfall.h"

#define LEFT WATERFALL_LEFT
#define WIDTH WATERFALL_WIDTH
#define PIXELS_A_POINT (WIDTH / WATERFALL_POINTS)

#define GRAPH_TOP WATERFALL_TOP
#define GRAPH_ROWS 64
#define FALL_TOP (GRAPH_TOP + GRAPH_ROWS)
#define FALL_ROWS WATERFALL_ALL_ROWS
#define ALL_ROWS (GRAPH_ROWS + FALL_ROWS)
#define ROWS_A_SLICE 16

#define FULL_SCALE_DB 80
#define COLOURS 256
#define COLOUR_BANDS 5
#define SHADES_A_BAND 32
#define SHADES_PER_DB 2

#define RED_MAX 31
#define GREEN_MAX 63
#define BLUE_MAX 31
#define RED_SHIFT 11
#define GREEN_SHIFT 5

#define BLACK 0x0000
#define GRAPH_LINE 0xFFE0
#define GRAPH_UNDER 0x0167
#define TUNED_LINE 0x7BEF
#define PASSBAND_FILL 0x18C6
#define PITCH_LINE 0xC000
#define PITCH_DOTS_EVERY 4

#define NOTHING_TO_PAINT (-1)

//the window's colours for the envelope
#define MODULATION_TOP 255
#define MODULATION_BACK rgb(0, 3, 3)
#define MODULATION_MIDDLE rgb(6, 16, 9)
#define MODULATION_LINE rgb(31, 57, 0)
#define MODULATION_MIDDLE_ROW (GRAPH_ROWS / 2)

static uint8_t fall[FALL_ROWS][WATERFALL_POINTS];
static uint8_t newest[WATERFALL_POINTS];
static uint8_t waiting[WATERFALL_POINTS];
static uint8_t height_of[WATERFALL_POINTS];
static uint8_t modulation_waiting[WATERFALL_POINTS];
static uint8_t reach_of[WATERFALL_POINTS];
static uint16_t colour_of[COLOURS];
static uint16_t row_pixels[WIDTH];

static int newest_row;
static int next_row = NOTHING_TO_PAINT;
static bool is_waiting = false;
static bool is_modulation_waiting = false;
static bool is_modulation_shown = false;
static bool is_painting = true;
static int rows_to_paint = ALL_ROWS;
//the rows this painting goes down to: all of them, or the graph alone for
//the envelope
static int paint_until = ALL_ROWS;
static int passband_low = WATERFALL_NO_POINT;
static int passband_high = WATERFALL_NO_POINT;
static int pitch_at = WATERFALL_NO_POINT;
static bool copy_to_serial = false;

static unsigned long lines_taken;
static unsigned long lines_replaced;
static unsigned long envelopes_taken;
static unsigned long repaints;
static unsigned long last_repaint_ms;
static unsigned long longest_slice_us;
static unsigned long repaint_began_ms;

static uint16_t rgb(int red, int green, int blue){
	return (uint16_t)((red << RED_SHIFT) | (green << GREEN_SHIFT) | blue);
}

//blue rises, then green rises, then blue falls, then red rises, then green falls
static uint16_t heat_map(int shade){
	int band = shade / SHADES_A_BAND;
	int step = shade % SHADES_A_BAND;

	if (band == 0)
		return rgb(0, 0, step);
	if (band == 1)
		return rgb(0, step * 2, BLUE_MAX);
	if (band == 2)
		return rgb(0, GREEN_MAX, BLUE_MAX - step);
	if (band == 3)
		return rgb(step, GREEN_MAX, 0);
	if (band == 4)
		return rgb(RED_MAX, GREEN_MAX - step * 2, 0);
	return rgb(RED_MAX, 0, 0);
}

void waterfall_init(void){
	int i;

	for (i = 0; i < COLOURS; i++)
		colour_of[i] = heat_map(i * SHADES_PER_DB);
	memset(fall, 0, sizeof(fall));
	memset(newest, 0, sizeof(newest));
	newest_row = 0;
	next_row = 0;
}

void waterfall_update(const uint8_t *points){
	noInterrupts();
	if (is_waiting)
		lines_replaced = lines_replaced + 1;
	memcpy(waiting, points, WATERFALL_POINTS);
	is_waiting = true;
	interrupts();
}

void waterfall_modulation(const uint8_t *points){
	noInterrupts();
	memcpy(modulation_waiting, points, WATERFALL_POINTS);
	is_modulation_waiting = true;
	interrupts();
}

static void take_the_waiting_modulation(void){
	int i;

	noInterrupts();
	for (i = 0; i < WATERFALL_POINTS; i++)
		reach_of[i] = (uint8_t)((modulation_waiting[i] * MODULATION_MIDDLE_ROW)
			/ MODULATION_TOP);
	is_modulation_waiting = false;
	interrupts();

	is_modulation_shown = true;
	envelopes_taken = envelopes_taken + 1;
	next_row = 0;
	paint_until = GRAPH_ROWS;
	repaint_began_ms = millis();
}

static void take_the_waiting_line(void){
	int i;
	int height;

	noInterrupts();
	memcpy(newest, waiting, WATERFALL_POINTS);
	is_waiting = false;
	interrupts();

	newest_row = newest_row - 1;
	if (newest_row < 0)
		newest_row = FALL_ROWS - 1;
	memcpy(fall[newest_row], newest, WATERFALL_POINTS);

	for (i = 0; i < WATERFALL_POINTS; i++){
		height = (newest[i] * GRAPH_ROWS) / FULL_SCALE_DB;
		if (height > GRAPH_ROWS - 1)
			height = GRAPH_ROWS - 1;
		height_of[i] = (uint8_t)height;
	}
	lines_taken = lines_taken + 1;
	is_modulation_shown = false;
	next_row = 0;
	paint_until = rows_to_paint;
	repaint_began_ms = millis();
}

static uint16_t modulation_pixel(int point, int up){
	int from_the_middle = abs(up - MODULATION_MIDDLE_ROW);

	if (from_the_middle <= reach_of[point])
		return MODULATION_LINE;
	if (from_the_middle == 0)
		return MODULATION_MIDDLE;
	return MODULATION_BACK;
}

static uint16_t graph_pixel(int point, int up){
	int here = height_of[point];
	int before = here;
	int low;
	int high;

	if (point > 0)
		before = height_of[point - 1];
	low = here;
	high = before;
	if (before < here){
		low = before;
		high = here;
	}
	if (up >= low && up <= high)
		return GRAPH_LINE;
	if (point == pitch_at)
		return PITCH_LINE;
	if (point == WATERFALL_POINTS / 2)
		return TUNED_LINE;
	if (up < here)
		return GRAPH_UNDER;
	if (point >= passband_low && point <= passband_high)
		return PASSBAND_FILL;
	return BLACK;
}

static void make_a_row(int row){
	const uint8_t *points;
	uint16_t pixel;
	int up;
	int i;
	int p;

	for (i = 0; i < WATERFALL_POINTS; i++){
		if (row < GRAPH_ROWS){
			up = GRAPH_ROWS - 1 - row;
			if (is_modulation_shown)
				pixel = modulation_pixel(i, up);
			else
				pixel = graph_pixel(i, up);
		}
		else {
			points = fall[(newest_row + row - GRAPH_ROWS) % FALL_ROWS];
			pixel = colour_of[points[i]];
			if (i == pitch_at && row % PITCH_DOTS_EVERY == 0)
				pixel = PITCH_LINE;
		}
		for (p = 0; p < PIXELS_A_POINT; p++)
			row_pixels[i * PIXELS_A_POINT + p] = pixel;
	}
}

//rows of one kind are painted: all of them from "from" across to the
//right edge
static void paint_rows_from(int first, int count, int from){
	int row;

	if (count <= 0)
		return;
	ili_set_window(LEFT + from, GRAPH_TOP + first, WIDTH - from, count);
	for (row = first; row < first + count; row++){
		make_a_row(row);
		ili_push_pixels(row_pixels + from, WIDTH - from);
		if (copy_to_serial){
			Serial.printf("AREA %d %d %d %d\n", LEFT + from, GRAPH_TOP + row,
				WIDTH - from, 1);
			Serial.write((const uint8_t *)(row_pixels + from),
				(WIDTH - from) * sizeof(row_pixels[0]));
		}
	}
}

//the top rows are painted from the right of the corner that is kept for
//the span, and the rest from the left edge
static void paint_rows(int first, int count){
	int in_the_corner = WATERFALL_CORNER_ROWS - first;

	if (in_the_corner < 0)
		in_the_corner = 0;
	if (in_the_corner > count)
		in_the_corner = count;
	paint_rows_from(first, in_the_corner, WATERFALL_CORNER_WIDTH);
	paint_rows_from(first + in_the_corner, count - in_the_corner, 0);
}

void waterfall_slice(void){
	unsigned long began_us;
	unsigned long took_us;
	int count;

	if (next_row == NOTHING_TO_PAINT && is_waiting)
		take_the_waiting_line();
	if (next_row == NOTHING_TO_PAINT && is_modulation_waiting)
		take_the_waiting_modulation();
	if (next_row == NOTHING_TO_PAINT || !is_painting)
		return;

	count = paint_until - next_row;
	if (count > ROWS_A_SLICE)
		count = ROWS_A_SLICE;
	began_us = micros();
	paint_rows(next_row, count);
	took_us = micros() - began_us;
	if (took_us > longest_slice_us)
		longest_slice_us = took_us;

	next_row = next_row + count;
	if (next_row < paint_until)
		return;
	next_row = NOTHING_TO_PAINT;
	repaints = repaints + 1;
	last_repaint_ms = millis() - repaint_began_ms;
}

void waterfall_redraw(void){
	paint_until = rows_to_paint;
	if (next_row == NOTHING_TO_PAINT)
		next_row = 0;
}

//painting turned on paints the whole of it again, once: it is asked often,
//main_paint_when_free() deciding at every opening and closing
void waterfall_set_painting(bool on){
	if (on == is_painting)
		return;
	is_painting = on;
	if (on)
		waterfall_redraw();
}

//where the passband and the pitch are, as points of the line, or
//WATERFALL_NO_POINT; the graph is painted again if they have moved
void waterfall_set_passband(int low_point, int high_point, int pitch_point){
	if (low_point == passband_low && high_point == passband_high && pitch_point == pitch_at)
		return;
	passband_low = low_point;
	passband_high = high_point;
	pitch_at = pitch_point;
	waterfall_redraw();
}

bool waterfall_is_painting(void){
	return is_painting;
}

void waterfall_set_rows(int fall_rows){
	int rows;

	if (fall_rows < 0)
		fall_rows = 0;
	if (fall_rows > FALL_ROWS)
		fall_rows = FALL_ROWS;
	rows = GRAPH_ROWS + fall_rows;
	if (rows < rows_to_paint)
		ili_fill_rect(LEFT, GRAPH_TOP + rows, WIDTH, rows_to_paint - rows, BLACK);
	rows_to_paint = rows;
	paint_until = rows;
	next_row = NOTHING_TO_PAINT;
	waterfall_redraw();
}

void waterfall_to_serial(void){
	copy_to_serial = true;
	paint_rows(0, ALL_ROWS);
	copy_to_serial = false;
}

void waterfall_report(void){
	Serial.printf("waterfall: lines %lu, replaced before painting %lu, "
		"repaints %lu\n", lines_taken, lines_replaced, repaints);
	Serial.printf("waterfall: envelopes %lu\n", envelopes_taken);
	Serial.printf("waterfall: the last repaint took %lu ms, "
		"the longest slice %lu us\n", last_repaint_ms, longest_slice_us);
	if (!is_painting)
		Serial.printf("waterfall: painting is off\n");
}
