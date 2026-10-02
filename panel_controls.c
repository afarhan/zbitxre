// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_controls.c — the radio's controls, as the front panel is told of
// them and as it changes them.
//
// The panel is not built knowing what controls the radio has. Each modem
// registers its own in the one table, control.c, and the panel has to show
// whatever is there. So the radio describes the table to the panel over
// the link, as it gives a network client its schema: front_panel/link.c
// says what a description is. The author's rule for the panel is that it
// lays itself out: it has places, to the pixel, for the controls it knows
// by name, and whatever else the radio describes goes into its list. So
// nothing here says where a control is shown.
//
// A control is known on the link by a number. FREQ and AUDIO have the
// numbers they always had; the rest are numbered in the order of the table.
// Controls that hold text have no number yet and are not described.
//
// A number's value goes as it is. A choice's goes as which of its choices
// it is, counted from 0, and comes back the same way.
//
// WHEN
//
// panel_controls_describe() starts the describing: when the radio starts,
// and whenever the panel says HELLO, since a panel that has just started
// knows nothing. The descriptions go a frame at a time, each when the
// panel has taken the one before, which panel_controls_poll() sees to; it
// is called each time round the radio's loop. When the last is taken,
// every control's value is said. Until then a change is not said, since
// the value will be said at the end anyway.
//
// WHO CALLS
//
// The thread that owns the controls and polls the panel.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "link.h"
#include "panel.h"
#include "panel_controls.h"

#define NOT_ON_THE_LINK 0
#define CHOICES_BETWEEN '/'

static uint8_t number_of[CONTROLS_MAX];
static uint8_t said[LINK_COUNTED_MAX];
static int next_to_describe;
static int is_describing;
static int is_the_end_said;

// the controls are given their numbers afresh each time they are described;
// the table does not change once the radio has started
static void number_the_controls(void) {
	const struct control *c;
	int next = LINK_FIRST_DESCRIBED;
	int i;

	memset(number_of, NOT_ON_THE_LINK, sizeof(number_of));
	for (i = 0; i < control_count() && i < CONTROLS_MAX; i++) {
		c = control_at(i);
		if (c->kind == CONTROL_TEXT)
			continue;
		if (!strcmp(c->name, "FREQ"))
			number_of[i] = LINK_FREQ;
		else if (!strcmp(c->name, "AUDIO"))
			number_of[i] = LINK_VOLUME;
		else if (next < LINK_CONTROLS)
			number_of[i] = (uint8_t)next++;
	}
}

// which of a control's choices its value is, counted from 0
static int choice_index(const struct control *c, const char *value) {
	const char *at = c->choices;
	size_t length = strlen(value);
	int index = 0;

	while (at != NULL && *at != 0) {
		if (strncmp(at, value, length) == 0
				&& (at[length] == CHOICES_BETWEEN || at[length] == 0))
			return index;
		at = strchr(at, CHOICES_BETWEEN);
		if (at != NULL)
			at++;
		index++;
	}
	return 0;
}

// the choice at an index, copied to text; -1 if there is none
static int choice_at(const struct control *c, int index, char *text, int room) {
	const char *at = c->choices;
	const char *end;
	int length;

	while (at != NULL && index > 0) {
		at = strchr(at, CHOICES_BETWEEN);
		if (at != NULL)
			at++;
		index--;
	}
	if (at == NULL || *at == 0 || index < 0)
		return -1;
	end = strchr(at, CHOICES_BETWEEN);
	if (end == NULL)
		end = at + strlen(at);
	length = (int)(end - at);
	if (length >= room)
		return -1;
	memcpy(text, at, length);
	text[length] = 0;
	return 0;
}

static int32_t link_value(const struct control *c) {
	if (c->kind == CONTROL_CHOICE)
		return choice_index(c, c->value);
	return atoi(c->value);
}

static void describe(const struct control *c, int number,
		struct link_description *d) {
	memset(d, 0, sizeof(*d));
	d->number = (uint8_t)number;
	d->kind = LINK_KIND_NUMBER;
	d->min = c->min;
	d->max = c->max;
	d->step = c->step;
	snprintf(d->name, sizeof(d->name), "%s", c->name);
	snprintf(d->group, sizeof(d->group), "%s", c->group);
	if (c->kind == CONTROL_CHOICE) {
		d->kind = LINK_KIND_CHOICE;
		snprintf(d->choices, sizeof(d->choices), "%s", c->choices);
	}
}

// fills a frame's worth of descriptions from where the last left off, and
// after the last control the mark that there are no more
static int fill_descriptions(void) {
	struct link_description d;
	int used = 0;
	int put;

	while (next_to_describe < control_count()) {
		if (number_of[next_to_describe] == NOT_ON_THE_LINK) {
			next_to_describe++;
			continue;
		}
		describe(control_at(next_to_describe), number_of[next_to_describe], &d);
		put = link_put_description(said + used, (int)sizeof(said) - used, &d);
		if (put == 0)
			return used;
		used += put;
		next_to_describe++;
	}
	memset(&d, 0, sizeof(d));
	d.number = LINK_NO_MORE_CONTROLS;
	put = link_put_description(said + used, (int)sizeof(said) - used, &d);
	if (put > 0)
		is_the_end_said = 1;
	return used + put;
}

static void say_every_value(void) {
	int i;

	for (i = 0; i < control_count() && i < CONTROLS_MAX; i++) {
		if (number_of[i] != NOT_ON_THE_LINK)
			panel_set(number_of[i], link_value(control_at(i)));
	}
}

void panel_controls_describe(void) {
	number_the_controls();
	next_to_describe = 0;
	is_the_end_said = 0;
	is_describing = 1;
}

void panel_controls_poll(void) {
	int count;

	if (!is_describing || panel_is_waiting(LINK_DESCRIBE))
		return;
	if (is_the_end_said) {
		is_describing = 0;
		say_every_value();
		return;
	}
	count = fill_descriptions();
	panel_set_kept_bytes(LINK_DESCRIBE, said, count);
}

void panel_controls_changed(const struct control *c) {
	int i;

	if (is_describing)
		return;
	for (i = 0; i < control_count() && i < CONTROLS_MAX; i++) {
		if (control_at(i) == c && number_of[i] != NOT_ON_THE_LINK)
			panel_set(number_of[i], link_value(c));
	}
}

// what the panel said, as the control's name and the value's text; NULL if
// the number is no control's or the value no choice of it
const char *panel_controls_took(int number, int32_t value, char *text, int room) {
	const struct control *c;
	int i;

	for (i = 0; i < control_count() && i < CONTROLS_MAX; i++) {
		if (number == NOT_ON_THE_LINK || number_of[i] != number)
			continue;
		c = control_at(i);
		if (c->kind != CONTROL_CHOICE) {
			snprintf(text, room, "%d", (int)value);
			return c->name;
		}
		if (choice_at(c, (int)value, text, room) < 0)
			return NULL;
		return c->name;
	}
	return NULL;
}
