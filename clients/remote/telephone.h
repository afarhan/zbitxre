// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// telephone.h — the radio's sound made into the telephone's, and back. See
// telephone.c.

#pragma once

#include <stdint.h>

#define TELEPHONE_RATE_HZ 8000
#define TELEPHONE_FRAME_SAMPLES 160
#define WHOLE_RATE_HZ 48000
#define WHOLE_FRAME_SAMPLES 960
#define WHOLE_FRAME_BYTES (WHOLE_FRAME_SAMPLES * 2)

void telephone_open(void);
void telephone_from_whole(const uint8_t *whole, uint8_t *telephone);
void telephone_to_whole(const uint8_t *telephone, uint8_t *whole);
