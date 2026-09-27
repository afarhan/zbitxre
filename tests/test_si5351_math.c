// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_si5351_math.c — the Si5351 driver's arithmetic, with no chip attached.
//
// The driver is linked against the two I2C functions below instead of hw/i2c.c.
// They record every register written, so the test can hold a retune up against
// the rows of si5351_golden.h, which came from the radio's original driver.
//
// It also checks what the driver refuses: a clock it does not have, a frequency
// outside its range, and a bus that keeps failing. A refused retune must write
// nothing; a failing bus is tried a fixed number of times and then reported.
//
// Runs anywhere, the radio or a PC. Exits 0 when every check passes.

#include <stdio.h>
#include <string.h>

#include "i2c.h"
#include "si5351.h"
#include "si5351_golden.h"

#define REG_PLL_A         26
#define REG_PLL_B         34
#define REG_DIVIDER       42
#define REG_CLOCK_CONTROL 16
#define CLOCK_ON_PLL_B    1
#define HIGHEST_REGISTER  255
#define NEVER_WRITTEN     (-1)
#define ATTEMPTS_EXPECTED 3

static int chip[HIGHEST_REGISTER + 1];
static int transfers;
static int bus_is_failing;
static int checks_failed;

int i2c_write(uint8_t address, uint8_t reg, const uint8_t *values, int count) {
	int i;

	(void)address;
	transfers++;
	if (bus_is_failing)
		return -1;
	for (i = 0; i < count; i++)
		chip[reg + i] = values[i];
	return 0;
}

int i2c_read(uint8_t address, uint8_t reg, uint8_t *values, int count) {
	(void)address;
	(void)reg;
	(void)values;
	(void)count;
	return -1;
}

static void forget(void) {
	int reg;

	for (reg = 0; reg <= HIGHEST_REGISTER; reg++)
		chip[reg] = NEVER_WRITTEN;
	transfers = 0;
}

static void expect(int ok, const char *what) {
	if (ok)
		return;
	printf("FAILED: %s\n", what);
	checks_failed++;
}

static int group_matches(int first_reg, const uint8_t *wanted) {
	int i;

	for (i = 0; i < GOLDEN_GROUP; i++) {
		if (chip[first_reg + i] != wanted[i])
			return 0;
	}
	return 1;
}

static void check_row(const struct golden_retune *row) {
	char what[80];
	int pll_reg;
	int ok;

	pll_reg = REG_PLL_A;
	if (row->clock == CLOCK_ON_PLL_B)
		pll_reg = REG_PLL_B;

	forget();
	ok = si5351_set_freq(row->clock, row->hz) == 0;
	if (ok)
		ok = group_matches(pll_reg, row->written + GOLDEN_PLL);
	if (ok)
		ok = group_matches(REG_DIVIDER + row->clock * GOLDEN_GROUP,
			row->written + GOLDEN_DIVIDER);
	if (ok)
		ok = chip[REG_CLOCK_CONTROL + row->clock]
			== row->written[GOLDEN_CONTROL];

	snprintf(what, sizeof(what), "clock %d at %u Hz matches the original",
		row->clock, (unsigned)row->hz);
	expect(ok, what);
}

static void check_refusals(void) {
	forget();
	expect(si5351_set_freq(SI5351_CLOCKS, 10000000u) < 0,
		"a clock the chip does not have is refused");
	expect(si5351_set_freq(-1, 10000000u) < 0,
		"a negative clock is refused");
	expect(si5351_set_freq(2, SI5351_MIN_HZ - 1) < 0,
		"a frequency below the range is refused");
	expect(si5351_set_freq(2, SI5351_MAX_HZ + 1) < 0,
		"a frequency above the range is refused");
	expect(si5351_clock_off(SI5351_CLOCKS) < 0,
		"switching off a clock the chip does not have is refused");
	expect(transfers == 0, "a refused request writes nothing");
}

static void check_failing_bus(void) {
	int failures_before;

	forget();
	failures_before = si5351_write_failures();
	bus_is_failing = 1;
	expect(si5351_set_freq(2, 47098000u) < 0,
		"a retune on a failing bus is reported");
	bus_is_failing = 0;
	expect(transfers == ATTEMPTS_EXPECTED,
		"a failing write is tried three times, then given up");
	expect(si5351_write_failures() - failures_before == ATTEMPTS_EXPECTED,
		"every failed attempt is counted");
}

int main(void) {
	int row;

	for (row = 0; row < GOLDEN_ROWS; row++)
		check_row(&golden[row]);
	check_refusals();
	check_failing_bus();

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return 1;
	}
	printf("si5351 arithmetic: all checks passed\n");
	return 0;
}
