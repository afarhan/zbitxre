// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// main.cpp — the main screen.
//
// WHERE EVERYTHING IS
//
// The author laid the screen out, and the places are exact to the pixel.
// Every widget is 48 high, as on the zbitx's shipping panel, and all but
// the frequency are 48 wide. The screen is 480 by 320:
//
//   y 0     BAND | FREQ 144 | RIT, AGC, DRIVE, IF, VOL | MENU
//   y 48    a button for each band, 10M to 80M, and MODE
//   y 96    on the left, 288 wide: the graph, 64 rows, the waterfall, 96,
//           and 16 rows kept for a meter; waterfall.cpp paints them
//   y 272   on the left: six buttons, the mode's own
//   y 96    on the right, 192 wide, to the bottom: the list, a title of 32
//           and four rows of 48
//
// The top row is the frequency and the radio's core controls. A band's
// button goes to that band at a touch. The bottom row is for what a mode
// needs often: WPM in CW. Whatever else there is, of the panel's own
// tuning controls and of the radio's that belong to the radio as a whole
// or to the mode in use, is in the list. The waterfall is not made deeper
// to fill the screen: waterfall.cpp says why.
//
// WHAT IS SHOWN
//
// The radio describes its controls over the link and controls.cpp keeps
// them. This file has the places, by the control's name. A control with a
// place is shown there; one without is listed. So a control that a new
// modem brings is on the panel, in the list, with nothing changed here.
//
// The controls of a mode are those in its modem's group: CW for CW and CWR,
// VOICE for USB, LSB and AM, and the mode's own name for any other. The
// desktop's window has the same rule.
//
// THE KNOB AND THE TOUCH SCREEN
//
// The author's rule: a widget is selected by touch and by nothing else,
// and the knob, its turning and its click, goes to the widget that is
// selected. The knob has three jobs, and which it does is plain from what
// was touched.
//
//   The frequency   turning tunes. The panel starts here, and a long press
//                   of the knob comes back here from anywhere.
//   A control in    turning changes its value, and the radio follows as it
//   the top rows    turns. A click moves a choice to its next choice, and
//   or the bottom   round from the last to the first, so RIT is switched
//   row             with a click. A touch on a choice that is already
//                   selected does the same, for working by touch alone.
//   The list        turning scrolls: it moves the mark from one row to the
//                   next, and the list moves to keep the mark in view. A
//                   click takes the marked row up, and turning then changes
//                   its value; another click goes back to scrolling. A
//                   touch on a row selects the list with the mark on that
//                   row; a touch on the list's title selects it with the
//                   mark where it was.
//
// What is selected has a bright edge. A control whose value the knob is
// changing is filled as well. While the list is being scrolled its title
// is filled, and the marked row has the bright edge.
//
// TUNING
//
// The tuning is the panel's own, as it is each client's: the zbitx's knob
// is on the panel. It is the desktop window's, which was zbitxv2's. The
// dial moves by STEP, onto a multiple of it. While RIT is on, tuning moves
// the receiver's offset and leaves the dial. With SPLIT on, the
// transmitter is on the other VFO's dial. The radio is sent where the
// receiver listens, and, when the radio says it has gone to transmit,
// where the transmitter is to be, if that is elsewhere; and the receiver's
// frequency again when it comes back.
//
// THE OPERATOR'S HAND COMES FIRST
//
// A value from the radio is not taken for a control that was changed on
// the panel less than a second ago, or it would jump back under the knob
// while it is being turned.

#include <Arduino.h>
#include <lvgl.h>
#include "link.h"
#include "controls.h"
#include "ui.h"

#define ROW_HEIGHT 48
#define TILE_WIDTH 48
#define FREQ_LEFT 48
#define FREQ_WIDTH 144
#define MENU_LEFT 432
#define BANDS 9
#define FOOTER_TOP 272
#define FOOTER_TILES 6
#define LIST_LEFT 288
#define LIST_TOP 96
#define LIST_WIDTH 192
#define LIST_TITLE_HEIGHT 32
#define LIST_ROWS 4

