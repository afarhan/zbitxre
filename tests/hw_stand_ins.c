// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// hw_stand_ins.c — the radio's hardware, stood in for, so that the radio can
// be set up and run on a computer that has none.
//
// A test that needs no radio is built with this file in place of the three
// files of hw/ that reach the hardware: gpio.c, i2c.c and audio.c. The rest
// of hw/, board.c and si5351.c, is the real thing, running on top of these.
// The test calls setup() as the radio program does, and setup() finds the
// pins, the I2C bus and the sound card all answering. Nothing is driven.
//
// The sound card does not start a thread here. audio_open() keeps the
// function it is given, and the test hands blocks to the radio itself, with
// hw_stand_in_block(), at whatever pace it likes.

#include <stddef.h>
#include <string.h>

#include "audio.h"
#include "gpio.h"
#include "i2c.h"
#include "hw_stand_ins.h"

#define PINS 64
#define PIN_IS_HIGH 1

static audio_block_fn radio_block;
static int pin_level[PINS];

int gpio_open(void) {
	int i;

	for (i = 0; i < PINS; i++)
		pin_level[i] = PIN_IS_HIGH;
	return 0;
}

void gpio_close(void) {
}

int gpio_make_output(int pin) {
	(void)pin;
	return 0;
}

int gpio_make_input(int pin, int pull) {
	(void)pin;
	(void)pull;
	return 0;
}

void gpio_write(int pin, int level) {
	if (pin >= 0 && pin < PINS)
		pin_level[pin] = level;
}

int gpio_read(int pin) {
	if (pin < 0 || pin >= PINS)
		return 0;
	return pin_level[pin];
}

int i2c_open(int bus) {
	(void)bus;
	return 0;
}

int i2c_open_bus_of(uint8_t address) {
	(void)address;
	return 0;
}

void i2c_close(void) {
}

int i2c_write(uint8_t address, uint8_t reg, const uint8_t *values, int count) {
	(void)address;
	(void)reg;
	(void)values;
	(void)count;
	return 0;
}

int i2c_read(uint8_t address, uint8_t reg, uint8_t *values, int count) {
	(void)address;
	(void)reg;
	memset(values, 0, count);
	return 0;
}

int audio_open(audio_block_fn fn) {
	radio_block = fn;
	return 0;
}

void audio_close(void) {
	radio_block = NULL;
}

int audio_set_speaker(int percent) {
	(void)percent;
	return 0;
}

int audio_set_receiver(int percent) {
	(void)percent;
	return 0;
}

void hw_stand_in_block(struct audio_block *block) {
	if (radio_block != NULL)
		radio_block(block);
}
