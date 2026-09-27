// si5351.c — the Si5351 clock generator: three outputs from two PLLs.
//
// An output is a PLL divided by an even integer, and the PLL is the crystal
// multiplied by a + b/c. Putting the fraction in the PLL and keeping the
// output divider an even integer is the low-jitter arrangement of Silicon Labs
// AN619. The divider is one more than 650 MHz / f, rounded up to even, which
// holds the PLL between 650 and 900 MHz for any output up to 125 MHz.
//
// The chip stores a + b/c as three numbers, with c fixed here at 2^19:
//
//     P1 = 128 a + floor(128 b / c) - 512
//     P2 = 128 b - c floor(128 b / c)
//     P3 = c
//
// and a whole number (b = 0) as P1 = 128 a - 512, P2 = 0, P3 = 1. Eight
// registers hold the three, packed the same way for a PLL and for a divider.
//
// Clock 1 has PLL B to itself. Clocks 0 and 2 both run from PLL A, so tuning
// one of them moves the other. The radio puts the receiver's oscillator on
// clock 2 and the BFO on clock 1, and leaves clock 0 off.
//
// A retune writes the PLL's eight registers, the divider's eight, and the
// clock's control register, each group as one I2C transfer. The PLL is not
// reset, so the output keeps running while it moves.
//
// The arithmetic gives the same registers as zbitxv2's si5351v2.c;
// tests/test_si5351_math.c holds values captured from it.

#include <unistd.h>

#include "i2c.h"
#include "si5351.h"

#define REG_STATUS           0
#define REG_CLOCK_CONTROL    16
#define REG_PLL_A            26
#define REG_PLL_B            34
#define REG_DIVIDER          42
#define REG_PLL_RESET        177
#define REGISTERS_PER_GROUP  8

#define RESET_BOTH_PLLS      0xA0
#define CLOCK_POWERED_DOWN   0x80
#define CLOCK_WHOLE_DIVIDER  0x40
#define CLOCK_FROM_PLL_B     0x20
#define CLOCK_FROM_DIVIDER   0x0C
#define CLOCK_DRIVE_8MA      0x03

#define CLOCK_ON_PLL_B       1
#define PLL_TARGET_HZ        650000000u
#define FRACTION_DENOMINATOR 0x80000u
#define P_SCALE              128u
#define P_OFFSET             512u
#define DEFAULT_CRYSTAL_HZ   25000000u

#define SETTLE_US            10000
#define WRITE_ATTEMPTS       3
#define RETRY_WAIT_US        1000

static uint32_t crystal_hz = DEFAULT_CRYSTAL_HZ;
static int write_failures;

static int write_registers(uint8_t reg, const uint8_t *values, int count) {
	int attempt;

	for (attempt = 0; attempt < WRITE_ATTEMPTS; attempt++) {
		if (i2c_write(SI5351_ADDRESS, reg, values, count) == 0)
			return 0;
		write_failures++;
		usleep(RETRY_WAIT_US);
	}
	return -1;
}

static int write_register(uint8_t reg, uint8_t value) {
	return write_registers(reg, &value, 1);
}

static void pack(uint8_t *group, uint32_t whole, uint32_t numerator) {
	uint32_t p1;
	uint32_t p2;
	uint32_t p3;
	uint32_t scaled_fraction;

	if (numerator == 0) {
		p1 = P_SCALE * whole - P_OFFSET;
		p2 = 0;
		p3 = 1;
	} else {
		scaled_fraction = (P_SCALE * numerator) / FRACTION_DENOMINATOR;
		p1 = P_SCALE * whole + scaled_fraction - P_OFFSET;
		p2 = P_SCALE * numerator - FRACTION_DENOMINATOR * scaled_fraction;
		p3 = FRACTION_DENOMINATOR;
	}
	group[0] = (p3 >> 8) & 0xFF;
	group[1] = p3 & 0xFF;
	group[2] = (p1 >> 16) & 0x03;
	group[3] = (p1 >> 8) & 0xFF;
	group[4] = p1 & 0xFF;
	group[5] = ((p3 >> 12) & 0xF0) | ((p2 >> 16) & 0x0F);
	group[6] = (p2 >> 8) & 0xFF;
	group[7] = p2 & 0xFF;
}

void si5351_set_crystal(uint32_t hz) {
	crystal_hz = hz;
}

int si5351_write_failures(void) {
	return write_failures;
}

int si5351_clock_off(int clock) {
	if (clock < 0 || clock >= SI5351_CLOCKS)
		return -1;
	return write_register(REG_CLOCK_CONTROL + clock, CLOCK_POWERED_DOWN);
}

int si5351_init(void) {
	int clock;

	usleep(SETTLE_US);
	if (write_register(REG_PLL_RESET, RESET_BOTH_PLLS) < 0)
		return -1;
	usleep(SETTLE_US);
	for (clock = 0; clock < SI5351_CLOCKS; clock++) {
		if (si5351_clock_off(clock) < 0)
			return -1;
	}
	return 0;
}

int si5351_set_freq(int clock, uint32_t hz) {
	uint8_t group[REGISTERS_PER_GROUP];
	uint8_t pll_reg;
	uint8_t control;
	uint32_t divider;
	uint32_t pll_hz;
	uint32_t whole;
	uint32_t numerator;
	uint64_t remainder_scaled;

	if (clock < 0 || clock >= SI5351_CLOCKS)
		return -1;
	if (hz < SI5351_MIN_HZ || hz > SI5351_MAX_HZ)
		return -1;

	pll_reg = REG_PLL_A;
	control = CLOCK_DRIVE_8MA | CLOCK_FROM_DIVIDER | CLOCK_WHOLE_DIVIDER;
	if (clock == CLOCK_ON_PLL_B) {
		pll_reg = REG_PLL_B;
		control |= CLOCK_FROM_PLL_B;
	}

	divider = PLL_TARGET_HZ / hz + 1;
	if (divider % 2 != 0)
		divider++;
	pll_hz = hz * divider;
	whole = pll_hz / crystal_hz;
	remainder_scaled = (uint64_t)(pll_hz % crystal_hz) * FRACTION_DENOMINATOR;
	numerator = (uint32_t)(remainder_scaled / crystal_hz);

	pack(group, whole, numerator);
	if (write_registers(pll_reg, group, REGISTERS_PER_GROUP) < 0)
		return -1;

	pack(group, divider, 0);
	if (write_registers(REG_DIVIDER + clock * REGISTERS_PER_GROUP, group,
			REGISTERS_PER_GROUP) < 0)
		return -1;

	return write_register(REG_CLOCK_CONTROL + clock, control);
}

int si5351_read_status(uint8_t *status) {
	return i2c_read(SI5351_ADDRESS, REG_STATUS, status, 1);
}
