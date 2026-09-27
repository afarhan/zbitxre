// gpio.c — the Pi's GPIO pins, reached through their registers.
//
// Thanks to wiringPi (Gordon Henderson), whose way of reaching the pins this
// is: /dev/gpiomem mapped into the program, and the registers of the BCM2835
// family read and written in place. Only the calls the radio needs are here,
// and no library is linked in.
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
// A pull is set the way the BCM2835 asks: name the pull, wait, clock it into
// the pin, wait, then clear both registers. That holds for every Pi up to the
// 3 and for the Zero 2W. The Pi 4 and 5 set pulls differently and are not
// handled here.
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

#define PULL_SETTLE_US    5

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

static void set_pull(int pin, int pull) {
	registers[REG_PULL] = (uint32_t)pull;
	usleep(PULL_SETTLE_US);
	registers[REG_PULL_CLOCK] = 1u << pin;
	usleep(PULL_SETTLE_US);
	registers[REG_PULL] = 0;
	registers[REG_PULL_CLOCK] = 0;
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
	set_function(pin, FUNCTION_INPUT);
	set_pull(pin, pull);
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
