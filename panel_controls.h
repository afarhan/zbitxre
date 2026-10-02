// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_controls.h — the radio's controls, as the front panel is told of
// them and as it changes them. See panel_controls.c.

#pragma once

#include <stdint.h>

#include "control.h"

void panel_controls_describe(void);
void panel_controls_poll(void);
void panel_controls_changed(const struct control *c);
const char *panel_controls_took(int number, int32_t value, char *text, int room);
const char *panel_controls_took_bytes(int control, const uint8_t *bytes, int count,
	char *text, int room);
