// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// main.cpp — the main screen: two controls, the frequency and the volume.
//
// One of the two has the focus, and it is the one with the bright border.
// Turning the knob changes the control that has the focus. Pressing the knob
// gives the focus to the other one. Touching a control gives it the focus
// too.
//
// The controls are listed in main_list, each a struct field as in the
// zbitx's shipping firmware. A field's selection holds its least value, its
// greatest and its step, one to a line. Its value is kept as text, which is
// what will go to the radio.
//
// The frequency is shown in the way of a radio's dial, with a dot after the
// megahertz and after the kilohertz: 7.040.000.
//
// Whenever a control is changed on the panel, the radio is told of it over
// the link, and the panel prints the label and the value on the serial port,
// as "FREQ 7040100".
//
// The radio may tell the panel a control's value too: when either of them
// starts, and when something else has changed it. The operator's hand comes
// first. A value from the radio is not taken for a control that was changed
// on the panel less than a second ago, or the dial would jump back under
// the knob while it is being turned.
//
// The rest of the screen, below the controls, is kept for the waterfall.

#include <Arduino.h>
#include <EEPROM.h>
#include <lvgl.h>
#include <WiFi.h>
#include "link.h"
#include "ui.h"

#define CONTROLS_TOP 6
#define CONTROLS_HEIGHT 84
#define FREQ_LEFT 6
#define FREQ_WIDTH 312
#define VOLUME_LEFT 324
#define VOLUME_WIDTH 150

#define TITLE_LEFT 8
#define TITLE_TOP 4
#define VALUE_DOWN 12
#define CORNER_RADIUS 6
#define BORDER_WITH_FOCUS 3
#define BORDER_WITHOUT 1

#define COLOUR_SCREEN 0x000000
#define COLOUR_BOX 0x101820
#define COLOUR_TITLE 0x90A0B0
#define COLOUR_VALUE 0xFFFFFF
#define COLOUR_WITH_FOCUS 0xFFC000
#define COLOUR_WITHOUT 0x405060

#define OPERATOR_WINS_FOR_MS 1000
#define NOT_ON_THE_LINK (-1)

#define HZ_PER_MHZ 1000000
#define HZ_PER_KHZ 1000
#define SHOWN_MAX 24

//what LVGL has made on the screen for a field; the field's data points to it
struct shown {
	lv_obj_t *box;
	lv_obj_t *value;
};

lv_obj_t *main_screen = NULL;
static lv_obj_t *note = NULL;

struct field main_list[] = {
	{FIELD_FREQ, FREQ_LEFT, CONTROLS_TOP, FREQ_WIDTH, CONTROLS_HEIGHT, TFT_BLACK,
		"FREQ", "7088000", "500000\n30000000\n100"},
	{FIELD_NUMBER, VOLUME_LEFT, CONTROLS_TOP, VOLUME_WIDTH, CONTROLS_HEIGHT, TFT_BLACK,
		"VOLUME", "60", "0\n100\n1"},
	{-1}
};

static struct shown shown_list[sizeof(main_list) / sizeof(main_list[0])];
static struct field *f_selected = NULL;

static int control_of(struct field *f){
	if (!strcmp(f->label, "FREQ"))
		return LINK_FREQ;
	if (!strcmp(f->label, "VOLUME"))
		return LINK_VOLUME;
	return NOT_ON_THE_LINK;
}

static void field_show_value(struct field *f){
	struct shown *s = (struct shown *)f->data;
	char text[SHOWN_MAX];
	int v = atoi(f->value);

	if (f->type == FIELD_FREQ)
		snprintf(text, sizeof(text), "%d.%03d.%03d", v / HZ_PER_MHZ,
			(v % HZ_PER_MHZ) / HZ_PER_KHZ, v % HZ_PER_KHZ);
	else
		snprintf(text, sizeof(text), "%d", v);
	lv_label_set_text(s->value, text);
}

static void field_show_focus(struct field *f){
	struct shown *s = (struct shown *)f->data;

	if (f == f_selected){
		lv_obj_set_style_border_color(s->box, lv_color_hex(COLOUR_WITH_FOCUS), 0);
		lv_obj_set_style_border_width(s->box, BORDER_WITH_FOCUS, 0);
	}
	else {
		lv_obj_set_style_border_color(s->box, lv_color_hex(COLOUR_WITHOUT), 0);
		lv_obj_set_style_border_width(s->box, BORDER_WITHOUT, 0);
	}
}

