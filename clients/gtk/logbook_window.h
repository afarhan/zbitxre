// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// logbook_window.h — the logbook's window. See logbook_window.c.

#pragma once

#include <gtk/gtk.h>

void logbook_window_open(GtkWindow *main_window,
	void (*send_line)(const char *word, const char *rest),
	const char *(*value_of)(const char *control), const char *macros,
	const char *macro_word, const char *stop_word);
void logbook_window_set_macros(const char *choices, const char *macro_word,
	const char *stop_word);
int logbook_window_is_open(void);
void logbook_window_took(const char *name, const char *value);
void logbook_window_turned(int notches);
void logbook_window_pressed(void);
