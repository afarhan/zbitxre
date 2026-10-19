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
//
// THE FULL KEYBOARD
//
// A WiFi key may have small letters and any sign, and the local GTK client
// on an sbitx's own screen sets the radio's WiFi, the author's asking of
// 18 October 2026, so keyboard_new_full() has three pages of the same
// four rows: small letters, capitals, and signs. SHIFT goes between the
// small letters and the capitals, #+= to the signs and abc back. It is for
// the WiFi key alone; everything else is typed on the one keyboard.

#include <string.h>

#include "keyboard.h"

#define ROWS 4
#define SPACE_COLUMNS 2
#define PAGES 3

// the keys, a row to a line: a character is its key, and these, which no
// key types, stand for the keys that are not characters. A string is
// broken after each, since C would read a letter after \x02 as more of it
#define RUB_OUT_KEY '\x01'
#define SPACE_KEY '\x02'
#define CLOSE_KEY '\x03'
#define SHIFT_KEY '\x04'
#define SIGNS_KEY '\x05'
#define LETTERS_KEY '\x06'
static const char *const key_rows[ROWS] = {
	"1234567890\x01", "QWERTYUIOP/", "ASDFGHJKL?-", "ZXCV\x02" "BNM.\x03",
};

// the full keyboard's pages: small letters, capitals and signs
static const char *const full_rows[PAGES][ROWS] = {
	{ "1234567890\x01", "qwertyuiop\x05", "asdfghjkl.\x04", "zxcv\x02" "bnm-\x03" },
	{ "1234567890\x01", "QWERTYUIOP\x05", "ASDFGHJKL.\x04", "ZXCV\x02" "BNM-\x03" },
	{ "!@#$%^&*()\x01", "_+=[]{};:'\x06", "\"\\|`~<>,/?", "zxcv\x02" "bnm-\x03" },
};
static const char *const page_names[PAGES] = { "small", "capitals", "signs" };

static const char *key_label(char c, char *text) {
	if (c == RUB_OUT_KEY)
		return "⌫";
	if (c == SPACE_KEY)
		return "SPACE";
	if (c == CLOSE_KEY)
		return "CLOSE";
	if (c == SHIFT_KEY)
		return "SHIFT";
	if (c == SIGNS_KEY)
		return "#+=";
	if (c == LETTERS_KEY)
		return "abc";
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

// the full keyboard's page is changed by its keys, the stack it is in
// being the grid's parent
static void show_page_of(GtkWidget *key, const char *page) {
	GtkWidget *grid = gtk_widget_get_parent(key);

	gtk_stack_set_visible_child_name(GTK_STACK(gtk_widget_get_parent(grid)), page);
}

static void on_key_clicked(GtkButton *button, gpointer key) {
	keyboard_fn pressed = (keyboard_fn)g_object_get_data(G_OBJECT(button), "pressed");
	const char *page = g_object_get_data(G_OBJECT(button), "page");
	char c = (char)GPOINTER_TO_INT(key);

	if (c == SHIFT_KEY && !strcmp(page, page_names[0]))
		show_page_of(GTK_WIDGET(button), page_names[1]);
	else if (c == SHIFT_KEY)
		show_page_of(GTK_WIDGET(button), page_names[0]);
	else if (c == SIGNS_KEY)
		show_page_of(GTK_WIDGET(button), page_names[2]);
	else if (c == LETTERS_KEY)
		show_page_of(GTK_WIDGET(button), page_names[0]);
	else
		pressed(key_given(c));
}

// a grid of the rows given, each key a button of the class named
static GtkWidget *keys_of(const char *const *rows, const char *key_class, keyboard_fn pressed,
		const char *page) {
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
		for (at = rows[row]; *at != 0; at++) {
			width = 1;
			if (*at == SPACE_KEY)
				width = SPACE_COLUMNS;
			key = gtk_button_new_with_label(key_label(*at, text));
			gtk_style_context_add_class(gtk_widget_get_style_context(key), key_class);
			gtk_widget_set_can_focus(key, FALSE);
			gtk_widget_set_hexpand(key, TRUE);
			gtk_widget_set_vexpand(key, TRUE);
			g_object_set_data(G_OBJECT(key), "pressed", (gpointer)pressed);
			g_object_set_data(G_OBJECT(key), "page", (gpointer)page);
			g_signal_connect(key, "clicked", G_CALLBACK(on_key_clicked),
				GINT_TO_POINTER(*at));
			gtk_grid_attach(GTK_GRID(keys), key, column, row, width, 1);
			column += width;
		}
	}
	return keys;
}

GtkWidget *keyboard_new(const char *key_class, keyboard_fn pressed) {
	return keys_of(key_rows, key_class, pressed, page_names[0]);
}

GtkWidget *keyboard_new_full(const char *key_class, keyboard_fn pressed) {
	GtkWidget *pages = gtk_stack_new();
	int i;

	for (i = 0; i < PAGES; i++)
		gtk_stack_add_named(GTK_STACK(pages), keys_of(full_rows[i], key_class, pressed,
			page_names[i]), page_names[i]);
	return pages;
}
