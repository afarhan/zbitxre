// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// md5.h — the MD5 of a block of bytes, in hex. See md5.c.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define MD5_HEX_ROOM 33

void md5_hex(const uint8_t *bytes, size_t count, char hex[MD5_HEX_ROOM]);
