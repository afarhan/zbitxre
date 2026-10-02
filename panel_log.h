// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_log.h — the logbook's newest entries, listed for the front panel.
// See panel_log.c.

#pragma once

#include <stdint.h>

void panel_log_open(const char *path);
void panel_log_list(const char *callsign);
void panel_log_took_bytes(int control, const uint8_t *bytes, int count);
void panel_log_poll(void);
