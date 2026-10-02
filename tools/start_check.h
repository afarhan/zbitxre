// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// start_check.h — whether the spectrum has the humps as the radio starts.
// See start_check.c.

#pragma once

#include <stdint.h>

// above this the code's own guess is that the humps are there; it is a
// first guess from one day's snapshots and is there to be corrected
#define START_CHECK_SUSPECT_DB 3.0

double start_check_ripple_db(const int32_t *samples, int frames, int channels,
	double *period_hz);
