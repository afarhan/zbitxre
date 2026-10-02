// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_panel_controls.c — the radio describes its controls to the panel.
//
// It needs no radio and no panel. panel_controls.c is given a table of
// controls and, in place of hw/panel.c, the radio's end of a link whose
// other end is here too. Frames go across as tests/test_link.c sends them.
// What the panel's end takes is what a panel would be told.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "link.h"
#include "panel.h"
#include "panel_controls.h"

#define PANEL_ROOM (LINK_ANSWER_MAX - 1)
#define TURNS_TO_SAY_ALL 12
#define DESCRIBED_MAX 16
#define TOOK_MAX 32
#define MANY_CONTROLS 30
#define TEXT_MAX 32

static struct link_end radio;
static struct link_end panel;

static struct link_description described[DESCRIBED_MAX];
static int described_count;
static int ends_seen;
static int frames_of_descriptions;
static int values_before_the_end;

static int took_number[TOOK_MAX];
static int32_t took_value[TOOK_MAX];
static int took_count;

static char text_taken[TEXT_MAX];
static int text_number;
static int texts_taken;

static int lose_the_word;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

// in place of hw/panel.c
void panel_set(int control, int32_t value) {
	link_offer(&radio, control, value);
}

void panel_set_kept_bytes(int control, const uint8_t *bytes, int count) {
	link_offer_kept_bytes(&radio, control, bytes, count);
}

int panel_is_waiting(int control) {
	return link_is_waiting(&radio, control);
}

static void on_panel_took(int control, int32_t value) {
	if (control == LINK_HELLO)
		return;
	if (ends_seen == 0)
		values_before_the_end++;
	if (took_count < TOOK_MAX) {
		took_number[took_count] = control;
		took_value[took_count] = value;
	}
	took_count++;
}

static void on_panel_took_bytes(int control, const uint8_t *bytes, int count) {
	struct link_description d;
	int at = 0;
	int taken;

	if (control >= LINK_FIRST_DESCRIBED && count < TEXT_MAX) {
		if (ends_seen == 0)
			values_before_the_end++;
		memcpy(text_taken, bytes, count);
		text_taken[count] = 0;
		text_number = control;
		texts_taken++;
	}
	if (control != LINK_DESCRIBE)
		return;
	frames_of_descriptions++;
	while (at < count) {
		taken = link_get_description(bytes + at, count - at, &d);
		if (taken < 0)
			return;
		at += taken;
		if (d.number == LINK_NO_MORE_CONTROLS)
			ends_seen++;
		else if (described_count < DESCRIBED_MAX)
			described[described_count++] = d;
	}
}

static void one_turn(void) {
	uint8_t answer[LINK_ANSWER_MAX + LINK_FRAME_MAX];
	uint8_t word[LINK_FRAME_MAX];
	int count;

	count = link_make_answer(&panel, answer);
	link_take_frame(&radio, answer + 1, answer[0], NULL, NULL);
	panel_controls_poll();
	if (!link_has_something_to_say(&radio))
		return;
	count = link_make_frame(&radio, word);
	if (!lose_the_word)
		link_take_frame(&panel, word, count, on_panel_took, on_panel_took_bytes);
}

static void turns(int count) {
	int i;

	for (i = 0; i < count; i++)
		one_turn();
}

static void forget_what_was_taken(void) {
	described_count = 0;
	ends_seen = 0;
	frames_of_descriptions = 0;
	values_before_the_end = 0;
	took_count = 0;
	texts_taken = 0;
}

static int value_taken_for(int number, int32_t *value) {
	int found = 0;
	int i;

	for (i = 0; i < took_count && i < TOOK_MAX; i++) {
		if (took_number[i] == number) {
			*value = took_value[i];
			found = 1;
		}
	}
	return found;
}

static const struct link_description *description_of(const char *name) {
	int i;

	for (i = 0; i < described_count; i++) {
		if (strcmp(described[i].name, name) == 0)
			return &described[i];
	}
	return NULL;
}

