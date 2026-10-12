// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rtp_microphone.h — speech to transmit, taken in on the radio itself as
// RTP, in the microphone's place. See rtp_microphone.c.

#pragma once

#include <stdint.h>

#define RTP_MICROPHONE_PORT 5006

int rtp_microphone_open(void);
void rtp_microphone_poll(void);
void rtp_microphone_fill(int32_t *microphone, int frames, int is_sending);
void rtp_microphone_set_gain(int percent);
void rtp_microphone_close(void);
int rtp_microphone_packets(void);
