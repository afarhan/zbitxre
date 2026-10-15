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
// WHAT IS KEPT
//
// Every control's value, the frequency and the mode among them, so that
// the radio comes back where it was left. The file is in the order of the
// controls' table, in which the frequency comes before the mode, and
// setting the frequency sets the band.
//
// Not kept as controls: BAND and STACK, which follow from the frequency,
// and setting STACK would move the radio to another of the band's places.
// TX is not a setting. An action has no value. The clock's two controls are the
// time and not a setting. Nor are the groups METER, what the panel measures
// as the radio transmits, and LOGGER, the entry being written for the
// logbook, which is done with when it is saved.
//
// The author's rule is that everything persists, the band stack too. The
// bands' places are not controls, so radio.c writes and reads them itself,
// through the two functions given to settings_set_extras(). Their lines come
// after the controls' in the file, and so are read after the frequency has
// been set, which would otherwise note the radio's starting frequency in a
// place as the band was left.
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

static const char *const not_kept[] = { "BAND", "STACK", "TX", "CLOCK", "TIME_SET", "NEW_COOKIE" };
#define NOT_KEPT ((int)(sizeof(not_kept) / sizeof(not_kept[0])))
// what the panel measures, the entry being written for the logbook, and
// what was last said of reaching the radio from afar, and of VoIP: who is
// heard, what the gateway says, and the owner's speaking
static const char *const groups_not_kept[] = { "METER", "LOGGER", "REMOTE", "VOIP" };
#define GROUPS_NOT_KEPT ((int)(sizeof(groups_not_kept) / sizeof(groups_not_kept[0])))

static char file_path[PATH_MAX_LENGTH];
static settings_save_fn save_extras;
static settings_load_fn load_extra;
static long save_at_ms = NOT_WAITING;

static int is_kept(const struct control *c) {
	int i;

	if (c->kind == CONTROL_ACTION)
		return 0;
	for (i = 0; i < NOT_KEPT; i++) {
		if (!strcmp(c->name, not_kept[i]))
			return 0;
	}
	for (i = 0; i < GROUPS_NOT_KEPT; i++) {
		if (!strcmp(c->group, groups_not_kept[i]))
			return 0;
	}
	return 1;
}

void settings_set_extras(settings_save_fn save, settings_load_fn load) {
	save_extras = save;
	load_extra = load;
}

// a line of the file: a control's, if there is a control of that name that
// is kept, or else one of the extras'
static void take_a_line(const char *name, const char *value) {
	char why[CONTROL_WHY_MAX];
	const struct control *c;
	int i;

	for (i = 0; i < control_count(); i++) {
		c = control_at(i);
		if (strcmp(c->name, name))
			continue;
		if (is_kept(c))
			control_set(name, value, ORIGIN_NONE, why, sizeof(why));
		return;
	}
	if (load_extra != NULL)
		load_extra(name, value);
}

void settings_load(const char *path) {
	char line[LINE_MAX_LENGTH];
	char *value;
	FILE *file;

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
		take_a_line(line, value);
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
	if (save_extras != NULL)
		save_extras(file);
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
