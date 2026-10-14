// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// controls.cpp — the controls the panel shows: the radio's, as the radio
// describes them, and the panel's own.
//
// The panel is not built knowing what controls the radio has. The radio
// describes them over the link, each with its number on the link, its
// name, its group, and its range or its choices; link.c says how. This
// file keeps what was described, and each control's value.
//
// The panel has controls of its own too, which the radio never hears of:
// the tuning step, OFFSET (NORM, RIT or SPLIT) and the VFO in use. The
// author's rule is that tuning belongs to each client, since the zbitx's
// knob is on the panel and not on the radio. They are kept in the same
// table, in the group PANEL, with no number on the link, so that the
// screen treats every control alike. They come first in the table and stay
// when the radio describes its controls afresh.
//
// A value is kept as a number. A choice's value is which of its choices it
// is, counted from 0, as it is on the link. An action, the macros', has
// choices and no value: the things it can do. A text control's value is its
// text.
//
// WHO CALLS
//
// The descriptions arrive in the link's interrupt, a frame at a time, and
// the screen is drawn from loop(). So what arrives is staged, and only when
// the radio has said that there are no more is it made the table that the
// screen uses, by controls_slice(), from loop(), with interrupts held off
// while it is copied.

#include <Arduino.h>
#include "controls.h"

#define CHOICES_BETWEEN '/'

static struct link_description staged[CONTROLS_MAX];
static volatile int staged_count;
static volatile bool staged_is_complete;
static volatile bool staged_is_new;

static struct panel_control table[CONTROLS_MAX];
static int own_count;
static int count;

//the radio has started afresh, or is describing afresh
void controls_forget_staged(){
	staged_count = 0;
	staged_is_complete = false;
	staged_is_new = false;
}

static bool is_staged(int number){
	int i;

	for (i = 0; i < staged_count; i++){
		if (staged[i].number == number)
			return true;
	}
	return false;
}

//a control that is described a second time means that the radio has begun
//describing afresh, as it does when the panel says HELLO just after the
//radio has started: what was staged of the first telling is dropped
void controls_stage(const uint8_t *bytes, int bytes_count){
	struct link_description d;
	int at = 0;
	int taken;

	if (staged_is_complete)
		controls_forget_staged();
	while (at < bytes_count){
		taken = link_get_description(bytes + at, bytes_count - at, &d);
		if (taken < 0)
			return;
		at += taken;
		if (d.number == LINK_NO_MORE_CONTROLS){
			staged_is_complete = true;
			staged_is_new = true;
			return;
		}
		if (is_staged(d.number))
			staged_count = 0;
		if (staged_count < CONTROLS_MAX){
			staged[staged_count] = d;
			staged_count = staged_count + 1;
		}
	}
}

//makes what the radio described the table in use; says whether it did
bool controls_slice(){
	int i;

	if (!staged_is_new)
		return false;
	noInterrupts();
	count = own_count;
	for (i = 0; i < staged_count && count < CONTROLS_MAX; i++){
		memset(&table[count], 0, sizeof(table[count]));
		table[count].d = staged[i];
		count++;
	}
	staged_is_new = false;
	interrupts();
	return true;
}

//the panel's own are added before the radio is heard from
struct panel_control *controls_add_own(const char *name, const char *choices,
		int32_t least, int32_t greatest, int32_t value){
	struct panel_control *c;

	if (own_count != count || count >= CONTROLS_MAX)
		return NULL;
	c = &table[count];
	memset(c, 0, sizeof(*c));
	c->d.number = NOT_ON_THE_LINK;
	c->d.kind = LINK_KIND_NUMBER;
	c->d.min = least;
	c->d.max = greatest;
	c->d.step = 1;
	snprintf(c->d.name, sizeof(c->d.name), "%s", name);
	snprintf(c->d.group, sizeof(c->d.group), "%s", OWN_GROUP);
	if (choices != NULL){
		c->d.kind = LINK_KIND_CHOICE;
		snprintf(c->d.choices, sizeof(c->d.choices), "%s", choices);
	}
	c->value = value;
	own_count++;
	count++;
	return c;
}

int controls_count(){
	return count;
}

struct panel_control *controls_at(int index){
	if (index < 0 || index >= count)
		return NULL;
	return &table[index];
}

struct panel_control *controls_find(const char *name){
	int i;

	for (i = 0; i < count; i++){
		if (!strcmp(table[i].d.name, name))
			return &table[i];
	}
	return NULL;
}

struct panel_control *controls_of_number(int number){
	int i;

	if (number == NOT_ON_THE_LINK)
		return NULL;
	for (i = own_count; i < count; i++){
		if (table[i].d.number == number)
			return &table[i];
	}
	return NULL;
}

int controls_choices(const struct panel_control *c){
	const char *at = c->d.choices;
	int choices = 0;

	if (c->d.kind == LINK_KIND_NUMBER || c->d.kind == LINK_KIND_TEXT || at[0] == 0)
		return 0;
	choices = 1;
	while ((at = strchr(at, CHOICES_BETWEEN)) != NULL){
		choices++;
		at++;
	}
	return choices;
}

//a control's value as it is shown: the number, or the choice by its name
void controls_value_text(const struct panel_control *c, char *text, int room){
	const char *at = c->d.choices;
	const char *end;
	int index = (int)c->value;
	int length;

	if (c->d.kind == LINK_KIND_TEXT){
		snprintf(text, room, "%s", c->text);
		return;
	}
	if (c->d.kind == LINK_KIND_NUMBER){
		snprintf(text, room, "%d", (int)c->value);
		return;
	}
	while (at != NULL && index > 0){
		at = strchr(at, CHOICES_BETWEEN);
		if (at != NULL)
			at++;
		index--;
	}
	text[0] = 0;
	if (at == NULL)
		return;
	end = strchr(at, CHOICES_BETWEEN);
	if (end == NULL)
		end = at + strlen(at);
	length = (int)(end - at);
	if (length >= room)
		length = room - 1;
	memcpy(text, at, length);
	text[length] = 0;
}

//whether a choice is at the one named; false if there is no such control
bool controls_is(const char *name, const char *choice){
	struct panel_control *c = controls_find(name);
	char text[CONTROL_TEXT_MAX];

	if (c == NULL)
		return false;
	controls_value_text(c, text, sizeof(text));
	return !strcmp(text, choice);
}
