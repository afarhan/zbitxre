// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// ft8_callsign_hash.h — the callsigns heard, by their hashes. See
// ft8_callsign_hash.c.

#pragma once

#include "ft8_lib/message.h"

ftx_callsign_hash_interface_t *ft8_callsign_hashes(void);
void ft8_callsign_remember(const char *callsign);
void ft8_callsign_age(void);
