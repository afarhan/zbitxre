// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// meter.h — what the radio measures of itself. See meter.c.

#pragma once

int meter_open(void);
void meter_poll(long now_ms);
