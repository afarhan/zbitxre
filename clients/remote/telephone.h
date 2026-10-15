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
#define TELEPHONE_TAPS 96
#define TELEPHONE_TAPS_A_PHASE (TELEPHONE_TAPS / (WHOLE_RATE_HZ / TELEPHONE_RATE_HZ))

// what one stream keeps of the samples before, each way
struct telephone {
	// from the whole: the last TELEPHONE_TAPS samples, going round
	float past_whole[TELEPHONE_TAPS];
	int past_whole_at;
	int until_kept;
	// to the whole: the last TELEPHONE_TAPS_A_PHASE samples, going round
	float past_telephone[TELEPHONE_TAPS_A_PHASE];
	int past_telephone_at;
};

void telephone_open(void);
void telephone_begin(struct telephone *t);
void telephone_from_whole(struct telephone *t, const uint8_t *whole, uint8_t *telephone);
void telephone_to_whole(struct telephone *t, const uint8_t *telephone, uint8_t *whole);
