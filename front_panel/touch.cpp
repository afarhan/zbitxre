// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// touch.cpp — the touch screen: an XPT2046 on the display's SPI port.
//
// The way the controller is read, and the way a reading is turned into a
// place on the screen, follow Touch.cpp of TFT_eSPI by Bodmer, by way of the
// zbitx's shipping firmware. The calibration is kept in the same five
// numbers, so a panel calibrated under that firmware needs no calibrating
// again.
//
// The controller measures three things: how hard the screen is pressed, and
// where, across and down. A touch is believed only if the screen is pressed
// harder than a threshold and two readings of the place, 3 ms apart, agree.
//
// The controller answers a request while the next one is being sent. So the
// two bytes of an answer are collected by sending nothing and then the next
// request, which is what exchange16() does.
//
// The pressure is read again and again until it stops rising, because a
// finger arriving is still pressing harder each time.
//
// A reading is a number from 0 to 4095 along each edge of the glass, and the
// glass is not lined up with the display. The calibration says which reading
// is across and which is down, where each begins, how far it runs, and
// whether it runs backwards.
//
// touch_calibrate() shows a mark in each corner in turn and waits for it to
// be touched. It takes eight readings of each corner.
//
// The display's driver sends and never reads, so whatever came back while
// it was sending is still waiting in the port. It is thrown away before the
// controller is spoken to, or it would be taken for the controller's answer.
//
// The port is the display's. Before every reading the display is let go of
// and the port slowed to what the controller can follow, and after it the
// display is taken back. So nothing here may be called while the display is
// being sent to. Both are called from loop(), one after the other.

#include <Arduino.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include "ili9488.h"
#include "touch.h"

#define PIN_TOUCH_CS 9
#define TOUCH_PORT spi1
#define TOUCH_HZ 2500000u

#define READ_X 0xD0
#define READ_Y 0x90
#define READ_Z1 0xB0
#define READ_Z2 0xC0
#define NOTHING 0x00
#define HIGHEST_READING 0xFFF

#define PRESSED_HARDER_THAN 350
#define CALIBRATING_HARDER_THAN 150
#define READINGS_MUST_AGREE_WITHIN 20
#define TRIES 5
#define CORNERS 4
#define READINGS_OF_A_CORNER 8
#define MARK_SIZE 15
#define MARK_THICKNESS 3
#define BETWEEN_CORNERS_MS 1000

#define WHITE 0xFFFF
#define RED 0xF800

#define ROTATED 0x01
#define X_BACKWARDS 0x02
#define Y_BACKWARDS 0x04

static uint16_t x_begins = 1;
static uint16_t x_runs = 1;
static uint16_t y_begins = 1;
static uint16_t y_runs = 1;
static bool is_rotated = false;
static bool x_is_backwards = false;
static bool y_is_backwards = false;

static void forget_what_came_back(void) {
	while (spi_is_readable(TOUCH_PORT))
		(void)spi_get_hw(TOUCH_PORT)->dr;
	spi_get_hw(TOUCH_PORT)->icr = SPI_SSPICR_RORIC_BITS;
}

static void begin(void) {
	ili_release();
	forget_what_came_back();
	spi_set_baudrate(TOUCH_PORT, TOUCH_HZ);
	gpio_put(PIN_TOUCH_CS, 0);
}

static void end(void) {
	gpio_put(PIN_TOUCH_CS, 1);
	ili_select();
}

static uint8_t exchange(uint8_t out) {
	uint8_t in;

	spi_write_read_blocking(TOUCH_PORT, &out, &in, 1);
	return in;
}

static uint16_t exchange16(uint8_t then) {
	uint16_t in;

	in = (uint16_t)exchange(NOTHING) << 8;
	in |= exchange(then);
	return in;
}

static void read_place(uint16_t *x, uint16_t *y) {
	uint16_t reading;

	begin();

	exchange(READ_X);
	exchange(NOTHING);
	exchange(READ_X);
	exchange(NOTHING);
	exchange(READ_X);
	exchange(NOTHING);
	exchange(READ_X);
	reading = (uint16_t)exchange(NOTHING) << 5;
	reading |= exchange(READ_Y) >> 3;
	*x = reading;

	exchange(NOTHING);
	exchange(READ_Y);
	exchange(NOTHING);
	exchange(READ_Y);
	exchange(NOTHING);
	exchange(READ_Y);
	reading = (uint16_t)exchange(NOTHING) << 5;
	reading |= exchange(NOTHING) >> 3;
	*y = reading;

	end();
}

static uint16_t read_pressure(void) {
	int16_t pressure;

	begin();
	pressure = HIGHEST_READING;
	exchange(READ_Z1);
	pressure += (int16_t)(exchange16(READ_Z2) >> 3);
	pressure -= (int16_t)(exchange16(NOTHING) >> 3);
	end();

	if (pressure == HIGHEST_READING)
		pressure = 0;
	return (uint16_t)pressure;
}

static bool read_if_pressed(uint16_t *x, uint16_t *y, uint16_t harder_than) {
	uint16_t x1, y1, x2, y2;
	uint16_t now = 1;
	uint16_t before = 0;

	while (now > before) {
		before = now;
		now = read_pressure();
		delay(1);
	}
	if (now <= harder_than)
		return false;

	read_place(&x1, &y1);
	delay(1);
	if (read_pressure() <= harder_than)
		return false;
	delay(2);
	read_place(&x2, &y2);

	if (abs((int16_t)x1 - (int16_t)x2) > READINGS_MUST_AGREE_WITHIN)
		return false;
	if (abs((int16_t)y1 - (int16_t)y2) > READINGS_MUST_AGREE_WITHIN)
		return false;
	*x = x1;
	*y = y1;
	return true;
}

