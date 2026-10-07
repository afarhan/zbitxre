// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// gpio.c — the Pi's GPIO pins, reached through their registers.
//
// Thanks to hermes-radio-daemon (Rafael Diniz, Rhizomatica), which reaches the
// pins through Raspberry Pi's gpiolib. The registers and the order they are
// written in follow gpiolib's BCM2835 part, Copyright (C) Raspberry Pi Ltd,
// BSD-3-Clause. wiringPi (Gordon Henderson) reaches the pins the same way:
// /dev/gpiomem mapped into the program, and the registers read and written in
// place. Only the calls the radio needs are here, and no library is linked in.
//
// A pin is known by its BCM number, the one printed beside "GPIO" on a pinout
// chart. It is not the pin's place on the header and not a wiringPi number.
// Only the 28 pins that reach the header are accepted.
//
// Reading or writing a pin is a single register access with no call into the
// kernel, so both are fit for the audio thread. A write names only the pin it
// changes, so two threads writing different pins cannot undo each other.
// Turning a pin into an input or an output rewrites a register that ten pins
// share, and setting a pull takes two waits, so both are for start-up only.
//
// A pull is set the way the Pi's processor asks, and there are two ways.
// The BCM2835's, which holds for every Pi up to the 3 and for the Zero 2W
// in the zbitx: name the pull, wait, clock it into the pin, wait, then clear
// each register with a wait after it. The BCM2711's, in the Pi 4 of the
// sbitx: each pin has two bits of its own in four registers further on,
// written once, and there the numbers for up and down are the other way
// round. Which processor it is, is told as gpiolib tells it: where the
// BCM2711 has the last of those four registers, the older ones read back
// the letters "gpio". The Pi 5 is different again and is not handled.
//
// Every function that can fail returns 0, or -1 with errno set.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/mman.h>

#include "gpio.h"

#define REGISTERS_BYTES   4096

#define REG_FUNCTION      0
#define REG_SET           7
#define REG_CLEAR         10
#define REG_LEVEL         13
#define REG_PULL          37
#define REG_PULL_CLOCK    38

#define PINS_PER_FUNCTION 10
#define BITS_PER_FUNCTION 3
#define FUNCTION_MASK     7u
#define FUNCTION_INPUT    0u
#define FUNCTION_OUTPUT   1u

#define PULL_SETTLE_US    10

#define REG_PULL_2711       57
#define REG_PULL_2711_LAST  60
#define NO_SUCH_REGISTER    0x6770696fu
#define PINS_PER_PULL_2711  16
#define BITS_PER_PULL_2711  2
#define PULL_MASK_2711      3u
#define PULL_UP_2711        1u
#define PULL_DOWN_2711      2u

static volatile uint32_t *registers;

static int pin_ok(int pin) {
	if (registers == NULL)
		return 0;
	if (pin < 0 || pin >= GPIO_PINS)
		return 0;
	return 1;
}

int gpio_open(void) {
	void *mapped;
	int fd;

	if (registers != NULL)
		return 0;
	fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
	if (fd < 0)
		return -1;
	mapped = mmap(NULL, REGISTERS_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED,
		fd, 0);
	close(fd);
	if (mapped == MAP_FAILED)
		return -1;
	registers = mapped;
	return 0;
}

void gpio_close(void) {
	if (registers == NULL)
		return;
	munmap((void *)registers, REGISTERS_BYTES);
	registers = NULL;
}

static void set_function(int pin, uint32_t function) {
	uint32_t value;
	int reg;
	int shift;

	reg = REG_FUNCTION + pin / PINS_PER_FUNCTION;
	shift = (pin % PINS_PER_FUNCTION) * BITS_PER_FUNCTION;
	value = registers[reg];
	value &= ~(FUNCTION_MASK << shift);
	value |= function << shift;
	registers[reg] = value;
}

static void set_pull_2711(int pin, int pull) {
	uint32_t value;
	uint32_t bits = 0;
	int reg = REG_PULL_2711 + pin / PINS_PER_PULL_2711;
	int shift = (pin % PINS_PER_PULL_2711) * BITS_PER_PULL_2711;

	if (pull == GPIO_PULL_UP)
		bits = PULL_UP_2711;
	if (pull == GPIO_PULL_DOWN)
		bits = PULL_DOWN_2711;
	value = registers[reg];
	value &= ~(PULL_MASK_2711 << shift);
	value |= bits << shift;
	registers[reg] = value;
}

static void set_pull(int pin, int pull) {
	if (registers[REG_PULL_2711_LAST] != NO_SUCH_REGISTER) {
		set_pull_2711(pin, pull);
		return;
	}
	registers[REG_PULL] = (uint32_t)pull;
	usleep(PULL_SETTLE_US);
	registers[REG_PULL_CLOCK] = 1u << pin;
	usleep(PULL_SETTLE_US);
	registers[REG_PULL] = 0;
	usleep(PULL_SETTLE_US);
	registers[REG_PULL_CLOCK] = 0;
	usleep(PULL_SETTLE_US);
}

int gpio_make_output(int pin) {
	if (!pin_ok(pin)) {
		errno = EINVAL;
		return -1;
	}
	set_function(pin, FUNCTION_OUTPUT);
	return 0;
}

int gpio_make_input(int pin, int pull) {
	if (!pin_ok(pin) || pull < GPIO_PULL_NONE || pull > GPIO_PULL_UP) {
		errno = EINVAL;
		return -1;
	}
	set_pull(pin, pull);
	set_function(pin, FUNCTION_INPUT);
	return 0;
}

void gpio_write(int pin, int level) {
	if (!pin_ok(pin))
		return;
	if (level)
		registers[REG_SET] = 1u << pin;
	else
		registers[REG_CLEAR] = 1u << pin;
}

int gpio_read(int pin) {
	if (!pin_ok(pin))
		return 0;
	if (registers[REG_LEVEL] & (1u << pin))
		return 1;
	return 0;
}
