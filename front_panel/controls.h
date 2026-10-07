// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// controls.h — the controls the panel shows: the radio's, as the radio
// describes them, and the panel's own. See controls.cpp.

#pragma once

#include <stdint.h>
#include "link.h"

#define CONTROLS_MAX 72
#define CONTROL_TEXT_MAX 16
//a text control's value, as the panel keeps it: a callsign, a grid
#define CONTROL_TEXT_ROOM 24
#define OWN_GROUP "PANEL"
#define NOT_ON_THE_LINK 0

struct panel_control {
	struct link_description d;
	int32_t value;
	char text[CONTROL_TEXT_ROOM];
	unsigned long last_user_change;
};

void controls_forget_staged();
void controls_stage(const uint8_t *bytes, int count);
bool controls_slice();

struct panel_control *controls_add_own(const char *name, const char *choices,
	int32_t least, int32_t greatest, int32_t value);
int controls_count();
struct panel_control *controls_at(int index);
struct panel_control *controls_find(const char *name);
struct panel_control *controls_of_number(int number);
int controls_choices(const struct panel_control *c);
void controls_value_text(const struct panel_control *c, char *text, int room);
bool controls_is(const char *name, const char *choice);
