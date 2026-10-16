// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// updater.h — new firmware for the panel, over I2C. See updater.cpp.
#pragma once

#include <stdint.h>

bool updater_is_message(const uint8_t *bytes, int count);
void updater_take(const uint8_t *bytes, int count);
bool updater_is_answering(void);
int updater_answer(uint8_t *answer);
void updater_slice(void);
