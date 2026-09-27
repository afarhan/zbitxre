// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// tone.h — an audio tone, one sample at a time. See tone.c.

#pragma once

struct tone {
	int hz;
	int phase;
	int step;
};

void tone_open(void);
void tone_start(struct tone *tone, int hz, int phase);
int tone_next(struct tone *tone);
