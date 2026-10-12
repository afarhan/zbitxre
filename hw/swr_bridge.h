// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// swr_bridge.h — the sbitx's SWR bridge, read through its ATtiny85. See
// swr_bridge.c.

#pragma once

int swr_bridge_read(int *forward, int *reflected);
