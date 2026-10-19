// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_wifi.h — the WiFi access points the radio can hear, listed for the
// front panel. See panel_wifi.c.

#pragma once

#include <stdint.h>

int panel_wifi_open(void);
void panel_wifi_poll(long now_ms);
int panel_wifi_take_results(const char *results);
void panel_wifi_took_bytes(int control, const uint8_t *bytes, int count, long now_ms);

