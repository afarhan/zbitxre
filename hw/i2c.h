// i2c.h — one I2C bus, reached through the kernel. See i2c.c.

#pragma once

#include <stdint.h>

#define I2C_BLOCK_MAX 32

int i2c_open(int bus);
int i2c_open_bus_of(uint8_t address);
void i2c_close(void);
int i2c_write(uint8_t address, uint8_t reg, const uint8_t *values, int count);
int i2c_read(uint8_t address, uint8_t reg, uint8_t *values, int count);
