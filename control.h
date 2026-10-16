// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// control.h — the radio's controls, in one table. See control.c.

#pragma once

#define CONTROL_NAME_MAX 16
#define CONTROL_VALUE_MAX 64
#define CONTROL_WHY_MAX 80
#define CONTROLS_MAX 96

// who made a change, so that it is not told of it again
#define ORIGIN_NONE (-1)
#define ORIGIN_TERMINAL (-2)
#define ORIGIN_PANEL (-3)

enum control_kind {
	CONTROL_NUMBER,
	CONTROL_CHOICE,
	CONTROL_TEXT,
	CONTROL_ACTION,
};

struct control {
	const char *group;
	const char *name;
	enum control_kind kind;
	int min;
	int max;
	int step;
	const char *choices;
	char value[CONTROL_VALUE_MAX];
	int (*apply)(struct control *c, const char *value, char *why, int room);
};

typedef void (*control_notify_fn)(const struct control *c, int origin);

int control_register(struct control *c);
void control_on_change(control_notify_fn notify);

int control_set(const char *name, const char *value, int origin, char *why, int room);
void control_update(const char *name, const char *value);
const char *control_get(const char *name);
int control_get_int(const char *name);

int control_count(void);
const struct control *control_at(int index);
