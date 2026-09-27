// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// sdr.h — the radio itself: tuning, modes, filters and the receiver.
// See sdr.c.

#pragma once

#include <stdint.h>

#include "audio.h"
#include "board.h"

#define SDR_BINS         2048
#define SDR_BLOCK        (SDR_BINS / 2)
#define SDR_HZ_PER_BIN   46.875

#define SDR_USB          0
#define SDR_LSB          1
#define SDR_CW           2
#define SDR_CWR          3
#define SDR_AM           5

#define SDR_AGC_OFF      (-1)
#define SDR_AGC_FAST     10
#define SDR_AGC_MEDIUM   33
#define SDR_AGC_SLOW     100

#define SDR_RESPONSE_MAX 64

struct sdr_setup {
	const struct board *board;
	const char *wisdom;
	const char *wisdom_float;
};

int sdr_open(const struct sdr_setup *setup);
int sdr_open_without_radio(const struct sdr_setup *setup);
void sdr_close(void);

int sdr_set_frequency(uint32_t hz);
int sdr_set_mode(int mode);
int sdr_set_passband(int low_hz, int high_hz);
int sdr_set_agc(int speed);
int sdr_set_pitch(int hz);
int sdr_set_volume(int percent);
int sdr_set_gain(int percent);

uint32_t sdr_oscillator_hz(void);
int sdr_s_meter(void);
int sdr_spectrum(int *plot, int first_bin, int count);

void sdr_block(struct audio_block *block);

int sdr_request(const char *request, char *response, int size);
