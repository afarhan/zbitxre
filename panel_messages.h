// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_messages.h — the messages the radio has copied, sent to the front
// panel. See panel_messages.c.

#pragma once

#include <stdint.h>

void panel_messages_poll(void);
void panel_messages_took_bytes(int control, const uint8_t *bytes, int count);
