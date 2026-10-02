// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// main.cpp — the main screen.
//
// WHERE EVERYTHING IS
//
// The author laid the screen out, and the places are exact to the pixel.
// Every widget is 48 high, as on the zbitx's shipping panel, and all but
// the frequency and the list's rows are 48 wide. The screen is 480 by 320:
//
//   y 0     STEP | FREQ 144 | RIT, SPLIT, DRIVE, AGC, IF, VOL
//   y 48    a button for each band, 10M to 80M, and MODE
//   y 96    a column of buttons on the left, 48 wide: LOG, SET, TERM
//           beside it, 288 wide: the graph, 64 rows, and the waterfall,
//           96; waterfall.cpp paints them. The span is shown in their top
//           left corner. Under them a line of text, 16 high: the status
//           bar, for what the radio has to say, and for what CW is read
//           when there is a decoder
//           on the right, 144 wide: the list, three rows of 48
//   y 272   six buttons under the waterfall, the mode's own: its macros in
//           CW; and MENU at the right end
//
// The top row is the frequency and the radio's core controls. Whatever
// else the radio has, that belongs to the radio as a whole or to the mode
// in use, is in the list: in CW that is WPM, PITCH and the rest. The
// waterfall is not made deeper to fill the screen: waterfall.cpp says why.
//
// In CW the bottom row's buttons are the first six of the radio's macros,
// each with its macro's label, and a touch sends that macro; modem_cw.c
// says where the macros come from.
//
// In USB, LSB and AM the bottom row has MIC, and two buttons, TX and RX:
// a touch on TX starts transmitting and a touch on RX stops it. The
// author wants the two apart, a button to start a voice transmission and
// another to end it, and not one button that does either by turns. TX is
// red while the radio transmits, and RX has the bright edge while it
// receives. They are worked by touch alone: the knob never selects them,
// so a click of the knob cannot key the transmitter. 2TONE has the same
// two buttons.
//
// SET opens the settings and LOG the logbook, which are in screens.cpp.
// TERM and MENU are for the keyboard and whatever else; neither is written
// yet, so those buttons are shown dim and do nothing.
//
// The frequency is shown with the VFO in use before it, A: or B:, and a
// click of the knob while the frequency is selected goes to the other VFO.
//
// RIT and SPLIT are red while they are on. The author's reason: they are
// switched on and then forgotten.
//
// A band's button goes to that band at a touch. A touch on the button of
// the band already in use goes to the next place in that band's stack,
// and from the last to the first; the radio keeps the stack, radio.c says
// how.
//
// A touch on the graph or the waterfall goes to the next span, and from
// the narrowest back to the widest. The radio makes the line of the
// spectrum for the span it is told, tools/sdr.c says how.
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
//   The frequency   turning tunes, and a click goes to the other VFO. The
//                   panel starts here, and a long press of the knob comes
//                   back here from anywhere.
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
//                   row.
//
// What is selected has a bright edge. A control whose value the knob is
// changing is filled as well. In the list the marked row has the bright
// edge while the knob scrolls, and is filled while the knob changes it.
//
// TUNING
//
// The tuning is the panel's own, as it is each client's: the zbitx's knob
// is on the panel. What is to be there when the panel is next switched on
// the radio keeps for it, as controls of the radio's that the radio does
// nothing with: STEP, the VFO in use, and each VFO's dial. RIT and SPLIT
// start afresh each time, as it would not do to come up with RIT on. It is the desktop window's, which was zbitxv2's. The
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
#include "waterfall.h"
#include "ui.h"

#define ROW_HEIGHT 48
#define TILE_WIDTH 48
#define FREQ_LEFT 48
#define FREQ_WIDTH 144
#define MENU_LEFT 432
#define COLUMN_BUTTONS 3
#define MACROS 6
#define STATUS_HEIGHT 16
#define STATUS_SHOWN_MAX 44
#define BANDS 9
#define FOOTER_TOP 272
#define FOOTER_TILES 6
#define LIST_LEFT 336
#define LIST_TOP 96
#define LIST_WIDTH 144
#define LIST_ROWS 3

