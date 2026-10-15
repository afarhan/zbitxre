// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rade_codec.h — speech to RADE's signal, versions 1 and 2, and back. See
// rade_codec.c.

#pragma once

#include <stdint.h>

#define RADE_CODEC_V1 1
#define RADE_CODEC_V2 2

#define RADE_CODEC_SPEECH_RATE_HZ 16000
#define RADE_CODEC_MODEM_RATE_HZ 8000
// speech goes in and comes out a frame at a time, 10 ms
#define RADE_CODEC_SPEECH_FRAME 160
#define RADE_CODEC_FEATURES 36
// the most signal one call gives, and the most the receiver asks for:
// version 1's end of an over is 1152 samples, its receiver asks for up to
// 1120
#define RADE_CODEC_MODEM_MOST 1280
// the most frames of features one call of the receiver gives: 12 in
// version 1, 4 in version 2
#define RADE_CODEC_FRAMES_MOST 16

struct rade_codec;

struct rade_codec *rade_codec_open(int version);
void rade_codec_close(struct rade_codec *c);

void rade_codec_tx_begin(struct rade_codec *c);
void rade_codec_tx_analyse(struct rade_codec *c, const int16_t speech[RADE_CODEC_SPEECH_FRAME],
	float features[RADE_CODEC_FEATURES]);
int rade_codec_tx_modulate(struct rade_codec *c, const float features[RADE_CODEC_FEATURES],
	float modem[RADE_CODEC_MODEM_MOST]);
int rade_codec_tx_end(struct rade_codec *c, float modem[RADE_CODEC_MODEM_MOST]);

void rade_codec_rx_begin(struct rade_codec *c);
int rade_codec_rx_wanted(struct rade_codec *c);
int rade_codec_rx_demodulate(struct rade_codec *c, const float *modem,
	float features[RADE_CODEC_FRAMES_MOST * RADE_CODEC_FEATURES], int *is_end);
void rade_codec_rx_synthesis_begin(struct rade_codec *c);
int rade_codec_rx_synthesise(struct rade_codec *c, const float features[RADE_CODEC_FEATURES],
	int16_t speech[RADE_CODEC_SPEECH_FRAME]);
int rade_codec_rx_is_synced(struct rade_codec *c);
float rade_codec_rx_snr_db(struct rade_codec *c);
float rade_codec_rx_offset_hz(struct rade_codec *c);