#define TITLE_LEFT 3
#define TITLE_TOP 1
#define VALUE_UP 2
#define ROW_PAD 6
#define BORDER_WITH_FOCUS 2
#define BORDER_WITHOUT 1

#define COLOUR_SCREEN 0x000000
#define COLOUR_BOX 0x101820
#define COLOUR_TAKEN_UP 0x604800
#define COLOUR_TITLE 0x90A0B0
#define COLOUR_VALUE 0xFFFFFF
#define COLOUR_DIM 0x506070
#define COLOUR_WITH_FOCUS 0xFFC000
#define COLOUR_WITHOUT 0x405060

#define OPERATOR_WINS_FOR_MS 1000
#define HZ_PER_MHZ 1000000
#define HZ_PER_KHZ 1000
#define LOWEST_HZ 500000
#define HIGHEST_HZ 30000000
#define RIT_MAX_HZ 25000
#define STARTING_HZ 7088000
#define VFO_A 0
#define VFO_B 1
#define VFOS 2
#define NOT_USED_YET 0
#define SHOWN_MAX 24
#define NOWHERE (-1)

//what is selected, and so what the knob does
enum selection { ON_FREQUENCY, ON_A_CONTROL, ON_THE_LIST };

//a place on the screen, and what LVGL has made there
struct widget {
	lv_obj_t *box;
	lv_obj_t *title;
	lv_obj_t *value;
	struct panel_control *control;
};

//a control that has a place of its own in the top two rows
struct place {
	const char *control;
	const char *label;
	int16_t x;
	int16_t y;
	int16_t w;
};

//what a mode has on the bottom row
struct footer {
	const char *group;
	const char *controls[FOOTER_TILES];
};

static const struct place places[] = {
	{"BAND", "BAND", 0, 0, TILE_WIDTH},
	{"RIT", "RIT", 192, 0, TILE_WIDTH},
	{"AGC", "AGC", 240, 0, TILE_WIDTH},
	{"DRIVE", "DRIVE", 288, 0, TILE_WIDTH},
	{"IF", "IF", 336, 0, TILE_WIDTH},
	{"AUDIO", "VOL", 384, 0, TILE_WIDTH},
	{"MODE", "MODE", 432, ROW_HEIGHT, TILE_WIDTH},
};
#define PLACES ((int)(sizeof(places) / sizeof(places[0])))

//the second row's buttons, left to right, each a choice of BAND
static const char *const band_names[BANDS] = {
	"10M", "12M", "15M", "17M", "20M", "30M", "40M", "60M", "80M",
};

//the panel's own controls that are in the list, ahead of the radio's
static const char *const own_listed[] = { "STEP", "SPLIT", "VFO" };
#define OWN_LISTED ((int)(sizeof(own_listed) / sizeof(own_listed[0])))

static const struct footer footers[] = {
	{"CW", {"WPM", "PITCH", NULL, NULL, NULL, NULL}},
	{"VOICE", {"MIC", "TX", NULL, NULL, NULL, NULL}},
	{"2TONE", {"TX", NULL, NULL, NULL, NULL, NULL}},
};
#define FOOTERS ((int)(sizeof(footers) / sizeof(footers[0])))

lv_obj_t *main_screen = NULL;

static struct widget menu_widget;
static struct widget freq_widget;
static struct widget placed[PLACES];
static struct widget band_widget[BANDS];
static struct widget footer_widget[FOOTER_TILES];
static struct widget list_widget[LIST_ROWS];
static lv_obj_t *list_title_box;
static lv_obj_t *list_title;

//the controls in the list, the row at the top of what is in view, and the
//row that is marked
static struct panel_control *listed[CONTROLS_MAX];
static int listed_count;
static int list_top;
static int list_mark;

static enum selection selected = ON_FREQUENCY;
static struct panel_control *selected_control;
static bool is_changing_the_marked_row;