#define TITLE_LEFT 3
#define TITLE_TOP 1
#define VALUE_UP 2
#define ROW_PAD 6
#define BORDER_WITH_FOCUS 2
#define BORDER_WITHOUT 1

#define COLOUR_SCREEN 0x000000
#define COLOUR_BOX 0x101820
#define COLOUR_TAKEN_UP 0x604800
#define COLOUR_SWITCHED_ON 0xB02020
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
	//how its edge and its fill were last drawn, so that they are drawn
	//again only when they change
	bool is_look_known;
	bool has_bright_edge;
	uint32_t fill;
};

//a control that has a place of its own in the top two rows
struct place {
	const char *control;
	const char *label;
	int16_t x;
	int16_t y;
	int16_t w;
};

//what is at a place on the bottom row: a control, shown with its value and
//changed by the knob once it is touched; or, where a choice is named, a
//button with a label of its own, that puts the control at that choice when
//it is touched, and is filled with its colour while the control is there
struct footer_tile {
	const char *control;
	const char *choice;
	const char *label;
	uint32_t fill_in_force;
};

//what a mode has on the bottom row
struct footer {
	const char *group;
	struct footer_tile tiles[FOOTER_TILES];
};

static const struct place places[] = {
	{"STEP", "STEP", 0, 0, TILE_WIDTH},
	{"RIT", "RIT", 192, 0, TILE_WIDTH},
	{"SPLIT", "SPLIT", 240, 0, TILE_WIDTH},
	{"DRIVE", "DRIVE", 288, 0, TILE_WIDTH},
	{"AGC", "AGC", 336, 0, TILE_WIDTH},
	{"IF", "IF", 384, 0, TILE_WIDTH},
	{"AUDIO", "VOL", 432, 0, TILE_WIDTH},
	{"MODE", "MODE", 432, ROW_HEIGHT, TILE_WIDTH},
};
#define PLACES ((int)(sizeof(places) / sizeof(places[0])))

//the second row's buttons, left to right, each a choice of BAND
static const char *const band_names[BANDS] = {
	"10M", "12M", "15M", "17M", "20M", "30M", "40M", "60M", "80M",
};

//the buttons down the left, and the macros' on the bottom row in CW
static const char *const column_names[COLUMN_BUTTONS] = { "LOG", "SET", "TERM" };
#define MACRO_GROUP "CW"
#define MACRO_CONTROL "MACRO"

static const struct footer footers[] = {
	{"VOICE", {
		{"MIC", NULL, NULL, COLOUR_BOX},
		{"TX", "ON", "TX", COLOUR_SWITCHED_ON},
		{"TX", "OFF", "RX", COLOUR_BOX},
	}},
	{"2TONE", {
		{"TX", "ON", "TX", COLOUR_SWITCHED_ON},
		{"TX", "OFF", "RX", COLOUR_BOX},
	}},
};
#define FOOTERS ((int)(sizeof(footers) / sizeof(footers[0])))

lv_obj_t *main_screen = NULL;

static struct widget macro_widget[MACROS];
static lv_obj_t *status_label;
static struct widget freq_widget;
static struct widget placed[PLACES];
static struct widget band_widget[BANDS];
static struct widget footer_widget[FOOTER_TILES];
static struct widget footer_button[FOOTER_TILES];
static const struct footer *footer_in_use;
static struct widget list_widget[LIST_ROWS];
static lv_obj_t *span_label;

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
static const char *const vfo_dial_names[VFOS] = { "VFO_A", "VFO_B" };
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

//the radio is told a number it keeps for the panel: which VFO is in use,
//and each VFO's dial, so that they are there when the panel is next
//switched on
static void keep_on_the_radio(const char *name, int32_t value){
	struct panel_control *c = controls_find(name);

	if (c == NULL || c->d.number == NOT_ON_THE_LINK || c->value == value)
		return;
	c->value = value;
	c->last_user_change = millis();
	i2c_link_changed(c->d.number, value);
}

