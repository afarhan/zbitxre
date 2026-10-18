// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// keyboard.c — the window's one on-screen keyboard.
//
// GTK has no keyboard on the screen that comes up by itself when something
// is typed, and the radio has no other keyboard to count on, so the window
// has its own. There were three, one for the logbook's fields, one for
// sending CW and one for the texts of the settings, until the author asked
// on 18 October 2026 for them to be one; the panel has the same one,
// front_panel/screens.cpp.
//
// It has no more keys than a callsign, a report, an exchange or a line of
// CW needs, in four rows: the digits with the rub-out after the 0, the
// letters in three rows with the space between V and B, the four marks,
// and CLOSE, which ends the typing and puts the keyboard away. What a key
// does is the caller's: each touch gives the caller the key's character, a
// space for SPACE, KEYBOARD_RUB_OUT or KEYBOARD_CLOSE. The logbook types
// into a field, the settings into a text, and KBD sends each key on the
// air as it is touched.
//
// The keys take whatever room they are given, all of one size, but SPACE,
// which is two keys wide. Their look is the caller's, by the class named.

#include "keyboard.h"

#define ROWS 4
#define SPACE_COLUMNS 2

// the keys, a row to a line: a character is its key, and these stand for
// the keys that are not characters
#define RUB_OUT_KEY '<'
#define SPACE_KEY '_'
#define CLOSE_KEY '>'
static const char *const key_rows[ROWS] = {
	"1234567890<", "QWERTYUIOP/", "ASDFGHJKL?-", "ZXCV_BNM.>",
};

static const char *key_label(char c, char *text) {
	if (c == RUB_OUT_KEY)
		return "⌫";
	if (c == SPACE_KEY)
		return "SPACE";
	if (c == CLOSE_KEY)
		return "CLOSE";
	text[0] = c;
	text[1] = 0;
	return text;
}

static char key_given(char c) {
	if (c == RUB_OUT_KEY)
		return KEYBOARD_RUB_OUT;
	if (c == SPACE_KEY)
		return ' ';
	if (c == CLOSE_KEY)
		return KEYBOARD_CLOSE;
	return c;
}

static void on_key_clicked(GtkButton *button, gpointer key) {
	keyboard_fn pressed = (keyboard_fn)g_object_get_data(G_OBJECT(button), "pressed");

	pressed(key_given((char)GPOINTER_TO_INT(key)));
}

GtkWidget *keyboard_new(const char *key_class, keyboard_fn pressed) {
	GtkWidget *keys = gtk_grid_new();
	GtkWidget *key;
	char text[2];
	const char *at;
	int column;
	int width;
	int row;

	gtk_grid_set_row_homogeneous(GTK_GRID(keys), TRUE);
	gtk_grid_set_column_homogeneous(GTK_GRID(keys), TRUE);
	for (row = 0; row < ROWS; row++) {
		column = 0;
		for (at = key_rows[row]; *at != 0; at++) {
			width = 1;
			if (*at == SPACE_KEY)
				width = SPACE_COLUMNS;
			key = gtk_button_new_with_label(key_label(*at, text));
			gtk_style_context_add_class(gtk_widget_get_style_context(key), key_class);
			gtk_widget_set_can_focus(key, FALSE);
			gtk_widget_set_hexpand(key, TRUE);
			gtk_widget_set_vexpand(key, TRUE);
			g_object_set_data(G_OBJECT(key), "pressed", (gpointer)pressed);
			g_signal_connect(key, "clicked", G_CALLBACK(on_key_clicked),
				GINT_TO_POINTER(*at));
			gtk_grid_attach(GTK_GRID(keys), key, column, row, width, 1);
			column += width;
		}
	}
	return keys;
}