static int dial_hz = STARTING_HZ;
static int vfo_dial_hz[VFOS];
static int vfo_in_use = VFO_A;
static bool is_transmitting;
static unsigned long freq_last_user_change;

static char note_text[SHOWN_MAX];
static bool note_is_large;

/* tuning, as the desktop's window does it */

static bool is_on(const char *name){
	return controls_is(name, "ON");
}

static int own_value(const char *name){
	struct panel_control *c = controls_find(name);

	if (c == NULL)
		return 0;
	return (int)c->value;
}

//where the receiver listens: the dial, moved by RIT_DELTA while RIT is on
static int receiver_hz(){
	if (is_on("RIT"))
		return dial_hz + own_value("RIT_DELTA");
	return dial_hz;
}

//where the transmitter is to be: the dial, or while SPLIT is on the other
//VFO's dial, if that VFO has been used
static int transmitter_hz(){
	int other = VFO_B;

	if (!is_on("SPLIT"))
		return dial_hz;
	if (vfo_in_use == VFO_B)
		other = VFO_A;
	if (vfo_dial_hz[other] == NOT_USED_YET)
		return dial_hz;
	return vfo_dial_hz[other];
}

//STEP in Hz: 10H is 10, 1K is 1000
static int step_hz(){
	struct panel_control *c = controls_find("STEP");
	char text[CONTROL_TEXT_MAX];
	int hz;

	if (c == NULL)
		return HZ_PER_KHZ;
	controls_value_text(c, text, sizeof(text));
	hz = atoi(text);
	if (strchr(text, 'K') != NULL)
		hz *= HZ_PER_KHZ;
	if (hz <= 0)
		hz = HZ_PER_KHZ;
	return hz;
}

static int clamp(int value, int lowest, int highest){
	if (value < lowest)
		return lowest;
	if (value > highest)
		return highest;
	return value;
}

//the radio is sent where it is to be now: where the receiver listens, or
//while it transmits, where the transmitter is to be
static void send_frequency(){
	struct panel_control *c = controls_find("FREQ");
	int hz = receiver_hz();

	if (is_transmitting)
		hz = transmitter_hz();
	if (c != NULL && c->value == hz)
		return;
	if (c != NULL)
		c->value = hz;
	freq_last_user_change = millis();
	Serial.printf("FREQ %d\n", hz);
	i2c_link_changed(LINK_FREQ, hz);
}

static void tune(int steps){
	struct panel_control *delta = controls_find("RIT_DELTA");
	int step = step_hz();

	if (is_on("RIT") && delta != NULL)
		delta->value = clamp((int)delta->value + steps * step, -RIT_MAX_HZ, RIT_MAX_HZ);
	else
		dial_hz = clamp((dial_hz / step + steps) * step, LOWEST_HZ, HIGHEST_HZ);
	send_frequency();
}

//a VFO chosen: the dial of the one left is kept, and the other's taken up
static void choose_vfo(int vfo){
	if (vfo == vfo_in_use)
		return;
	vfo_dial_hz[vfo_in_use] = dial_hz;
	if (vfo_dial_hz[vfo] == NOT_USED_YET)
		vfo_dial_hz[vfo] = dial_hz;
	vfo_in_use = vfo;
	dial_hz = vfo_dial_hz[vfo];
}

//one of the panel's own controls has changed
static void apply_own(struct panel_control *c){
	struct panel_control *vfo = controls_find("VFO");

	if (!strcmp(c->d.name, "SPLIT") && is_on("SPLIT") && vfo != NULL)
		vfo->value = VFO_A;
	if (vfo != NULL)
		choose_vfo((int)vfo->value);
	send_frequency();
}

//the radio tells where it is; the dial is that, less RIT_DELTA while RIT
//is on. While the panel has it transmitting somewhere else than the dial,
//what it tells is the transmitter's frequency and the dial is left
static void take_radio_frequency(int hz){
	if (is_transmitting && transmitter_hz() != receiver_hz())
		return;
	dial_hz = hz;
	if (is_on("RIT"))
		dial_hz -= own_value("RIT_DELTA");
}

