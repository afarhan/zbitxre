// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// stand_ins.c — everything zbitxv2's sbitx.c reaches for outside itself,
// so that it can be run alone on any computer.
//
// Nothing here touches a radio. The pins, the sound card and the modems do
// nothing. The Si5351 only remembers the last frequency it was asked for, so
// that script.c can write it down.

#include <stdint.h>

unsigned int oscillator_asked_hz;
unsigned int bfo_asked_hz;

#define BFO_CLOCK 1
#define OSCILLATOR_CLOCK 2
#define NOTHING_ANSWERED (-1)

void pinMode(int pin, int mode) {
	(void)pin;
	(void)mode;
}

void digitalWrite(int pin, int value) {
	(void)pin;
	(void)value;
}

int digitalRead(int pin) {
	(void)pin;
	return 1;
}

void delay(unsigned int ms) {
	(void)ms;
}

unsigned int millis(void) {
	return 0;
}

void si5351bx_init(void) {
}

void si5351_reset(void) {
}

void si5351a_clkoff(uint8_t clk) {
	(void)clk;
}

void si5351_set_calibration(int32_t cal) {
	(void)cal;
}

void si5351bx_setfreq(uint8_t clk, uint32_t hz) {
	if (clk == OSCILLATOR_CLOCK)
		oscillator_asked_hz = hz;
	if (clk == BFO_CLOCK)
		bfo_asked_hz = hz;
}

int32_t i2cbb_read_i2c_block_data(uint8_t address, uint8_t command,
		uint8_t length, uint8_t *values) {
	(void)address;
	(void)command;
	(void)length;
	(void)values;
	return NOTHING_ANSWERED;
}

void sound_mixer(char *card, char *element, int value) {
	(void)card;
	(void)element;
	(void)value;
}

int sound_thread_start(char *device) {
	(void)device;
	return 0;
}

void sound_input(int loop) {
	(void)loop;
}

void modem_rx(int mode, int32_t *samples, int count) {
	(void)mode;
	(void)samples;
	(void)count;
}

float modem_next_sample(int mode) {
	(void)mode;
	return 0;
}

void modem_init(void) {
}

void sdr_modulation_update(int32_t *samples, int count, double scale) {
	(void)samples;
	(void)count;
	(void)scale;
}
