// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// i2c.h — one I2C bus, reached through the kernel. See i2c.c.

#pragma once

#include <stdint.h>

#define I2C_BLOCK_MAX 32

int i2c_open(int bus);
int i2c_open_bus_of(uint8_t address);
void i2c_close(void);
int i2c_write(uint8_t address, uint8_t reg, const uint8_t *values, int count);
int i2c_read(uint8_t address, uint8_t reg, uint8_t *values, int count);

#define I2C_SEND_MAX 255
#define I2C_COUNTED_MAX 32
#define I2C_COUNTED_ROOM (1 + I2C_COUNTED_MAX)

int i2c_send(uint8_t address, const uint8_t *bytes, int count);
int i2c_receive(uint8_t address, uint8_t *bytes, int count);
int i2c_receive_counted(uint8_t address, uint8_t *bytes, int room);
