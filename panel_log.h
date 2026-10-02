// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_log.h — the logbook's newest contacts, listed for the front panel.
// See panel_log.c.

#pragma once

int panel_log_open(const char *path);
void panel_log_poll(void);
