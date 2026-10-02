// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// settings.h — the radio's settings, kept from one start to the next. See
// settings.c.

#pragma once

#include <stdio.h>

#include "control.h"

// what is kept besides the controls: one function writes its lines, NAME=value,
// and the other is given each line that is no control's and says whether
// it took it
typedef void (*settings_save_fn)(FILE *file);
typedef int (*settings_load_fn)(const char *name, const char *value);
void settings_set_extras(settings_save_fn save, settings_load_fn load);

void settings_load(const char *path);
void settings_changed(const struct control *c, long now_ms);
void settings_poll(long now_ms);
void settings_save(void);