static void field_select(struct field *f){
	f_selected = f;
	for (int i = 0; main_list[i].type != -1; i++)
		field_show_focus(main_list + i);
}

static void field_change(struct field *f, int steps){
	int least, greatest, step;
	int v = atoi(f->value);

	if (sscanf(f->selection, "%d\n%d\n%d", &least, &greatest, &step) != 3)
		return;
	v = ((v / step) + steps) * step;
	if (v < least)
		v = least;
	if (v > greatest)
		v = greatest;
	if (v == atoi(f->value))
		return;

	snprintf(f->value, sizeof(f->value), "%d", v);
	f->last_user_change = millis();
	field_show_value(f);
	Serial.printf("%s %s\n", f->label, f->value);
	if (control_of(f) != NOT_ON_THE_LINK)
		i2c_link_changed(control_of(f), v);
}

static void on_field_touched(lv_event_t *e){
	struct field *f = (struct field *)lv_event_get_user_data(e);

	field_select(f);
}

static void field_create(lv_obj_t *mom, struct field *f, struct shown *s){
	lv_obj_t *title;

	s->box = lv_obj_create(mom);
	lv_obj_set_size(s->box, f->w, f->h);
	lv_obj_align(s->box, LV_ALIGN_TOP_LEFT, f->x, f->y);
	lv_obj_set_style_radius(s->box, CORNER_RADIUS, 0);
	lv_obj_set_style_pad_all(s->box, 0, 0);
	lv_obj_set_style_bg_color(s->box, lv_color_hex(COLOUR_BOX), 0);
	lv_obj_remove_flag(s->box, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(s->box, on_field_touched, LV_EVENT_CLICKED, f);

	title = lv_label_create(s->box);
	lv_label_set_text(title, f->label);
	lv_obj_set_style_text_color(title, lv_color_hex(COLOUR_TITLE), 0);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
	lv_obj_align(title, LV_ALIGN_TOP_LEFT, TITLE_LEFT, TITLE_TOP);

	s->value = lv_label_create(s->box);
	lv_obj_set_style_text_color(s->value, lv_color_hex(COLOUR_VALUE), 0);
	lv_obj_set_style_text_font(s->value, &lv_font_montserrat_40, 0);
	lv_obj_align(s->value, LV_ALIGN_CENTER, 0, VALUE_DOWN);

	f->data = s;
	field_show_value(f);
}

void main_radio_says(int control, int32_t value){
	struct field *f;

	for (f = main_list; f->type != -1; f++){
		if (control_of(f) != control)
			continue;
		if (f->last_user_change != 0
				&& millis() - f->last_user_change < OPERATOR_WINS_FOR_MS)
			return;
		snprintf(f->value, sizeof(f->value), "%d", (int)value);
		field_show_value(f);
	}
}

//a few words in the corner of the frequency's box, for whatever is being tried
void main_note(const char *text){
	if (note)
		lv_label_set_text(note, text);
}

void main_knob_turned(int steps){
	if (f_selected)
		field_change(f_selected, steps);
}

void main_knob_pressed(){
	struct field *next;

	if (!f_selected)
		return;
	next = f_selected + 1;
	if (next->type == -1)
		next = main_list;
	field_select(next);
}

void main_ui(){

	if (!main_screen){
		Serial.println("Creating the main screen");
		lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(COLOUR_SCREEN), 0);

		main_screen = lv_obj_create(lv_screen_active());
		lv_obj_set_size(main_screen, TFT_VER_RES, TFT_HOR_RES);
		lv_obj_align(main_screen, LV_ALIGN_TOP_LEFT, 0, 0);
		lv_obj_set_style_bg_color(main_screen, lv_color_hex(COLOUR_SCREEN), 0);
		lv_obj_set_style_border_width(main_screen, 0, 0);
		lv_obj_set_style_radius(main_screen, 0, 0);
		lv_obj_set_style_pad_all(main_screen, 0, 0);
		lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

		for (int i = 0; main_list[i].type != -1; i++)
			field_create(main_screen, main_list + i, shown_list + i);
		field_select(main_list);

		note = lv_label_create(shown_list[0].box);
		lv_label_set_text(note, "");
		lv_obj_set_style_text_color(note, lv_color_hex(COLOUR_WITH_FOCUS), 0);
		lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
		lv_obj_align(note, LV_ALIGN_TOP_RIGHT, -TITLE_LEFT, TITLE_TOP);
	}
	lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}
