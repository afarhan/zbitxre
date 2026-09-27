// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// filter.h — the shape of a filter, as one number for each FFT bin.
// See filter.c.

#pragma once

#include <complex.h>

#define FILTER_BLOCK   1024
#define FILTER_IMPULSE 1025
#define FILTER_BINS    (FILTER_BLOCK + FILTER_IMPULSE - 1)
#define FILTER_BETA    5.0f

int filter_open(void);
int filter_shape(float complex *shape, float low, float high, float beta);
void filter_hann(float *window, int count);
