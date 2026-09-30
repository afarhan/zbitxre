// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// radio.h — the radio as its operator sees it. See radio.c.

#pragma once

#define RADIO_LABEL_MAX 16
#define RADIO_VALUE_MAX 32

typedef void (*radio_changed_fn)(const char *label, const char *value);

void radio_open(radio_changed_fn changed);
int radio_set(const char *label, const char *value, char *why, int room);
int radio_get(const char *label, char *value, int room);
void radio_tell_all(radio_changed_fn tell);
