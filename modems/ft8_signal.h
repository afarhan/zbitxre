// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// ft8_signal.h — the sound of an FT8 message, to be sent. See ft8_signal.c.

#pragma once

#include <stdint.h>

// 79 symbols of 0.16 second
#define FT8_SIGNAL_SECONDS 12.64

int ft8_signal_samples(int sample_rate);
int sbitx_ft8_encode(const char *message, int pitch_hz, int sample_rate,
	float *signal, int room);
void synth_gfsk(const uint8_t *symbols, int n_sym, float f0, float symbol_bt,
	float symbol_period, int signal_rate, float *signal);