static void place_on_the_screen(uint16_t *x, uint16_t *y) {
	uint16_t across = *x;
	uint16_t down = *y;

	if (is_rotated) {
		across = *y;
		down = *x;
	}
	across = (uint16_t)((int32_t)(across - x_begins) * ILI_W / x_runs);
	down = (uint16_t)((int32_t)(down - y_begins) * ILI_H / y_runs);
	if (x_is_backwards)
		across = (uint16_t)(ILI_W - across);
	if (y_is_backwards)
		down = (uint16_t)(ILI_H - down);
	*x = across;
	*y = down;
}

static uint16_t never_zero(uint16_t value) {
	if (value == 0)
		return 1;
	return value;
}

void touch_init(void) {
	gpio_init(PIN_TOUCH_CS);
	gpio_set_dir(PIN_TOUCH_CS, GPIO_OUT);
	gpio_put(PIN_TOUCH_CS, 1);
}

bool touch_read(uint16_t *x, uint16_t *y) {
	uint16_t across, down;
	int good = 0;
	int i;

	for (i = 0; i < TRIES; i++) {
		if (read_if_pressed(&across, &down, PRESSED_HARDER_THAN))
			good++;
	}
	if (good == 0)
		return false;
	place_on_the_screen(&across, &down);
	*x = across;
	*y = down;
	return true;
}

void touch_set_calibration(const uint16_t *values) {
	x_begins = never_zero(values[0]);
	x_runs = never_zero(values[1]);
	y_begins = never_zero(values[2]);
	y_runs = never_zero(values[3]);
	is_rotated = (values[4] & ROTATED) != 0;
	x_is_backwards = (values[4] & X_BACKWARDS) != 0;
	y_is_backwards = (values[4] & Y_BACKWARDS) != 0;
}

static void mark_a_corner(int corner) {
	int x = 0;
	int y = 0;

	ili_fill_rect(0, 0, ILI_W, ILI_H, RED);
	if (corner == 1 || corner == 3)
		y = ILI_H - MARK_SIZE;
	if (corner == 2 || corner == 3)
		x = ILI_W - MARK_SIZE;
	ili_fill_rect(x, y, MARK_SIZE, MARK_SIZE, WHITE);
	if (corner == 0)
		ili_fill_rect(MARK_THICKNESS, MARK_THICKNESS, MARK_SIZE, MARK_SIZE, RED);
	if (corner == 1)
		ili_fill_rect(MARK_THICKNESS, y - MARK_THICKNESS, MARK_SIZE, MARK_SIZE, RED);
	if (corner == 2)
		ili_fill_rect(x - MARK_THICKNESS, MARK_THICKNESS, MARK_SIZE, MARK_SIZE, RED);
	if (corner == 3)
		ili_fill_rect(x - MARK_THICKNESS, y - MARK_THICKNESS, MARK_SIZE, MARK_SIZE, RED);
}

static void swap(uint16_t *a, uint16_t *b) {
	uint16_t kept = *a;

	*a = *b;
	*b = kept;
}

void touch_calibrate(uint16_t *values) {
	int16_t at[CORNERS * 2] = {0, 0, 0, 0, 0, 0, 0, 0};
	uint16_t x, y;
	uint16_t flags;
	int corner;
	int i;

	for (corner = 0; corner < CORNERS; corner++) {
		mark_a_corner(corner);
		if (corner > 0)
			delay(BETWEEN_CORNERS_MS);
		for (i = 0; i < READINGS_OF_A_CORNER; i++) {
			while (!read_if_pressed(&x, &y, CALIBRATING_HARDER_THAN))
				;
			at[corner * 2] += (int16_t)x;
			at[corner * 2 + 1] += (int16_t)y;
		}
		at[corner * 2] /= READINGS_OF_A_CORNER;
		at[corner * 2 + 1] /= READINGS_OF_A_CORNER;
	}

	is_rotated = abs(at[0] - at[2]) > abs(at[1] - at[3]);
	if (is_rotated) {
		x_begins = (uint16_t)((at[1] + at[3]) / 2);
		x_runs = (uint16_t)((at[5] + at[7]) / 2);
		y_begins = (uint16_t)((at[0] + at[4]) / 2);
		y_runs = (uint16_t)((at[2] + at[6]) / 2);
	} else {
		x_begins = (uint16_t)((at[0] + at[2]) / 2);
		x_runs = (uint16_t)((at[4] + at[6]) / 2);
		y_begins = (uint16_t)((at[1] + at[5]) / 2);
		y_runs = (uint16_t)((at[3] + at[7]) / 2);
	}

	x_is_backwards = false;
	if (x_begins > x_runs) {
		swap(&x_begins, &x_runs);
		x_is_backwards = true;
	}
	y_is_backwards = false;
	if (y_begins > y_runs) {
		swap(&y_begins, &y_runs);
		y_is_backwards = true;
	}
	x_runs -= x_begins;
	y_runs -= y_begins;
	x_begins = never_zero(x_begins);
	x_runs = never_zero(x_runs);
	y_begins = never_zero(y_begins);
	y_runs = never_zero(y_runs);

	flags = 0;
	if (is_rotated)
		flags |= ROTATED;
	if (x_is_backwards)
		flags |= X_BACKWARDS;
	if (y_is_backwards)
		flags |= Y_BACKWARDS;
	values[0] = x_begins;
	values[1] = x_runs;
	values[2] = y_begins;
	values[3] = y_runs;
	values[4] = flags;
}
