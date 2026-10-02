// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// macros.h — the function keys' macros, read from a file. See macros.c.

#pragma once

#define MACRO_MAX_LABEL (30)
#define MACRO_MAX_TEXT (200)
#define MACRO_MAX (25)

int macro_load(const char *filename);
void macro_label(int fn_key, char *label);
void macro_labels(char *labels, int room);
int macro_key(const char *label);
int macro_exec(int key, char *dest, int room);