/* what is where */

//the group of the modem that has the mode in use
static void mode_group(char *group, int room){
	struct panel_control *mode = controls_find("MODE");

	group[0] = 0;
	if (mode == NULL)
		return;
	controls_value_text(mode, group, room);
	if (!strcmp(group, "CW") || !strcmp(group, "CWR"))
		snprintf(group, room, "CW");
	if (!strcmp(group, "USB") || !strcmp(group, "LSB") || !strcmp(group, "AM"))
		snprintf(group, room, "VOICE");
}

static bool has_a_place(const struct panel_control *c){
	int i;

	if (c == freq_widget.control)
		return true;
	for (i = 0; i < PLACES; i++){
		if (placed[i].control == c)
			return true;
	}
	for (i = 0; i < FOOTER_TILES; i++){
		if (footer_widget[i].control == c)
			return true;
	}
	return false;
}

//gives each place its control, and fills the bottom row and the list for
//the mode in use. It is done again whenever the radio describes its
//controls or the mode changes
static void lay_out(){
	char group[CONTROL_TEXT_MAX];
	const struct footer *footer = NULL;
	struct panel_control *c;
	int i;

	mode_group(group, sizeof(group));
	freq_widget.control = controls_find("FREQ");
	for (i = 0; i < PLACES; i++)
		placed[i].control = controls_find(places[i].control);
	for (i = 0; i < FOOTERS; i++){
		if (!strcmp(footers[i].group, group))
			footer = &footers[i];
	}
	for (i = 0; i < FOOTER_TILES; i++){
		footer_widget[i].control = NULL;
		if (footer != NULL && footer->controls[i] != NULL)
			footer_widget[i].control = controls_find(footer->controls[i]);
	}

	listed_count = 0;
	for (i = 0; i < OWN_LISTED; i++){
		c = controls_find(own_listed[i]);
		if (c != NULL)
			listed[listed_count++] = c;
	}
	for (i = 0; i < controls_count(); i++){
		c = controls_at(i);
		if (c->d.number == NOT_ON_THE_LINK || has_a_place(c))
			continue;
		if (strcmp(c->d.group, "RADIO") && strcmp(c->d.group, group))
			continue;
		listed[listed_count++] = c;
	}

	if (selected == ON_A_CONTROL && !has_a_place(selected_control))
		selected = ON_FREQUENCY;
	if (list_mark > listed_count - 1)
		list_mark = listed_count - 1;
	if (list_mark < 0)
		list_mark = 0;
	if (list_top > listed_count - LIST_ROWS)
		list_top = listed_count - LIST_ROWS;
	if (list_top < 0)
		list_top = 0;
}

//the list is moved to bring the marked row into view
static void keep_the_mark_in_view(){
	if (list_mark < list_top)
		list_top = list_mark;
	if (list_mark >= list_top + LIST_ROWS)
		list_top = list_mark - LIST_ROWS + 1;
}

/* showing it */

static void format_frequency(char *text, int room, int hz){
	snprintf(text, room, "%d.%03d.%03d", hz / HZ_PER_MHZ,
		(hz % HZ_PER_MHZ) / HZ_PER_KHZ, hz % HZ_PER_KHZ);
}

//BW_CW and BW_VOICE are both the bandwidth of the mode they are shown in,
//and a name that begins with its group, CW_DELAY, is shown without it
static const char *label_of(const struct panel_control *c){
	size_t group = strlen(c->d.group);

	if (!strncmp(c->d.name, "BW_", 3))
		return "BW";
	if (group > 0 && !strncmp(c->d.name, c->d.group, group)
			&& c->d.name[group] == '_')
		return c->d.name + group + 1;
	return c->d.name;
}

