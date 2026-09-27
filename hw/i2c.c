// i2c.c — one I2C bus, reached through the kernel's /dev/i2c-N.
//
// The radio used to toggle the two bus pins itself, from whichever thread
// wanted the bus, with nothing to stop two threads doing so at once. Here the
// kernel owns the pins and lets one transfer finish before the next begins, so
// callers on different threads need no lock of their own.
//
// The kernel still makes each bit by waiting in a loop, as the old code did. A
// transfer holds a processor for its whole length, roughly 0.6 ms for eight
// registers, so it belongs on a thread that can afford that and never on the
// one that carries the audio.
//
// Every transfer is a single I2C_RDWR request that carries the device address
// inside it. The open descriptor therefore holds no "current device", and two
// threads talking to two chips cannot disturb each other.
//
// A read is the register number written, a repeated start, and the bytes read
// back, all inside one transfer, so nothing else on the bus can come between
// naming the register and reading it.
//
// On the zbitx the bus is a kernel bit-banged one on the pins the Si5351 and
// the clock chip were always wired to. This line in /boot/config.txt makes it
// /dev/i2c-3:
//
//     dtoverlay=i2c-gpio,bus=3,i2c_gpio_sda=13,i2c_gpio_scl=6
//
// Every function returns 0, or -1 with errno set.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "i2c.h"

#define DEVICE_PATH_MAX 32
#define NOT_OPEN (-1)
#define REGISTER_BYTES 1

static int bus_fd = NOT_OPEN;
static int bus_number = NOT_OPEN;

int i2c_open(int bus) {
	char path[DEVICE_PATH_MAX];

	if (bus_fd != NOT_OPEN) {
		if (bus == bus_number)
			return 0;
		errno = EBUSY;
		return -1;
	}
	snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
	bus_fd = open(path, O_RDWR);
	if (bus_fd < 0) {
		bus_fd = NOT_OPEN;
		return -1;
	}
	bus_number = bus;
	return 0;
}

void i2c_close(void) {
	if (bus_fd == NOT_OPEN)
		return;
	close(bus_fd);
	bus_fd = NOT_OPEN;
	bus_number = NOT_OPEN;
}

static int transfer(struct i2c_msg *messages, int count) {
	struct i2c_rdwr_ioctl_data request;

	request.msgs = messages;
	request.nmsgs = count;
	if (ioctl(bus_fd, I2C_RDWR, &request) < 0)
		return -1;
	return 0;
}

static int arguments_ok(const uint8_t *values, int count) {
	if (bus_fd == NOT_OPEN)
		return 0;
	if (count < 0 || count > I2C_BLOCK_MAX)
		return 0;
	if (count > 0 && values == NULL)
		return 0;
	return 1;
}

int i2c_write(uint8_t address, uint8_t reg, const uint8_t *values, int count) {
	uint8_t frame[REGISTER_BYTES + I2C_BLOCK_MAX];
	struct i2c_msg message;

	if (!arguments_ok(values, count)) {
		errno = EINVAL;
		return -1;
	}
	frame[0] = reg;
	if (count > 0)
		memcpy(frame + REGISTER_BYTES, values, count);

	message.addr = address;
	message.flags = 0;
	message.len = REGISTER_BYTES + count;
	message.buf = frame;
	return transfer(&message, 1);
}

int i2c_read(uint8_t address, uint8_t reg, uint8_t *values, int count) {
	struct i2c_msg messages[2];

	if (!arguments_ok(values, count) || count == 0) {
		errno = EINVAL;
		return -1;
	}
	messages[0].addr = address;
	messages[0].flags = 0;
	messages[0].len = REGISTER_BYTES;
	messages[0].buf = &reg;

	messages[1].addr = address;
	messages[1].flags = I2C_M_RD;
	messages[1].len = count;
	messages[1].buf = values;
	return transfer(messages, 2);
}
