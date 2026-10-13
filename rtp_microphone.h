// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rtp_microphone.h — speech to transmit, taken in on the radio itself as
// RTP, in the microphone's place. See rtp_microphone.c.

#pragma once

#include <stdint.h>

#define RTP_MICROPHONE_PORT 5006

// what comes before the RTP packet: whose sound it is, and what it is for
#define RTP_MICROPHONE_KIND_BYTES 1
#define RTP_MICROPHONE_CALLSIGN_BYTES 10
#define RTP_MICROPHONE_LABEL_BYTES (RTP_MICROPHONE_KIND_BYTES + RTP_MICROPHONE_CALLSIGN_BYTES)
// speech from the owner working the radio from afar, to be transmitted
#define RTP_MICROPHONE_TO_TRANSMIT 1
// sound from another program on the radio, to be transmitted
#define RTP_MICROPHONE_FROM_A_PROGRAM 2
// someone speaking over the internet, to be heard and not transmitted
#define RTP_MICROPHONE_VOIP 3

int rtp_microphone_open(void);
void rtp_microphone_poll(void);
void rtp_microphone_fill(int32_t *microphone, int frames, int is_sending);
void rtp_microphone_set_gain(int percent);
void rtp_microphone_close(void);
int rtp_microphone_packets(void);