static void show_edge(struct widget *w, bool has_focus, bool is_taken_up){
	uint32_t edge = COLOUR_WITHOUT;
	uint32_t fill = COLOUR_BOX;
	int width = BORDER_WITHOUT;

	if (has_focus){
		edge = COLOUR_WITH_FOCUS;
		width = BORDER_WITH_FOCUS;
	}
	if (is_taken_up)
		fill = COLOUR_TAKEN_UP;
	lv_obj_set_style_border_color(w->box, lv_color_hex(edge), 0);
	lv_obj_set_style_border_width(w->box, width, 0);
	lv_obj_set_style_bg_color(w->box, lv_color_hex(fill), 0);
}

static void show_widget(struct widget *w, const char *label, bool has_edge,
		bool is_filled){
	char text[SHOWN_MAX];

	if (w->control == NULL){
		lv_obj_add_flag(w->box, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_remove_flag(w->box, LV_OBJ_FLAG_HIDDEN);
	controls_value_text(w->control, text, sizeof(text));
	lv_label_set_text(w->title, label);
	lv_label_set_text(w->value, text);
	show_edge(w, has_edge, is_filled);
}

//a control in the top rows or the bottom row: selected, the knob changes it
static void show_tile(struct widget *w, const char *label){
	bool is_selected = selected == ON_A_CONTROL && w->control == selected_control;

	show_widget(w, label, is_selected, is_selected);
}

static void show_frequency(){
	char text[SHOWN_MAX];
	char title[SHOWN_MAX];

	snprintf(title, sizeof(title), "FREQ");
	if (transmitter_hz() != receiver_hz()){
		format_frequency(text, sizeof(text), transmitter_hz());
		snprintf(title, sizeof(title), "TX %s", text);
	}
	if (note_text[0] != 0 && !note_is_large)
		snprintf(title, sizeof(title), "%s", note_text);
	format_frequency(text, sizeof(text), receiver_hz());
	if (note_is_large)
		snprintf(text, sizeof(text), "%s", note_text);
	lv_label_set_text(freq_widget.title, title);
	lv_label_set_text(freq_widget.value, text);
	show_edge(&freq_widget, selected == ON_FREQUENCY, false);
}

static void show_list(){
	char group[CONTROL_TEXT_MAX];
	struct widget title;
	bool is_scrolling = selected == ON_THE_LIST && !is_changing_the_marked_row;
	bool is_marked;
	int i;

	for (i = 0; i < LIST_ROWS; i++){
		list_widget[i].control = NULL;
		if (list_top + i < listed_count)
			list_widget[i].control = listed[list_top + i];
		if (list_widget[i].control == NULL){
			show_widget(&list_widget[i], "", false, false);
			continue;
		}
		is_marked = selected == ON_THE_LIST && list_top + i == list_mark;
		show_widget(&list_widget[i], label_of(list_widget[i].control), is_marked,
			is_marked && is_changing_the_marked_row);
	}
	mode_group(group, sizeof(group));
	lv_label_set_text(list_title, group);
	title.box = list_title_box;
	show_edge(&title, is_scrolling, is_scrolling);
}

//a band's button has the bright edge while the radio is on that band
static void show_bands(){
	int i;

	for (i = 0; i < BANDS; i++)
		show_edge(&band_widget[i], controls_is("BAND", band_names[i]), false);
}

static void show(){
	int i;

	show_frequency();
	show_bands();
	for (i = 0; i < PLACES; i++)
		show_tile(&placed[i], places[i].label);
	for (i = 0; i < FOOTER_TILES; i++){
		if (footer_widget[i].control != NULL)
			show_tile(&footer_widget[i], label_of(footer_widget[i].control));
		else
			show_tile(&footer_widget[i], "");
	}
	show_list();
}

/* changing a control */

//a number moves by its step, onto a multiple of it; a choice to the next
//or the one before, and no further than the first or the last
static void change(struct panel_control *c, int steps){
	int32_t step = c->d.step;
	int32_t v = c->value;
	char text[SHOWN_MAX];

	if (c->d.kind == LINK_KIND_CHOICE)
		v = clamp(v + steps, 0, controls_choices(c) - 1);
	else {
		if (step <= 0)
			step = 1;
		v = clamp((v / step + steps) * step, c->d.min, c->d.max);
	}
	if (v == c->value)
		return;
	c->value = v;
	c->last_user_change = millis();
	controls_value_text(c, text, sizeof(text));
	Serial.printf("%s %s\n", c->d.name, text);
	if (c->d.number == NOT_ON_THE_LINK)
		apply_own(c);
	else
		i2c_link_changed(c->d.number, v);
	if (!strcmp(c->d.name, "MODE"))
		lay_out();
}

//a choice moves on to its next choice, and round from the last to the first
static void change_to_the_next_choice(struct panel_control *c){
	int choices = controls_choices(c);

	if (choices < 2)
		return;
	if (c->value >= choices - 1)
		change(c, -(choices - 1));
	else
		change(c, 1);
}

//a choice is put at the one named, if it has one of that name
static void change_to_the_choice(struct panel_control *c, const char *choice){
	struct panel_control tried = *c;
	char text[CONTROL_TEXT_MAX];
	int i;

	for (i = 0; i < controls_choices(c); i++){
		tried.value = i;
		controls_value_text(&tried, text, sizeof(text));
		if (!strcmp(text, choice))
			change(c, i - (int)c->value);
	}
}

//a band's button: the radio goes to that band, and what is selected stays
static void on_band_touched(lv_event_t *e){
	const char *band = (const char *)lv_event_get_user_data(e);
	struct panel_control *c = controls_find("BAND");

	if (c != NULL)
		change_to_the_choice(c, band);
	show();
}

static void select_the_frequency(){
	selected = ON_FREQUENCY;
	is_changing_the_marked_row = false;
}

//the list is selected for scrolling, with the mark on a row if one is given
static void select_the_list(int row){
	selected = ON_THE_LIST;
	is_changing_the_marked_row = false;
	if (row != NOWHERE && row < listed_count)
		list_mark = row;
	keep_the_mark_in_view();
}

static void select_a_control(struct panel_control *c){
	if (selected == ON_A_CONTROL && selected_control == c)
		change_to_the_next_choice(c);
	selected = ON_A_CONTROL;
	selected_control = c;
	is_changing_the_marked_row = false;
}

static void on_touched(lv_event_t *e){
	struct widget *w = (struct widget *)lv_event_get_user_data(e);
	int i;

	if (w == &freq_widget)
		select_the_frequency();
	else if (w == NULL)
		select_the_list(NOWHERE);
	else if (w->control != NULL){
		for (i = 0; i < LIST_ROWS; i++){
			if (w == &list_widget[i])
				break;
		}
		if (i < LIST_ROWS)
			select_the_list(list_top + i);
		else
			select_a_control(w->control);
	}
	show();
}

/* making the screen */

static lv_obj_t *box_create(int x, int y, int w, int h){
	lv_obj_t *box = lv_obj_create(main_screen);

	lv_obj_set_size(box, w, h);
	lv_obj_align(box, LV_ALIGN_TOP_LEFT, x, y);
	lv_obj_set_style_radius(box, 0, 0);
	lv_obj_set_style_pad_all(box, 0, 0);
	lv_obj_set_style_bg_color(box, lv_color_hex(COLOUR_BOX), 0);
	lv_obj_set_style_border_color(box, lv_color_hex(COLOUR_WITHOUT), 0);
	lv_obj_set_style_border_width(box, BORDER_WITHOUT, 0);
	lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
	return box;
}

static lv_obj_t *text_create(lv_obj_t *box, const lv_font_t *font, uint32_t colour){
	lv_obj_t *text = lv_label_create(box);

	lv_label_set_text(text, "");
	lv_obj_set_style_text_color(text, lv_color_hex(colour), 0);
	lv_obj_set_style_text_font(text, font, 0);
	return text;
}

//a tile: the control's name small at the top, its value below
static void tile_create(struct widget *w, int x, int y, int width,
		const lv_font_t *value_font){
	w->box = box_create(x, y, width, ROW_HEIGHT);
	lv_obj_add_event_cb(w->box, on_touched, LV_EVENT_CLICKED, w);
	w->title = text_create(w->box, &lv_font_montserrat_12, COLOUR_TITLE);
	lv_obj_align(w->title, LV_ALIGN_TOP_LEFT, TITLE_LEFT, TITLE_TOP);
	w->value = text_create(w->box, value_font, COLOUR_VALUE);
	lv_obj_align(w->value, LV_ALIGN_BOTTOM_MID, 0, -VALUE_UP);
}

//a row of the list: the control's name on the left, its value on the right
static void row_create(struct widget *w, int x, int y, int width){
	w->box = box_create(x, y, width, ROW_HEIGHT);
	lv_obj_add_event_cb(w->box, on_touched, LV_EVENT_CLICKED, w);
	w->title = text_create(w->box, &lv_font_montserrat_14, COLOUR_TITLE);
	lv_obj_align(w->title, LV_ALIGN_LEFT_MID, ROW_PAD, 0);
	w->value = text_create(w->box, &lv_font_montserrat_16, COLOUR_VALUE);
	lv_obj_align(w->value, LV_ALIGN_RIGHT_MID, -ROW_PAD, 0);
}

static void screen_create(){
	int i;

	lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(COLOUR_SCREEN), 0);
	main_screen = lv_obj_create(lv_screen_active());
	lv_obj_set_size(main_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
	lv_obj_align(main_screen, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_set_style_bg_color(main_screen, lv_color_hex(COLOUR_SCREEN), 0);
	lv_obj_set_style_border_width(main_screen, 0, 0);
	lv_obj_set_style_radius(main_screen, 0, 0);
	lv_obj_set_style_pad_all(main_screen, 0, 0);
	lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

	//MENU will open the other screens; there are none yet, so it is dim
	menu_widget.box = box_create(MENU_LEFT, 0, TILE_WIDTH, ROW_HEIGHT);
	menu_widget.value = text_create(menu_widget.box, &lv_font_montserrat_12, COLOUR_DIM);
	lv_label_set_text(menu_widget.value, "MENU");
	lv_obj_align(menu_widget.value, LV_ALIGN_CENTER, 0, 0);

	tile_create(&freq_widget, FREQ_LEFT, 0, FREQ_WIDTH, &lv_font_montserrat_24);
	for (i = 0; i < PLACES; i++)
		tile_create(&placed[i], places[i].x, places[i].y, places[i].w,
			&lv_font_montserrat_16);
	for (i = 0; i < BANDS; i++){
		band_widget[i].box = box_create(i * TILE_WIDTH, ROW_HEIGHT, TILE_WIDTH, ROW_HEIGHT);
		lv_obj_add_event_cb(band_widget[i].box, on_band_touched, LV_EVENT_CLICKED,
			(void *)band_names[i]);
		band_widget[i].value = text_create(band_widget[i].box,
			&lv_font_montserrat_16, COLOUR_VALUE);
		lv_label_set_text(band_widget[i].value, band_names[i]);
		lv_obj_align(band_widget[i].value, LV_ALIGN_CENTER, 0, 0);
	}
	for (i = 0; i < FOOTER_TILES; i++)
		tile_create(&footer_widget[i], i * TILE_WIDTH, FOOTER_TOP, TILE_WIDTH,
			&lv_font_montserrat_16);

	list_title_box = box_create(LIST_LEFT, LIST_TOP, LIST_WIDTH, LIST_TITLE_HEIGHT);
	lv_obj_add_event_cb(list_title_box, on_touched, LV_EVENT_CLICKED, NULL);
	list_title = text_create(list_title_box, &lv_font_montserrat_14, COLOUR_TITLE);
	lv_obj_align(list_title, LV_ALIGN_CENTER, 0, 0);
	for (i = 0; i < LIST_ROWS; i++)
		row_create(&list_widget[i], LIST_LEFT,
			LIST_TOP + LIST_TITLE_HEIGHT + i * ROW_HEIGHT, LIST_WIDTH);
}

/* what the rest of the panel calls */

//the radio has described its controls afresh
void main_controls_described(){
	lay_out();
	show();
}

void main_radio_says(int number, int32_t value){
	struct panel_control *c = controls_of_number(number);
	bool was_transmitting = is_transmitting;

	if (c == NULL)
		return;
	if (number == LINK_FREQ){
		if (freq_last_user_change != 0
				&& millis() - freq_last_user_change < OPERATOR_WINS_FOR_MS)
			return;
		c->value = value;
		take_radio_frequency((int)value);
		show();
		return;
	}
	if (c->last_user_change != 0
			&& millis() - c->last_user_change < OPERATOR_WINS_FOR_MS)
		return;
	c->value = value;
	if (!strcmp(c->d.name, "MODE"))
		lay_out();
	if (!strcmp(c->d.name, "TX")){
		is_transmitting = controls_is("TX", "ON");
		if (is_transmitting != was_transmitting)
			send_frequency();
	}
	show();
}

//a few words in the corner of the frequency's box, for whatever is being tried
void main_note(const char *text){
	note_is_large = false;
	snprintf(note_text, sizeof(note_text), "%s", text);
	show();
}

//a few words in place of the frequency, as large as the frequency, for a test
//that is to be followed by eye while it is listened to. An empty note brings
//the frequency back
void main_note_large(const char *text){
	note_is_large = text[0] != 0;
	snprintf(note_text, sizeof(note_text), "%s", text);
	show();
}

//the knob goes to whatever is selected
void main_knob_turned(int steps){
	if (selected == ON_FREQUENCY)
		tune(steps);
	if (selected == ON_A_CONTROL && selected_control != NULL)
		change(selected_control, steps);
	if (selected == ON_THE_LIST && listed_count > 0){
		if (is_changing_the_marked_row)
			change(listed[list_mark], steps);
		else {
			list_mark = clamp(list_mark + steps, 0, listed_count - 1);
			keep_the_mark_in_view();
		}
	}
	show();
}

void main_knob_clicked(){
	if (selected == ON_A_CONTROL && selected_control != NULL)
		change_to_the_next_choice(selected_control);
	if (selected == ON_THE_LIST && listed_count > 0)
		is_changing_the_marked_row = !is_changing_the_marked_row;
	show();
}

void main_knob_held(){
	select_the_frequency();
	show();
}

//what a touch would select, by name, for trying the screen from the serial
//port: FREQ, LIST, or a control's name
void main_select(const char *name){
	struct panel_control *c = controls_find(name);
	int i;

	if (!strcmp(name, "FREQ"))
		select_the_frequency();
	else if (!strcmp(name, "LIST"))
		select_the_list(NOWHERE);
	else if (c != NULL && has_a_place(c))
		select_a_control(c);
	for (i = 0; i < listed_count; i++){
		if (c != NULL && listed[i] == c)
			select_the_list(i);
	}
	show();
}

void main_ui(){
	if (!main_screen){
		Serial.println("Creating the main screen");
		controls_add_own("STEP", "10H/100H/500H/1K/10K", 0, 0, 1);
		controls_add_own("RIT", "OFF/ON", 0, 0, 0);
		controls_add_own("RIT_DELTA", NULL, -RIT_MAX_HZ, RIT_MAX_HZ, 0);
		controls_add_own("SPLIT", "OFF/ON", 0, 0, 0);
		controls_add_own("VFO", "A/B", 0, 0, VFO_A);
		screen_create();
		lay_out();
		show();
	}
	lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}
