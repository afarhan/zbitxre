// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// wifi.h — the radio's WiFi: the access points it hears, the one it is on,
// and joining one. See wifi.c.

#pragma once

#include "link.h"

#define WIFI_ANSWER_MAX 4096

// how the WiFi is asked for something: the program, wpa_cli or nmcli, the
// words of its command, and room for what it answers. It says 0, or -1 if
// the command could not be run. The tests put their own in its place
typedef int (*wifi_ask_fn)(const char *program, const char *const *words, char *answer,
	int room);
void wifi_set_asking(wifi_ask_fn ask);

int wifi_scan(void);
int wifi_read_results(struct link_wifi_point *points, int most);
int wifi_take_results(const char *results, struct link_wifi_point *points, int most);
int wifi_status(char *answer, int room);
void wifi_ssid_in(const char *status, char *ssid, int room);
int wifi_is_on(const char *status, const char *name);
const char *wifi_join(const char *name, const char *key);
