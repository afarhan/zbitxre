// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// ft8_monitor.h — the FT8 messages in one slot of sound. See ft8_monitor.c.

#pragma once

#define FT8_SAMPLE_RATE 12000
// the longest text of a message, with the zero at its end: ft8_lib's size
#define FT8_TEXT_ROOM 25

struct ft8_decoded {
	int score;
	int snr;
	int freq_hz;
	float time_sec;
	char text[FT8_TEXT_ROOM];
};

int sbitx_ft8_decode(const float *signal, int num_samples,
	struct ft8_decoded *found, int room);
