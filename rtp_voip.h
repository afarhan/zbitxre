// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rtp_voip.h — the radio's side of VoIP: the owner's speech to the gateway,
// and another's from it to the speaker. See rtp_voip.c.

#pragma once

#include <stdint.h>

// where the gateway listens for the owner's speech, on the radio itself
#define RTP_VOIP_PORT 5007
#define RTP_VOIP_CALLSIGN_BYTES 10

int rtp_voip_open(void);
void rtp_voip_close(void);
void rtp_voip_poll(void);
void rtp_voip_set_speaking(int is_on);
int rtp_voip_is_speaking(void);
void rtp_voip_set_hearing(int is_allowed);
void rtp_voip_take_heard(const uint8_t *callsign, const uint8_t *samples);
void rtp_voip_heard_callsign(char *callsign, int room);
void rtp_voip_microphone(const int32_t *microphone, int frames);
void rtp_voip_speaker(int32_t *speaker, int frames, int is_sending);
int rtp_voip_packets_sent(void);
