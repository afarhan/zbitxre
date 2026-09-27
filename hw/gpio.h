// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// gpio.h — the Pi's GPIO pins, by BCM number. See gpio.c.

#pragma once

#define GPIO_PINS      28

#define GPIO_PULL_NONE 0
#define GPIO_PULL_DOWN 1
#define GPIO_PULL_UP   2

int gpio_open(void);
void gpio_close(void);
int gpio_make_output(int pin);
int gpio_make_input(int pin, int pull);
void gpio_write(int pin, int level);
int gpio_read(int pin);
