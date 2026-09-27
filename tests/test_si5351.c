// test_si5351.c — the Si5351 on the radio, driven through the kernel's I2C bus.
//
// Stop the radio program first; this test owns the chip while it runs.
//
// It does four things, in order:
//
//   1. Finds the chips. The Si5351 must answer; the DS3231 clock chip shares
//      the bus and is reported, but the test does not depend on it.
//   2. Sets the BFO on clock 1 and the receiver's oscillator on clock 2, reads
//      the registers back from the chip, and compares them with the rows of
//      si5351_golden.h.
//   3. Waits for both PLLs to lock and reports the status register.
//   4. Retunes clock 2 a few hundred times in 10 Hz steps, as a tuning knob
//      would, and reports how long one retune takes.
//
// Both clocks are switched off at the end, which is how the driver's init
// leaves them. Exits 0 when every check passes.
//
//     test_si5351 [bus]        the bus defaults to 3

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "i2c.h"
#include "si5351.h"
#include "si5351_golden.h"

#define DEFAULT_BUS        3
#define SI5351_ADDRESS     0x60
#define DS3231_ADDRESS     0x68
#define REG_PLL_A          26
#define REG_PLL_B          34
#define REG_DIVIDER        42
#define REG_CLOCK_CONTROL  16
#define CLOCK_ON_PLL_B     1
#define BFO_ROW            0
#define OSCILLATOR_ROW     1
#define LOCK_WAIT_US       20000
#define TIMED_RETUNES      300
#define TUNING_STEP_HZ     10
#define NS_PER_US          1000
#define US_PER_SECOND      1000000

static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static int answers(uint8_t address) {
	uint8_t value;

	return i2c_read(address, 0, &value, 1) == 0;
}

static int group_on_chip_matches(int first_reg, const uint8_t *wanted) {
	uint8_t on_chip[GOLDEN_GROUP];

	if (i2c_read(SI5351_ADDRESS, first_reg, on_chip, GOLDEN_GROUP) < 0)
		return 0;
	return memcmp(on_chip, wanted, GOLDEN_GROUP) == 0;
}

static void set_and_read_back(const struct golden_retune *row, const char *name) {
	char what[80];
	uint8_t control;
	int pll_reg;
	int ok;

	pll_reg = REG_PLL_A;
	if (row->clock == CLOCK_ON_PLL_B)
		pll_reg = REG_PLL_B;

	ok = si5351_set_freq(row->clock, row->hz) == 0;
	if (ok)
		ok = group_on_chip_matches(pll_reg, row->written + GOLDEN_PLL);
	if (ok)
		ok = group_on_chip_matches(REG_DIVIDER + row->clock * GOLDEN_GROUP,
			row->written + GOLDEN_DIVIDER);
	if (ok)
		ok = i2c_read(SI5351_ADDRESS, REG_CLOCK_CONTROL + row->clock,
			&control, 1) == 0;
	if (ok)
		ok = control == row->written[GOLDEN_CONTROL];

	snprintf(what, sizeof(what), "%s on clock %d at %u Hz reads back as written",
		name, row->clock, (unsigned)row->hz);
	expect(ok, what);
}

static void check_lock(void) {
	uint8_t status;
	int ok;

	usleep(LOCK_WAIT_US);
	ok = si5351_read_status(&status) == 0;
	expect(ok, "the status register can be read");
	if (!ok)
		return;
	printf("          status register is 0x%02X\n", status);
	expect((status & SI5351_STATUS_STARTING) == 0, "the chip has finished starting");
	expect((status & SI5351_STATUS_PLL_A_NO_LOCK) == 0, "PLL A is locked");
	expect((status & SI5351_STATUS_PLL_B_NO_LOCK) == 0, "PLL B is locked");
}

static long elapsed_us(const struct timespec *from, const struct timespec *to) {
	long seconds;
	long nanoseconds;

	seconds = to->tv_sec - from->tv_sec;
	nanoseconds = to->tv_nsec - from->tv_nsec;
	return seconds * US_PER_SECOND + nanoseconds / NS_PER_US;
}

static void time_retunes(uint32_t start_hz) {
	struct timespec before;
	struct timespec after;
	long took;
	long total;
	long worst;
	int failed;
	int i;

	total = 0;
	worst = 0;
	failed = 0;
	for (i = 0; i < TIMED_RETUNES; i++) {
		clock_gettime(CLOCK_MONOTONIC, &before);
		if (si5351_set_freq(2, start_hz + i * TUNING_STEP_HZ) < 0)
			failed++;
		clock_gettime(CLOCK_MONOTONIC, &after);
		took = elapsed_us(&before, &after);
		total += took;
		if (took > worst)
			worst = took;
	}
	expect(failed == 0, "every timed retune was accepted by the chip");
	printf("          %d retunes: %ld us each on average, %ld us the slowest\n",
		TIMED_RETUNES, total / TIMED_RETUNES, worst);
}

int main(int argc, char **argv) {
	int bus;

	bus = DEFAULT_BUS;
	if (argc > 1)
		bus = atoi(argv[1]);

	if (i2c_open(bus) < 0) {
		printf("cannot open /dev/i2c-%d: %s\n", bus, strerror(errno));
		return 1;
	}
	printf("bus /dev/i2c-%d\n", bus);

	expect(answers(SI5351_ADDRESS), "the Si5351 answers at 0x60");
	if (answers(DS3231_ADDRESS))
		printf("          the DS3231 clock chip answers at 0x68\n");
	else
		printf("          no DS3231 clock chip at 0x68\n");

	expect(si5351_init() == 0, "the chip accepts a reset and all clocks off");
	set_and_read_back(&golden[BFO_ROW], "the BFO");
	set_and_read_back(&golden[OSCILLATOR_ROW], "the oscillator");
	check_lock();
	time_retunes(golden[OSCILLATOR_ROW].hz);
	check_lock();

	expect(si5351_clock_off(1) == 0, "clock 1 switches off");
	expect(si5351_clock_off(2) == 0, "clock 2 switches off");
	expect(si5351_write_failures() == 0, "no write had to be repeated");
	i2c_close();

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return 1;
	}
	printf("si5351 on the kernel bus: all checks passed\n");
	return 0;
}