//a VFO chosen: the dial of the one left is kept, and the other's taken up
static void choose_vfo(int vfo){
	if (vfo == vfo_in_use)
		return;
	vfo_dial_hz[vfo_in_use] = dial_hz;
	if (vfo_dial_hz[vfo] == NOT_USED_YET)
		vfo_dial_hz[vfo] = dial_hz;
	keep_on_the_radio(vfo_dial_names[vfo_in_use], dial_hz);
	vfo_in_use = vfo;
	dial_hz = vfo_dial_hz[vfo];
}

//one of the tuning's controls has changed: RIT, SPLIT or the VFO
static void apply_own(struct panel_control *c){
	struct panel_control *vfo = controls_find("VFO");

	if (!strcmp(c->d.name, "SPLIT") && is_on("SPLIT"))
		keep_on_the_radio("VFO", VFO_A);
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

//the button at a place on the bottom row, if the mode in use has one there
static const struct footer_tile *footer_button_at(int which){
	if (footer_in_use == NULL || footer_in_use->tiles[which].choice == NULL)
		return NULL;
	return &footer_in_use->tiles[which];
}

static bool has_a_place(const struct panel_control *c){
	int i;

	//the band has its row of buttons, and an action or a text is not for
	//the list
	if (c == freq_widget.control || !strcmp(c->d.name, "BAND")
			|| c->d.kind == LINK_KIND_ACTION || c->d.kind == LINK_KIND_TEXT)
		return true;
	for (i = 0; i < PLACES; i++){
		if (placed[i].control == c)
			return true;
	}
	for (i = 0; i < FOOTER_TILES; i++){
		if (footer_widget[i].control == c)
			return true;
		if (footer_button_at(i) != NULL && !strcmp(footer_button_at(i)->control, c->d.name))
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
	footer_in_use = footer;
	for (i = 0; i < FOOTER_TILES; i++){
		footer_widget[i].control = NULL;
		if (footer != NULL && footer->tiles[i].control != NULL
				&& footer->tiles[i].choice == NULL)
			footer_widget[i].control = controls_find(footer->tiles[i].control);
	}

	listed_count = 0;
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

//a label is given its text only if the text is new, and a widget is shown or
//hidden only if it was not already. LVGL takes every setting of a text, a
//colour or a flag as a change and paints that part of the screen again,
//and when more than 32 parts are to be painted it paints the whole screen.
//Painting the whole screen at every step of the knob blanked the waterfall
//and held the knob up, which the author saw as the tuning stalling
static void label_show(lv_obj_t *label, const char *text){
	if (strcmp(lv_label_get_text(label), text) != 0)
		lv_label_set_text(label, text);
}

static void set_hidden(lv_obj_t *box, bool is_hidden){
	if (lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN) == is_hidden)
		return;
	if (is_hidden)
		lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_remove_flag(box, LV_OBJ_FLAG_HIDDEN);
}

//a widget's edge is bright while it is selected, and it is filled with the
//colour given
static void show_look(struct widget *w, bool has_bright_edge, uint32_t fill){
	uint32_t edge = COLOUR_WITHOUT;
	int width = BORDER_WITHOUT;

	if (w->is_look_known && w->has_bright_edge == has_bright_edge && w->fill == fill)
		return;
	w->is_look_known = true;
	w->has_bright_edge = has_bright_edge;
	w->fill = fill;
	if (has_bright_edge){
		edge = COLOUR_WITH_FOCUS;
		width = BORDER_WITH_FOCUS;
	}
	lv_obj_set_style_border_color(w->box, lv_color_hex(edge), 0);
	lv_obj_set_style_border_width(w->box, width, 0);
	lv_obj_set_style_bg_color(w->box, lv_color_hex(fill), 0);
}

static void show_edge(struct widget *w, bool has_focus, bool is_taken_up){
	uint32_t fill = COLOUR_BOX;

	if (is_taken_up)
		fill = COLOUR_TAKEN_UP;
	show_look(w, has_focus, fill);
}

//RIT and SPLIT are red while they are on
static bool is_switched_on(const struct panel_control *c){
	if (c == NULL || (strcmp(c->d.name, "RIT") && strcmp(c->d.name, "SPLIT")))
		return false;
	return controls_is(c->d.name, "ON");
}

static void show_widget(struct widget *w, const char *label, bool has_edge,
		bool is_filled){
	char text[SHOWN_MAX];

	if (w->control == NULL){
		set_hidden(w->box, true);
		return;
	}
	set_hidden(w->box, false);
	controls_value_text(w->control, text, sizeof(text));
	label_show(w->title, label);
	label_show(w->value, text);
	if (is_switched_on(w->control))
		show_look(w, has_edge, COLOUR_SWITCHED_ON);
	else
		show_edge(w, has_edge, is_filled);
}

//a control in the top rows or the bottom row: selected, the knob changes it
static void show_tile(struct widget *w, const char *label){
	bool is_selected = selected == ON_A_CONTROL && w->control == selected_control;

	show_widget(w, label, is_selected, is_selected);
}

static void show_frequency(){
	char text[SHOWN_MAX];
	char dial[SHOWN_MAX];
	char title[SHOWN_MAX];

	snprintf(title, sizeof(title), "FREQ");
	if (transmitter_hz() != receiver_hz()){
		format_frequency(text, sizeof(text), transmitter_hz());
		snprintf(title, sizeof(title), "TX %s", text);
	}
	if (note_text[0] != 0 && !note_is_large)
		snprintf(title, sizeof(title), "%s", note_text);
	format_frequency(dial, sizeof(dial), receiver_hz());
	snprintf(text, sizeof(text), "%c:%s", vfo_in_use == VFO_B ? 'B' : 'A', dial);
	if (note_is_large)
		snprintf(text, sizeof(text), "%s", note_text);
	label_show(freq_widget.title, title);
	label_show(freq_widget.value, text);
	show_edge(&freq_widget, selected == ON_FREQUENCY, false);
}

static void show_list(){
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
}

static void show_span(){
	struct panel_control *span = controls_find("SPAN");
	char text[CONTROL_TEXT_MAX];

	text[0] = 0;
	if (span != NULL)
		controls_value_text(span, text, sizeof(text));
	label_show(span_label, text);
}

//a band's button has the bright edge while the radio is on that band
static void show_bands(){
	int i;

	for (i = 0; i < BANDS; i++)
		show_edge(&band_widget[i], controls_is("BAND", band_names[i]), false);
}

//the macros' buttons are there in CW only, as many as the radio has macros
//for and the row has room for, each with its macro's label
static void show_macros(){
	struct panel_control *macro = controls_find(MACRO_CONTROL);
	struct panel_control one;
	char group[CONTROL_TEXT_MAX];
	char label[SHOWN_MAX];
	int i;

	mode_group(group, sizeof(group));
	for (i = 0; i < MACROS; i++){
		if (macro == NULL || strcmp(group, MACRO_GROUP) || i >= controls_choices(macro)){
			set_hidden(macro_widget[i].box, true);
			continue;
		}
		one = *macro;
		one.value = i;
		controls_value_text(&one, label, sizeof(label));
		label_show(macro_widget[i].value, label);
		set_hidden(macro_widget[i].box, false);
	}
}

//the bottom row's buttons of the mode in use: each has the bright edge and
//its own colour while its control is at its choice, so that TX is red while
//the radio transmits and RX is marked while it receives
static void show_footer_buttons(){
	const struct footer_tile *tile;
	bool is_in_force;
	uint32_t fill;
	int i;

	for (i = 0; i < FOOTER_TILES; i++){
		tile = footer_button_at(i);
		if (tile == NULL || controls_find(tile->control) == NULL){
			set_hidden(footer_button[i].box, true);
			continue;
		}
		is_in_force = controls_is(tile->control, tile->choice);
		fill = COLOUR_BOX;
		if (is_in_force)
			fill = tile->fill_in_force;
		label_show(footer_button[i].value, tile->label);
		show_look(&footer_button[i], is_in_force, fill);
		set_hidden(footer_button[i].box, false);
	}
}

static void show(){
	int i;

	show_frequency();
	show_bands();
	show_span();
	for (i = 0; i < PLACES; i++)
		show_tile(&placed[i], places[i].label);
	for (i = 0; i < FOOTER_TILES; i++){
		if (footer_widget[i].control != NULL)
			show_tile(&footer_widget[i], label_of(footer_widget[i].control));
		else
			show_tile(&footer_widget[i], "");
	}
	show_footer_buttons();
	show_macros();
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
	if (c->d.number != NOT_ON_THE_LINK)
		i2c_link_changed(c->d.number, v);
	if (c->d.number == NOT_ON_THE_LINK || !strcmp(c->d.name, "VFO"))
		apply_own(c);
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

//a number moves on by one step, and round from its greatest to its least
static void change_to_the_next_number(struct panel_control *c){
	if (c->value >= c->d.max)
		change(c, -(int)((c->d.max - c->d.min) / (c->d.step > 0 ? c->d.step : 1)));
	else
		change(c, 1);
}

//a band's button: the radio goes to that band, or if it is on that band
//already, to the next place in the band's stack. What is selected stays
static void band_touched(const char *band){
	struct panel_control *c = controls_find("BAND");
	struct panel_control *stack = controls_find("STACK");

	if (c != NULL && !controls_is("BAND", band))
		change_to_the_choice(c, band);
	else if (c != NULL && stack != NULL)
		change_to_the_next_number(stack);
	show();
}

//a button of the bottom row: its control is put at its choice. Touched
//again while the control is there already, it does nothing
static void footer_button_touched(int which){
	const struct footer_tile *tile = footer_button_at(which);
	struct panel_control *c;

	if (tile == NULL)
		return;
	c = controls_find(tile->control);
	if (c != NULL)
		change_to_the_choice(c, tile->choice);
	show();
}

static void on_footer_button_touched(lv_event_t *e){
	footer_button_touched((int)(intptr_t)lv_event_get_user_data(e));
}

//the place of the bottom row's button that has this label, for the serial
//port's stand-in for a touch
static int footer_button_labelled(const char *label){
	int i;

	for (i = 0; i < FOOTER_TILES; i++){
		if (footer_button_at(i) != NULL && !strcmp(footer_button_at(i)->label, label))
			return i;
	}
	return NOWHERE;
}

static void on_band_touched(lv_event_t *e){
	band_touched((const char *)lv_event_get_user_data(e));
}

//a macro's button: the radio is told to send that macro. It is told each
//time the button is touched, since a macro is something done, not set
static void macro_touched(int which){
	struct panel_control *macro = controls_find(MACRO_CONTROL);

	if (macro == NULL || which >= controls_choices(macro))
		return;
	Serial.printf("%s %d\n", MACRO_CONTROL, which);
	i2c_link_changed(macro->d.number, which);
}

static void on_macro_touched(lv_event_t *e){
	macro_touched((int)(intptr_t)lv_event_get_user_data(e));
}

//a touch on the graph or the waterfall: the next span
static void on_spectrum_touched(lv_event_t *e){
	struct panel_control *span = controls_find("SPAN");

	(void)e;
	if (span != NULL)
		change_to_the_next_choice(span);
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

//a button for something that is not written yet
static lv_obj_t *dim_button_create(int x, int y, const char *label){
	lv_obj_t *box = box_create(x, y, TILE_WIDTH, ROW_HEIGHT);
	lv_obj_t *text = text_create(box, &lv_font_montserrat_12, COLOUR_DIM);

	lv_label_set_text(text, label);
	lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);
	return box;
}

static void on_set_touched(lv_event_t *e){
	(void)e;
	screens_open_settings();
}

static void on_log_touched(lv_event_t *e){
	(void)e;
	screens_open_log();
}

//a button that opens another screen: SET the settings, LOG the logbook
static void live_button_create(int x, int y, const char *label, lv_event_cb_t touched){
	lv_obj_t *box = box_create(x, y, TILE_WIDTH, ROW_HEIGHT);
	lv_obj_t *text = text_create(box, &lv_font_montserrat_14, COLOUR_VALUE);

	lv_label_set_text(text, label);
	lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(box, touched, LV_EVENT_CLICKED, NULL);
}

static void screen_create(){
	lv_obj_t *spectrum;
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

	//what these are for is not written yet, so they are dim and do nothing
	for (i = 0; i < COLUMN_BUTTONS; i++){
		if (!strcmp(column_names[i], "SET"))
			live_button_create(0, WATERFALL_TOP + i * ROW_HEIGHT, "SET", on_set_touched);
		else if (!strcmp(column_names[i], "LOG"))
			live_button_create(0, WATERFALL_TOP + i * ROW_HEIGHT, "LOG", on_log_touched);
		else
			dim_button_create(0, WATERFALL_TOP + i * ROW_HEIGHT, column_names[i]);
	}
	for (i = 0; i < MACROS; i++){
		macro_widget[i].box = box_create(WATERFALL_LEFT + i * TILE_WIDTH, FOOTER_TOP,
			TILE_WIDTH, ROW_HEIGHT);
		lv_obj_add_event_cb(macro_widget[i].box, on_macro_touched, LV_EVENT_CLICKED,
			(void *)(intptr_t)i);
		macro_widget[i].value = text_create(macro_widget[i].box,
			&lv_font_montserrat_14, COLOUR_VALUE);
		lv_obj_align(macro_widget[i].value, LV_ALIGN_CENTER, 0, 0);
	}
	dim_button_create(MENU_LEFT, FOOTER_TOP, "MENU");

	tile_create(&freq_widget, FREQ_LEFT, 0, FREQ_WIDTH, &lv_font_montserrat_20);
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
		tile_create(&footer_widget[i], WATERFALL_LEFT + i * TILE_WIDTH, FOOTER_TOP, TILE_WIDTH,
			&lv_font_montserrat_16);
	for (i = 0; i < FOOTER_TILES; i++){
		footer_button[i].box = box_create(WATERFALL_LEFT + i * TILE_WIDTH, FOOTER_TOP,
			TILE_WIDTH, ROW_HEIGHT);
		lv_obj_add_event_cb(footer_button[i].box, on_footer_button_touched,
			LV_EVENT_CLICKED, (void *)(intptr_t)i);
		footer_button[i].value = text_create(footer_button[i].box,
			&lv_font_montserrat_16, COLOUR_VALUE);
		lv_obj_align(footer_button[i].value, LV_ALIGN_CENTER, 0, 0);
		lv_obj_add_flag(footer_button[i].box, LV_OBJ_FLAG_HIDDEN);
	}

	for (i = 0; i < LIST_ROWS; i++)
		row_create(&list_widget[i], LIST_LEFT, LIST_TOP + i * ROW_HEIGHT, LIST_WIDTH);

	//nothing of LVGL's is drawn over the graph and the waterfall but the
	//span in their corner, which waterfall.cpp leaves unpainted; the rest
	//is only there to be touched
	spectrum = lv_obj_create(main_screen);
	lv_obj_remove_style_all(spectrum);
	lv_obj_set_size(spectrum, WATERFALL_WIDTH, WATERFALL_HEIGHT);
	lv_obj_align(spectrum, LV_ALIGN_TOP_LEFT, WATERFALL_LEFT, WATERFALL_TOP);
	lv_obj_add_event_cb(spectrum, on_spectrum_touched, LV_EVENT_CLICKED, NULL);
	span_label = text_create(spectrum, &lv_font_montserrat_12, COLOUR_TITLE);
	lv_obj_align(span_label, LV_ALIGN_TOP_LEFT, TITLE_LEFT, TITLE_TOP);

	//the status bar, between the waterfall and the bottom row
	status_label = text_create(main_screen, &lv_font_montserrat_12, COLOUR_VALUE);
	lv_obj_set_size(status_label, WATERFALL_WIDTH - 2 * TITLE_LEFT, STATUS_HEIGHT);
	lv_label_set_long_mode(status_label, LV_LABEL_LONG_CLIP);
	lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, WATERFALL_LEFT + TITLE_LEFT,
		WATERFALL_TOP + WATERFALL_HEIGHT);
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
	//what the radio kept for the panel: the VFO that was in use, whose
	//dial is the radio's frequency, and each VFO's dial
	if (!strcmp(c->d.name, "VFO"))
		vfo_in_use = (int)value;
	if (!strcmp(c->d.name, vfo_dial_names[VFO_A]))
		vfo_dial_hz[VFO_A] = (int)value;
	if (!strcmp(c->d.name, vfo_dial_names[VFO_B]))
		vfo_dial_hz[VFO_B] = (int)value;
	if (!strcmp(c->d.name, "TX")){
		is_transmitting = controls_is("TX", "ON");
		if (is_transmitting != was_transmitting)
			send_frequency();
	}
	show();
}

//a text control's value from the radio; the panel has it to show in the
//settings, and does not take it over what was typed here a moment ago
void main_radio_says_text(int number, const char *text){
	struct panel_control *c = controls_of_number(number);

	if (c == NULL || c->d.kind != LINK_KIND_TEXT)
		return;
	if (c->last_user_change != 0
			&& millis() - c->last_user_change < OPERATOR_WINS_FOR_MS)
		return;
	snprintf(c->text, sizeof(c->text), "%s", text);
	screens_radio_said();
}

void main_hide(){
	if (main_screen)
		lv_obj_add_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}

//a line of text in the status bar; of one that is too long, the end is
//shown, since what is newest is at the end
void main_status(const char *text){
	size_t length = strlen(text);

	if (length > STATUS_SHOWN_MAX)
		text += length - STATUS_SHOWN_MAX;
	lv_label_set_text(status_label, text);
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
	if (screens_is_open()){
		screens_knob_turned(steps);
		return;
	}
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
	struct panel_control *vfo = controls_find("VFO");

	if (screens_is_open()){
		screens_knob_clicked();
		return;
	}
	if (selected == ON_FREQUENCY && vfo != NULL)
		change_to_the_next_choice(vfo);
	if (selected == ON_A_CONTROL && selected_control != NULL)
		change_to_the_next_choice(selected_control);
	if (selected == ON_THE_LIST && listed_count > 0)
		is_changing_the_marked_row = !is_changing_the_marked_row;
	show();
}

void main_knob_held(){
	screens_close();
	select_the_frequency();
	show();
}

//what a touch would select, by name, for trying the screen from the serial
//port: FREQ, LIST, a button of the bottom row by its label, TX or RX, or a
//control's name
void main_select(const char *name){
	struct panel_control *c = controls_find(name);
	int button = footer_button_labelled(name);
	int i;

	for (i = 0; i < BANDS; i++){
		if (!strcmp(name, band_names[i]))
			band_touched(band_names[i]);
	}
	if (screens_is_open()){
		screens_select(name);
		return;
	}
	if (!strcmp(name, "SET"))
		screens_open_settings();
	else if (!strcmp(name, "LOG"))
		screens_open_log();
	else if (name[0] == 'F' && name[1] >= '1' && name[1] <= '0' + MACROS && name[2] == 0)
		macro_touched(name[1] - '1');
	else if (!strcmp(name, "FREQ"))
		select_the_frequency();
	else if (!strcmp(name, "LIST"))
		select_the_list(NOWHERE);
	else if (!strcmp(name, "SPECTRUM"))
		on_spectrum_touched(NULL);
	else if (button != NOWHERE)
		footer_button_touched(button);
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
		controls_add_own("RIT", "OFF/ON", 0, 0, 0);
		controls_add_own("RIT_DELTA", NULL, -RIT_MAX_HZ, RIT_MAX_HZ, 0);
		controls_add_own("SPLIT", "OFF/ON", 0, 0, 0);
		screen_create();
		lay_out();
		show();
	}
	lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}
