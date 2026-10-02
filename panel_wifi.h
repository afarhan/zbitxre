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

// how the WiFi is asked for something: the words of a wpa_cli command, and
// room for what it answers. It says 0, or -1 if the command could not be
// run. The tests put their own in its place
typedef int (*panel_wifi_ask_fn)(const char *const *words, char *answer, int room);
void panel_wifi_set_asking(panel_wifi_ask_fn ask);