static struct control freq = {
	.group = "RADIO", .name = "FREQ", .kind = CONTROL_NUMBER,
	.min = 500000, .max = 30000000, .step = 10, .value = "7035000",
};
static struct control audio = {
	.group = "RADIO", .name = "AUDIO", .kind = CONTROL_NUMBER,
	.min = 0, .max = 100, .step = 1, .value = "60",
};
static struct control mode = {
	.group = "RADIO", .name = "MODE", .kind = CONTROL_CHOICE,
	.choices = "USB/LSB/CW/CWR/AM/2TONE", .value = "CW",
};
static struct control callsign = {
	.group = "STATION", .name = "CALLSIGN", .kind = CONTROL_TEXT, .value = "VU2ESE",
};
static struct control pitch = {
	.group = "CW", .name = "PITCH", .kind = CONTROL_NUMBER,
	.min = 100, .max = 3000, .step = 10, .value = "600",
};

static void check_describing(void) {
	const struct link_description *d;
	int32_t value = 0;

	link_start(&radio, LINK_FRAME_MAX);
	link_start(&panel, PANEL_ROOM);
	forget_what_was_taken();
	panel_controls_describe();
	turns(TURNS_TO_SAY_ALL);

	expect(described_count == 5 && ends_seen == 1,
		"the panel is told of the five controls, and that there are no more");
	d = description_of("CALLSIGN");
	expect(d != NULL && d->kind == LINK_KIND_TEXT && d->number == LINK_FIRST_DESCRIBED + 1,
		"a control that holds text is described as text");
	d = description_of("FREQ");
	expect(d != NULL && d->number == LINK_FREQ && d->kind == LINK_KIND_NUMBER
		&& d->min == 500000 && d->max == 30000000 && d->step == 10
		&& strcmp(d->group, "RADIO") == 0,
		"FREQ keeps its number and is described with its range and its group");
	d = description_of("AUDIO");
	expect(d != NULL && d->number == LINK_VOLUME, "AUDIO keeps the volume's number");
	d = description_of("MODE");
	expect(d != NULL && d->number == LINK_FIRST_DESCRIBED && d->kind == LINK_KIND_CHOICE
		&& strcmp(d->choices, "USB/LSB/CW/CWR/AM/2TONE") == 0,
		"MODE is the first of the rest, a choice with its choices");
	d = description_of("PITCH");
	expect(d != NULL && d->number == LINK_FIRST_DESCRIBED + 2 && strcmp(d->group, "CW") == 0,
		"PITCH is numbered after the text control");

	expect(values_before_the_end == 0 && took_count == 4 && texts_taken == 1,
		"the values are said after the descriptions, one for each control");
	expect(text_number == LINK_FIRST_DESCRIBED + 1 && strcmp(text_taken, "VU2ESE") == 0,
		"the text control's value is its text");
	expect(value_taken_for(LINK_FREQ, &value) && value == 7035000,
		"FREQ's value is its number");
	expect(value_taken_for(LINK_FIRST_DESCRIBED, &value) && value == 2,
		"MODE's is which choice it is: CW is the third");
	expect(value_taken_for(LINK_FIRST_DESCRIBED + 2, &value) && value == 600,
		"PITCH's is 600");
}

