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
//
// The key jack is stood in for too, in place of hw/key.c. The clock that
// key_now_us() reads is the count of samples handed over so far, so the
// test's time moves only as blocks go by. hw_stand_in_key() closes or opens
// a contact at a time from now, and key_take_event() reports it once that
// time has come, moving the contact's pin as it does.
//
// Which radio it is, setup() reads from the radio's hw_settings.ini. A test
// says which with hw_stand_in_radio() before it calls setup(): a file of
// that one line is written for the occasion and the core is pointed at it.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "gpio.h"
#include "i2c.h"
#include "board.h"
#include "key.h"
#include "sdr.h"
#include "hw_stand_ins.h"

#define PINS 64
#define PIN_IS_HIGH 1

#define KEY_EVENTS_MAX 64
#define US_PER_SECOND 1000000LL

static audio_block_fn radio_block;
static int pin_level[PINS];
static long pin_changed_at[PINS];
static long pin_changes;

static const struct board *key_board;
static struct key_event key_events[KEY_EVENTS_MAX];
static int key_event_count;
static int64_t frames_handed_over;

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
	if (pin < 0 || pin >= PINS)
		return;
	if (pin_level[pin] != level)
		pin_changed_at[pin] = ++pin_changes;
	pin_level[pin] = level;
}

// a count that goes up with every change of any pin, as it was when this
// pin last changed, so that a test can tell which of two pins moved first
long hw_stand_in_pin_changed_at(int pin) {
	if (pin < 0 || pin >= PINS)
		return 0;
	return pin_changed_at[pin];
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

int audio_set_transmitter(int percent) {
	(void)percent;
	return 0;
}

int audio_set_microphone(int percent) {
	(void)percent;
	return 0;
}

static char radio_file[] = "/tmp/zbitxre_hw_settings_XXXXXX";

static void remove_radio_file(void) {
	unlink(radio_file);
}

void hw_stand_in_radio(const char *radio) {
	char *path = radio_file;
	char line[HW_SETTINGS_PATH_MAX];
	int file;
	int length;

	file = mkstemp(path);
	if (file < 0)
		return;
	length = snprintf(line, sizeof(line), "radio=%s\n", radio);
	if (write(file, line, length) == length)
		snprintf(hw_settings_file, HW_SETTINGS_PATH_MAX, "%s", path);
	close(file);
	atexit(remove_radio_file);
}

void hw_stand_in_block(struct audio_block *block) {
	if (radio_block != NULL)
		radio_block(block);
	frames_handed_over += block->frames;
}

int key_open(const struct board *board) {
	key_board = board;
	key_event_count = 0;
	return 0;
}

void key_close(void) {
	key_board = NULL;
}

int64_t key_now_us(void) {
	return frames_handed_over * US_PER_SECOND / AUDIO_RATE_HZ;
}

// the reports are kept in the order they were made, which the tests make
// the order of their times
int key_take_event(struct key_event *event) {
	int pin;

	if (key_event_count == 0 || key_events[0].us > key_now_us())
		return 0;
	*event = key_events[0];
	key_event_count--;
	memmove(key_events, key_events + 1, key_event_count * sizeof(key_events[0]));
	if (key_board != NULL) {
		pin = key_board->key_pin;
		if (event->contact == KEY_RING)
			pin = key_board->ptt_pin;
		pin_level[pin] = !event->is_closed;
	}
	return 1;
}

void hw_stand_in_key(int contact, int is_closed, int64_t us_from_now) {
	if (key_event_count == KEY_EVENTS_MAX)
		return;
	key_events[key_event_count].contact = contact;
	key_events[key_event_count].is_closed = is_closed;
	key_events[key_event_count].us = key_now_us() + us_from_now;
	key_event_count++;
}
