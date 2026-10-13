// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rade2_codec.h — speech to RADE version 2's signal, and back. See
// rade2_codec.c.

#pragma once

#include <stdint.h>

#define RADE2_SPEECH_RATE_HZ 16000
#define RADE2_MODEM_RATE_HZ 8000
// speech goes in and comes out a frame at a time, 10 ms
#define RADE2_SPEECH_FRAME 160
#define RADE2_FEATURES 36
// the most signal one call gives, and the most the receiver asks for
#define RADE2_MODEM_MOST 1024
// the most frames of features one call of the receiver gives
#define RADE2_FRAMES_MOST 16

int rade2_codec_open(void);
void rade2_codec_close(void);

void rade2_tx_begin(void);
void rade2_tx_analyse(const int16_t speech[RADE2_SPEECH_FRAME], float features[RADE2_FEATURES]);
int rade2_tx_modulate(const float features[RADE2_FEATURES], float modem[RADE2_MODEM_MOST]);
int rade2_tx_end(float modem[RADE2_MODEM_MOST]);

void rade2_rx_begin(void);
int rade2_rx_wanted(void);
int rade2_rx_demodulate(const float *modem, float features[RADE2_FRAMES_MOST * RADE2_FEATURES],
	int *is_end);
void rade2_rx_synthesis_begin(void);
int rade2_rx_synthesise(const float features[RADE2_FEATURES], int16_t speech[RADE2_SPEECH_FRAME]);
int rade2_rx_is_synced(void);
float rade2_rx_snr_db(void);
float rade2_rx_offset_hz(void);