static void check_changes(void) {
	char text[TEXT_MAX];
	const char *name;
	int32_t value = 0;

	forget_what_was_taken();
	ends_seen = 1;
	control_update("MODE", "AM");
	panel_controls_changed(&mode);
	turns(2);
	expect(took_count == 1 && value_taken_for(LINK_FIRST_DESCRIBED, &value) && value == 4,
		"a change of mode is said to the panel as the new choice");

	name = panel_controls_took(LINK_FIRST_DESCRIBED, 1, text, sizeof(text));
	expect(name != NULL && strcmp(name, "MODE") == 0 && strcmp(text, "LSB") == 0,
		"what the panel says of a choice comes back as the control's name and the choice");
	name = panel_controls_took(LINK_FIRST_DESCRIBED + 2, 700, text, sizeof(text));
	expect(name != NULL && strcmp(name, "PITCH") == 0 && strcmp(text, "700") == 0,
		"and of a number as its name and the number");
	expect(panel_controls_took(LINK_FIRST_DESCRIBED, 9, text, sizeof(text)) == NULL,
		"a choice the control does not have is refused");
	expect(panel_controls_took(LINK_FIRST_DESCRIBED + 9, 1, text, sizeof(text)) == NULL
		&& panel_controls_took(0, 1, text, sizeof(text)) == NULL,
		"and so is a number that is no control's");
}

// a text from the panel, as the panel would send it: in parts
static void check_a_text_from_the_panel(void) {
	const char *new_text = "A LONGER CALLSIGN/P";
	uint8_t part[LINK_TEXT_PART_HEADER + LINK_TEXT_PART_DATA];
	char text[TEXT_MAX];
	const char *name = NULL;
	int parts = 0;
	int offset = 0;
	int count;

	while (name == NULL && parts < TEXT_MAX) {
		count = link_put_text_part(part, LINK_FIRST_DESCRIBED + 1, new_text, offset);
		name = panel_controls_took_bytes(LINK_TEXT_PART, part, count, text, sizeof(text));
		offset += count - LINK_TEXT_PART_HEADER;
		parts++;
	}
	expect(parts == 3 && name != NULL && strcmp(name, "CALLSIGN") == 0
		&& strcmp(text, new_text) == 0,
		"a text of 19 characters comes from the panel in three parts, whole");
	count = link_put_text_part(part, LINK_FIRST_DESCRIBED + 1, "", 0);
	name = panel_controls_took_bytes(LINK_TEXT_PART, part, count, text, sizeof(text));
	expect(name != NULL && text[0] == 0, "an empty text is one part");
	count = link_put_text_part(part, LINK_FIRST_DESCRIBED + 2, "600", 0);
	expect(panel_controls_took_bytes(LINK_TEXT_PART, part, count, text, sizeof(text)) == NULL,
		"a text for a control that does not hold text is refused");
}

static void check_a_lost_frame(void) {
	forget_what_was_taken();
	panel_controls_describe();
	lose_the_word = 1;
	turns(2);
	lose_the_word = 0;
	turns(TURNS_TO_SAY_ALL);
	expect(described_count == 5 && ends_seen == 1 && took_count == 4,
		"descriptions whose frame is lost are said again, and the values follow");
}

static struct control many[MANY_CONTROLS];
static char many_names[MANY_CONTROLS][CONTROL_NAME_MAX];

static void check_many_controls(void) {
	int i;

	for (i = 0; i < MANY_CONTROLS; i++) {
		snprintf(many_names[i], sizeof(many_names[i]), "CONTROL_%02d", i);
		many[i].group = "MANY";
		many[i].name = many_names[i];
		many[i].kind = CONTROL_CHOICE;
		many[i].choices = "STRAIGHT/IAMBIC/IAMBICB/BUG/ULTIMATIC";
		snprintf(many[i].value, sizeof(many[i].value), "BUG");
		control_register(&many[i]);
	}
	forget_what_was_taken();
	panel_controls_describe();
	turns(4 * TURNS_TO_SAY_ALL);
	expect(frames_of_descriptions > 1 && ends_seen == 1
		&& took_count == 4 + MANY_CONTROLS && values_before_the_end == 0,
		"thirty more controls take several frames, and every value still follows the last");
}

int main(void) {
	control_register(&freq);
	control_register(&audio);
	control_register(&mode);
	control_register(&callsign);
	control_register(&pitch);

	check_describing();
	check_changes();
	check_a_text_from_the_panel();
	check_a_lost_frame();
	check_many_controls();

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("panel controls: all checks passed\n");
	return EXIT_SUCCESS;
}
