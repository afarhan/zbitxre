// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rtp_audio.h — what the radio plays, sent out on the radio itself as RTP,
// for other programs to hear. See rtp_audio.c.

#pragma once

#include <stdint.h>

#define RTP_AUDIO_GROUP "239.255.73.88"
#define RTP_AUDIO_PORT 5004
#define RTP_AUDIO_RATE_HZ 48000
#define RTP_AUDIO_FRAME_SAMPLES 960
#define RTP_AUDIO_SAMPLE_BYTES 2
#define RTP_AUDIO_PAYLOAD_TYPE 96
#define RTP_HEADER_BYTES 12
#define RTP_PACKET_BYTES (RTP_HEADER_BYTES + RTP_AUDIO_FRAME_SAMPLES * RTP_AUDIO_SAMPLE_BYTES)

void rtp_audio_take(const int32_t *speaker, int frames);
int rtp_audio_open(void);
void rtp_audio_poll(void);
void rtp_audio_close(void);
int rtp_audio_peak_percent(void);
