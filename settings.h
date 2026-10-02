// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// settings.h — the radio's settings, kept from one start to the next. See
// settings.c.

#pragma once

#include "control.h"

void settings_load(const char *path);
void settings_changed(const struct control *c, long now_ms);
void settings_poll(long now_ms);
void settings_save(void);
