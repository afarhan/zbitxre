// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// hw_stand_ins.h — see hw_stand_ins.c.

#pragma once

#include <stdint.h>

#include "audio.h"

void hw_stand_in_block(struct audio_block *block);
void hw_stand_in_key(int contact, int is_closed, int64_t us_from_now);
long hw_stand_in_pin_changed_at(int pin);
