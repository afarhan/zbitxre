// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// touch.h — the touch screen. See touch.cpp.

#pragma once

#include <stdint.h>

#define TOUCH_CALIBRATION_VALUES 5

void touch_init(void);
bool touch_read(uint16_t *x, uint16_t *y);
void touch_set_calibration(const uint16_t *values);
void touch_calibrate(uint16_t *values);
