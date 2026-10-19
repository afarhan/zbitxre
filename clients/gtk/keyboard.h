// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// keyboard.h — the window's one on-screen keyboard. See keyboard.c.

#pragma once

#include <gtk/gtk.h>

// what a key gives besides its character
#define KEYBOARD_RUB_OUT '\b'
#define KEYBOARD_CLOSE '\n'

typedef void (*keyboard_fn)(char key);

GtkWidget *keyboard_new(const char *key_class, keyboard_fn pressed);
GtkWidget *keyboard_new_full(const char *key_class, keyboard_fn pressed);
