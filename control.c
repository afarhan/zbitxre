// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// control.c — the radio's controls, in one table.
//
// Everything an operator can set is a control: the frequency, the mode, the
// volume, and what a modem needs, such as the speed of the CW keyer. Each is
// a struct control, registered here by whoever owns it: radio.c registers
// the radio's own when it opens, and a modem registers its own in its
// start(). So a modem's controls live in the modem's file, and nothing else
// has to know of them.
//
// A CONTROL
//
//   group    who it belongs to, "RADIO", "CW", "FT8"; it is for a client to
//            show the controls in their groups.
//   name     what it is called, in the protocol and everywhere else: "WPM".
//            Names are unique; a second control of the same name is refused.
//   kind     a number, from min to max in steps of step; a choice, one of
//            choices, separated by "/"; text; or an action, which is
//            something done and not something set: sending a macro. An
//            action has choices, the things it can do, and no value. Setting
//            it to one of them does that, every time, and nobody is told.
//   value    what it is now, as text. Its owner gives it its first value.
//   apply    makes a new value so: asks the core, the keyer, whatever the
//            control works. It is given a value already checked. It returns
//            0, or -1 with the reason in why. It may change other controls on
//            the way, with control_update(): a new band moves the frequency.
//
// Every control exists all the time, whatever the mode, so a modem's
// controls keep their values while another mode is in use.
//
// A CHANGE, AND WHO IS TOLD OF IT
//
// control_set() is how every change is made, by any client. It checks the
// value, snapping a number onto its steps, and gives it to apply. Then it
// tells, through the function given to control_on_change(), everyone who
// should know:
//
//   A value that is already the control's changes nothing and is told to
//   nobody. Whatever a client does wrong, a change going round in a loop
//   stops here.
//
//   The one who made a change is not told of it, only everyone else: it
//   knows already. That keeps a client's own changes from coming back to it
//   late, while it has moved on, and it keeps a client from setting a widget
//   from the radio's answer and so sending the change again. The exception
//   is a value that was not taken as it was asked for, a number snapped onto
//   its steps: then the one who asked is told what was taken.
//
//   A control changed by another's apply, through control_update(), is told
//   to everyone, the one who made the first change included.
//
// origin is who made the change: a number a client is known by, or
// ORIGIN_TERMINAL, ORIGIN_PANEL, or ORIGIN_NONE for the radio itself.
//
// All of this is for the thread that owns the controls. The audio thread
// never touches a control; what an apply does to the audio thread's work, it
// does through the core's own handover.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"

#define CHOICE_SEPARATOR '/'

static struct control *controls[CONTROLS_MAX];
static int count;
static control_notify_fn notify;

int control_register(struct control *c) {
	if (count == CONTROLS_MAX)
		return -1;
	if (control_get(c->name) != NULL)
		return -1;
	controls[count++] = c;
	return 0;
}

void control_on_change(control_notify_fn fn) {
	notify = fn;
}

static struct control *find(const char *name) {
	int i;

	for (i = 0; i < count; i++) {
		if (!strcmp(controls[i]->name, name))
			return controls[i];
	}
	return NULL;
}

const char *control_get(const char *name) {
	struct control *c = find(name);

	if (c == NULL)
		return NULL;
	return c->value;
}

int control_get_int(const char *name) {
	const char *value = control_get(name);

	if (value == NULL)
		return 0;
	return atoi(value);
}

int control_count(void) {
	return count;
}

const struct control *control_at(int index) {
	if (index < 0 || index >= count)
		return NULL;
	return controls[index];
}

static void notify_change(const struct control *c, int origin) {
	if (notify != NULL)
		notify(c, origin);
}

static int is_integer(const char *text) {
	char *end;

	if (*text == 0)
		return 0;
	strtol(text, &end, 10);
	return *end == 0;
}

static int is_a_choice(const char *choices, const char *value) {
	size_t length = strlen(value);
	const char *at = choices;

	while (at != NULL && *at != 0) {
		if (!strncmp(at, value, length)
				&& (at[length] == CHOICE_SEPARATOR || at[length] == 0))
			return 1;
		at = strchr(at, CHOICE_SEPARATOR);
		if (at != NULL)
			at++;
	}
	return 0;
}

static int check(const struct control *c, const char *value, char *taken,
		char *why, int room) {
	int number;

	snprintf(taken, CONTROL_VALUE_MAX, "%s", value);
	if (c->kind == CONTROL_CHOICE || c->kind == CONTROL_ACTION) {
		if (is_a_choice(c->choices, value))
			return 0;
		snprintf(why, room, "one of %s", c->choices);
		return -1;
	}
	if (c->kind != CONTROL_NUMBER)
		return 0;
	number = atoi(value);
	if (!is_integer(value) || number < c->min || number > c->max) {
		snprintf(why, room, "a number from %d to %d", c->min, c->max);
		return -1;
	}
	if (c->step > 1)
		number = c->min + (number - c->min) / c->step * c->step;
	snprintf(taken, CONTROL_VALUE_MAX, "%d", number);
	return 0;
}

int control_set(const char *name, const char *value, int origin, char *why, int room) {
	struct control *c = find(name);
	char taken[CONTROL_VALUE_MAX];

	if (c == NULL) {
		snprintf(why, room, "no such control");
		return -1;
	}
	if (check(c, value, taken, why, room) < 0)
		return -1;
	if (c->kind == CONTROL_ACTION) {
		if (c->apply == NULL)
			return 0;
		return c->apply(c, taken, why, room);
	}
	if (!strcmp(c->value, taken))
		return 0;
	if (c->apply != NULL && c->apply(c, taken, why, room) < 0)
		return -1;
	snprintf(c->value, sizeof(c->value), "%s", taken);
	if (!strcmp(taken, value))
		notify_change(c, origin);
	else
		notify_change(c, ORIGIN_NONE);
	return 0;
}

void control_update(const char *name, const char *value) {
	struct control *c = find(name);

	if (c == NULL || !strcmp(c->value, value))
		return;
	snprintf(c->value, sizeof(c->value), "%s", value);
	notify_change(c, ORIGIN_NONE);
}
