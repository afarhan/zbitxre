// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// settings.c — the radio's settings, kept from one start to the next.
//
// The callsign has to be there when the radio is next switched on, and so
// should the keyer's speed and the rest of what an operator sets once. So
// the controls' values are kept in a file, as zbitxv2 kept its fields in
// user_settings.ini: a line for each, NAME=value.
//
// settings_load() sets each control from the file, through control_set(),
// so each is checked and applied as if a client had set it. A line for a
// control the radio does not have, or with a value it will not take, is
// passed over.
//
// A change is written out when nothing has changed for two seconds, so
// that a knob being turned does not write the file at every step. It is
// written to a file beside it and that file put in its place, so that a
// radio switched off in the middle does not lose what it had.
//
// NOT EVERYTHING IS KEPT
//
// Where the radio is tuned is not kept here: the frequency, the band and
// its stack, and the mode. The band's memories are a different matter and
// are not kept yet. TX is not a setting. SPAN is the panel's and begins
// wide. An action has no value.
//
// For the thread that owns the controls.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "settings.h"

#define PATH_MAX_LENGTH 200
#define LINE_MAX_LENGTH 200
#define QUIET_FOR_MS 2000
#define NOT_WAITING (-1L)

static const char *const not_kept[] = { "FREQ", "BAND", "STACK", "MODE", "TX", "SPAN" };
#define NOT_KEPT ((int)(sizeof(not_kept) / sizeof(not_kept[0])))

static char file_path[PATH_MAX_LENGTH];
static long save_at_ms = NOT_WAITING;

static int is_kept(const struct control *c) {
	int i;

	if (c->kind == CONTROL_ACTION)
		return 0;
	for (i = 0; i < NOT_KEPT; i++) {
		if (!strcmp(c->name, not_kept[i]))
			return 0;
	}
	return 1;
}

void settings_load(const char *path) {
	char line[LINE_MAX_LENGTH];
	char why[CONTROL_WHY_MAX];
	const struct control *c;
	char *value;
	FILE *file;
	int i;

	snprintf(file_path, sizeof(file_path), "%s", path);
	file = fopen(file_path, "r");
	if (file == NULL)
		return;
	while (fgets(line, sizeof(line), file) != NULL) {
		line[strcspn(line, "\r\n")] = 0;
		value = strchr(line, '=');
		if (value == NULL)
			continue;
		*value++ = 0;
		for (i = 0; i < control_count(); i++) {
			c = control_at(i);
			if (!strcmp(c->name, line) && is_kept(c))
				control_set(line, value, ORIGIN_NONE, why, sizeof(why));
		}
	}
	fclose(file);
	save_at_ms = NOT_WAITING;
}

void settings_save(void) {
	char beside[PATH_MAX_LENGTH + 8];
	const struct control *c;
	FILE *file;
	int i;

	save_at_ms = NOT_WAITING;
	if (file_path[0] == 0)
		return;
	snprintf(beside, sizeof(beside), "%s.new", file_path);
	file = fopen(beside, "w");
	if (file == NULL)
		return;
	for (i = 0; i < control_count(); i++) {
		c = control_at(i);
		if (is_kept(c))
			fprintf(file, "%s=%s\n", c->name, c->value);
	}
	if (fclose(file) == 0)
		rename(beside, file_path);
}

void settings_changed(const struct control *c, long now_ms) {
	if (is_kept(c))
		save_at_ms = now_ms + QUIET_FOR_MS;
}

void settings_poll(long now_ms) {
	if (save_at_ms != NOT_WAITING && now_ms >= save_at_ms)
		settings_save();
}
