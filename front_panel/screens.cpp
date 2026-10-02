// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// screens.cpp — the screens other than the main one: the settings, and
// the one on which a line of text is typed.
//
// THE SETTINGS
//
// Every screen here has its way back at the top left, an arrow to the
// left, as a telephone has: from the settings to the main screen, from the
// WiFi list to the settings.
//
// SET on the main screen opens them. The author's plan for the screen: it
// is just a list of the housekeeping, one row to each, and a touch on a row
// opens what changes it.
//
//   Callsign    the station's callsign, the radio's control MYCALLSIGN. It
//               is typed on the text screen, with what it was to begin with.
//   Grid        the station's grid, MYGRID, the same way.
//   Date/Time   to set the radio's clock: not written yet, and shown dim.
//   WiFi        the access points the radio can hear, the strongest first,
//               no more than sixteen. Opening the screen asks the radio to
//               scan, which takes some seconds; SCAN asks again. A touch
//               on one chooses it: its key is typed on the text screen,
//               and the name and the key go to the radio, which joins it
//               and says on this screen's top line how it went. An open
//               one is joined with no key asked for.
//
// The radio keeps the callsign and the grid and remembers them; the panel
// only shows what the radio last said and sends what was typed.
//
// TYPING A LINE OF TEXT
//
// One screen serves wherever a line of text is wanted: a title, a prompt,
// a box of one line, and LVGL's keyboard. The author's rule is that the box
// is in the upper part of the screen, so that the keyboard, which takes
// the lower part, never covers it. SAVE, or the keyboard's own tick, gives
// the text to whoever asked for it; CANCEL, or the keyboard's cross, gives
// nothing. Either way the screen it was opened from comes back.
//
// THE KNOB
//
// On the settings and on the WiFi list one row is marked with a bright
// edge. Turning the knob moves the mark, the list moving to keep it in
// view, and a click chooses the marked row as a touch on it would. A long
// press goes back to the main screen from anywhere.
//
// WHILE ANOTHER SCREEN IS UP
//
// The main screen is hidden and the waterfall is not painted, since it is
// painted straight to the display and would paint over whatever is there.
// A long press of the knob closes whatever is open and goes back to the
// main screen.

#include <Arduino.h>
#include <lvgl.h>
#include <ctype.h>
#include "link.h"
#include "controls.h"
#include "waterfall.h"
#include "ui.h"

#define BAR_HEIGHT 48
#define ROW_HEIGHT 48
#define PAD 8
#define BACK_WIDTH 48
#define BUTTON_WIDTH 96
#define TITLE_LEFT (BACK_WIDTH + PAD)

#define INPUT_TITLE_TOP 4
#define INPUT_PROMPT_TOP 28
#define INPUT_BOX_TOP 52
#define INPUT_BOX_WIDTH 296
#define INPUT_BOX_HEIGHT 44
#define INPUT_BUTTON_WIDTH 76
#define INPUT_KEYBOARD_HEIGHT 212
#define INPUT_TEXT_MAX 64
#define EDGE_WITH_MARK 2
#define EDGE_WITHOUT 1

#define COLOUR_SCREEN 0x000000
#define COLOUR_BOX 0x101820
#define COLOUR_TITLE 0x90A0B0
#define COLOUR_VALUE 0xFFFFFF
#define COLOUR_DIM 0x506070
#define COLOUR_EDGE 0x405060
#define COLOUR_MARK 0xFFC000

#define WIFI_STATUS_LEFT (TITLE_LEFT + 64)
#define WIFI_SHOWN_MAX 48

#define CALLSIGN_MAX 12
#define GRID_MAX 6

typedef void (*text_done_fn)(const char *text);

//a row of the settings: what it is called, the radio's control it shows, if
//it has one, and what a touch on it does
struct setting {
	const char *label;
	const char *control;
	void (*open)();
	lv_obj_t *value;
	lv_obj_t *box;
};

static void callsign_open();
static void grid_open();
static void wifi_open();
static void settings_show_values();

static struct setting settings[] = {
	{"Callsign", "MYCALLSIGN", callsign_open, NULL, NULL},
	{"Grid", "MYGRID", grid_open, NULL, NULL},
	{"Date/Time", NULL, NULL, NULL, NULL},
	{"WiFi", NULL, wifi_open, NULL, NULL},
};
#define SETTINGS ((int)(sizeof(settings) / sizeof(settings[0])))

//a row of the WiFi screen
struct wifi_row {
	lv_obj_t *box;
	lv_obj_t *name;
	lv_obj_t *signal;
};

static lv_obj_t *wifi_screen;
static lv_obj_t *wifi_status;
static struct wifi_row wifi_rows[LINK_WIFI_POINTS_MAX];
static struct link_wifi_point wifi_points[LINK_WIFI_POINTS_MAX];
static int wifi_count;
static bool wifi_is_listed;
static bool wifi_is_to_show;
static char wifi_said[LINK_SAY_MAX + 1];

static lv_obj_t *settings_screen;
static int settings_mark;
static int wifi_mark;
static lv_obj_t *wifi_list;
static lv_obj_t *input_screen;
static lv_obj_t *input_title;
static lv_obj_t *input_prompt;
static lv_obj_t *input_box;
static lv_obj_t *input_keyboard;
static text_done_fn input_done;
static lv_obj_t *input_opened_from;
static bool input_is_capitals;

/* what both screens are made of */

static lv_obj_t *screen_create(){
	lv_obj_t *screen = lv_obj_create(lv_screen_active());

	lv_obj_set_size(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
	lv_obj_align(screen, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_set_style_bg_color(screen, lv_color_hex(COLOUR_SCREEN), 0);
	lv_obj_set_style_border_width(screen, 0, 0);
	lv_obj_set_style_radius(screen, 0, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);
	lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
	return screen;
}

static lv_obj_t *box_create(lv_obj_t *mom, int x, int y, int w, int h){
	lv_obj_t *box = lv_obj_create(mom);

	lv_obj_set_size(box, w, h);
	lv_obj_align(box, LV_ALIGN_TOP_LEFT, x, y);
	lv_obj_set_style_radius(box, 0, 0);
	lv_obj_set_style_pad_all(box, 0, 0);
	lv_obj_set_style_bg_color(box, lv_color_hex(COLOUR_BOX), 0);
	lv_obj_set_style_border_color(box, lv_color_hex(COLOUR_EDGE), 0);
	lv_obj_set_style_border_width(box, 1, 0);
	lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
	return box;
}

//the row the knob is on has the bright edge
static void show_mark(lv_obj_t *box, bool is_marked){
	lv_obj_set_style_border_color(box,
		lv_color_hex(is_marked ? COLOUR_MARK : COLOUR_EDGE), 0);
	lv_obj_set_style_border_width(box, is_marked ? EDGE_WITH_MARK : EDGE_WITHOUT, 0);
}

static lv_obj_t *text_create(lv_obj_t *mom, const char *text, const lv_font_t *font,
		uint32_t colour){
	lv_obj_t *label = lv_label_create(mom);

	lv_label_set_text(label, text);
	lv_obj_set_style_text_color(label, lv_color_hex(colour), 0);
	lv_obj_set_style_text_font(label, font, 0);
	return label;
}

static lv_obj_t *button_create(lv_obj_t *mom, int x, int y, int w, int h,
		const char *label, lv_event_cb_t touched){
	lv_obj_t *box = box_create(mom, x, y, w, h);
	lv_obj_t *text = text_create(box, label, &lv_font_montserrat_14, COLOUR_VALUE);

	lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(box, touched, LV_EVENT_CLICKED, NULL);
	return box;
}

//the way back, at the top left of a screen, an arrow to the left: where
//the author wants it, as on a telephone
static void back_button_create(lv_obj_t *mom, lv_event_cb_t touched){
	lv_obj_t *box = box_create(mom, 0, 0, BACK_WIDTH, BAR_HEIGHT);
	lv_obj_t *arrow = text_create(box, LV_SYMBOL_LEFT, &lv_font_montserrat_20, COLOUR_VALUE);

	lv_obj_align(arrow, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(box, touched, LV_EVENT_CLICKED, NULL);
}

/* typing a line of text */

static void input_close(const char *text){
	text_done_fn done = input_done;

	lv_obj_add_flag(input_screen, LV_OBJ_FLAG_HIDDEN);
	input_done = NULL;
	lv_obj_remove_flag(input_opened_from, LV_OBJ_FLAG_HIDDEN);
	if (done != NULL && text != NULL)
		done(text);
}

static void input_save(){
	char text[INPUT_TEXT_MAX];
	char *p;

	snprintf(text, sizeof(text), "%s", lv_textarea_get_text(input_box));
	for (p = text; input_is_capitals && *p; p++)
		*p = (char)toupper((unsigned char)*p);
	input_close(text);
}

static void on_input_save(lv_event_t *e){
	(void)e;
	input_save();
}

static void on_input_cancel(lv_event_t *e){
	(void)e;
	input_close(NULL);
}

static void input_create(){
	input_screen = screen_create();
	input_title = text_create(input_screen, "", &lv_font_montserrat_16, COLOUR_VALUE);
	lv_obj_align(input_title, LV_ALIGN_TOP_LEFT, PAD, INPUT_TITLE_TOP);
	input_prompt = text_create(input_screen, "", &lv_font_montserrat_14, COLOUR_TITLE);
	lv_obj_align(input_prompt, LV_ALIGN_TOP_LEFT, PAD, INPUT_PROMPT_TOP);

	input_box = lv_textarea_create(input_screen);
	lv_textarea_set_one_line(input_box, true);
	lv_obj_set_size(input_box, INPUT_BOX_WIDTH, INPUT_BOX_HEIGHT);
	lv_obj_align(input_box, LV_ALIGN_TOP_LEFT, PAD, INPUT_BOX_TOP);
	lv_obj_set_style_text_font(input_box, &lv_font_montserrat_20, 0);

	button_create(input_screen, PAD + INPUT_BOX_WIDTH + PAD, INPUT_BOX_TOP,
		INPUT_BUTTON_WIDTH, INPUT_BOX_HEIGHT, "SAVE", on_input_save);
	button_create(input_screen, PAD + INPUT_BOX_WIDTH + PAD + INPUT_BUTTON_WIDTH + PAD,
		INPUT_BOX_TOP, INPUT_BUTTON_WIDTH, INPUT_BOX_HEIGHT, "CANCEL", on_input_cancel);

	input_keyboard = lv_keyboard_create(input_screen);
	lv_obj_set_size(input_keyboard, SCREEN_WIDTH, INPUT_KEYBOARD_HEIGHT);
	lv_obj_align(input_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_keyboard_set_textarea(input_keyboard, input_box);
	lv_obj_add_event_cb(input_keyboard, on_input_save, LV_EVENT_READY, NULL);
	lv_obj_add_event_cb(input_keyboard, on_input_cancel, LV_EVENT_CANCEL, NULL);
}

//opens the screen for a line of text, over the screen it is opened from,
//which comes back when it closes. done is given the text if it is saved,
//and is not called if it is not
static void text_input_open(lv_obj_t *from, const char *title, const char *prompt,
		const char *initial, int max_length, bool capitals, text_done_fn done){
	if (input_screen == NULL)
		input_create();
	lv_label_set_text(input_title, title);
	lv_label_set_text(input_prompt, prompt);
	lv_textarea_set_max_length(input_box, max_length);
	lv_textarea_set_text(input_box, initial);
	lv_keyboard_set_mode(input_keyboard,
		capitals ? LV_KEYBOARD_MODE_TEXT_UPPER : LV_KEYBOARD_MODE_TEXT_LOWER);
	input_is_capitals = capitals;
	input_done = done;
	input_opened_from = from;
	lv_obj_add_flag(from, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(input_screen, LV_OBJ_FLAG_HIDDEN);
}

/* the settings */

static void settings_show(){
	if (settings_screen == NULL)
		return;
	settings_show_values();
}

static void settings_show_values(){
	struct panel_control *c;
	char text[INPUT_TEXT_MAX];
	int i;

	for (i = 0; i < SETTINGS; i++){
		show_mark(settings[i].box, i == settings_mark);
		if (settings[i].control == NULL)
			continue;
		c = controls_find(settings[i].control);
		text[0] = 0;
		if (c != NULL)
			controls_value_text(c, text, sizeof(text));
		lv_label_set_text(settings[i].value, text);
	}
}

//the text typed for a control is kept, shown, and sent to the radio
static void set_text(const char *control, const char *text){
	struct panel_control *c = controls_find(control);

	if (c == NULL)
		return;
	snprintf(c->text, sizeof(c->text), "%s", text);
	c->last_user_change = millis();
	Serial.printf("%s %s\n", control, c->text);
	i2c_link_send_text(c->d.number, c->text);
	settings_show();
}

static void callsign_done(const char *text){
	set_text("MYCALLSIGN", text);
}

static void grid_done(const char *text){
	set_text("MYGRID", text);
}

static void text_of(const char *control, char *text, int room){
	struct panel_control *c = controls_find(control);

	text[0] = 0;
	if (c != NULL)
		controls_value_text(c, text, room);
}

static void callsign_open(){
	char now[INPUT_TEXT_MAX];

	text_of("MYCALLSIGN", now, sizeof(now));
	text_input_open(settings_screen, "Callsign", "The station's callsign, as it is sent", now,
		CALLSIGN_MAX, true, callsign_done);
}

static void grid_open(){
	char now[INPUT_TEXT_MAX];

	text_of("MYGRID", now, sizeof(now));
	text_input_open(settings_screen, "Grid", "The station's grid locator, four or six characters", now,
		GRID_MAX, false, grid_done);
}

static void on_setting_touched(lv_event_t *e){
	struct setting *s = (struct setting *)lv_event_get_user_data(e);

	settings_mark = (int)(s - settings);
	settings_show();
	if (s->open != NULL)
		s->open();
}

static void on_back(lv_event_t *e){
	(void)e;
	screens_close();
}

static void settings_create(){
	lv_obj_t *row;
	lv_obj_t *label;
	uint32_t colour;
	int i;

	settings_screen = screen_create();
	label = text_create(settings_screen, "SETTINGS", &lv_font_montserrat_16, COLOUR_VALUE);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, TITLE_LEFT, (BAR_HEIGHT - 16) / 2);
	back_button_create(settings_screen, on_back);

	for (i = 0; i < SETTINGS; i++){
		row = box_create(settings_screen, 0, BAR_HEIGHT + i * ROW_HEIGHT, SCREEN_WIDTH,
			ROW_HEIGHT);
		settings[i].box = row;
		lv_obj_add_event_cb(row, on_setting_touched, LV_EVENT_CLICKED, &settings[i]);
		//what is not written yet is dim
		colour = COLOUR_VALUE;
		if (settings[i].open == NULL)
			colour = COLOUR_DIM;
		label = text_create(row, settings[i].label, &lv_font_montserrat_16, colour);
		lv_obj_align(label, LV_ALIGN_LEFT_MID, PAD, 0);
		settings[i].value = text_create(row, "", &lv_font_montserrat_16, COLOUR_VALUE);
		lv_obj_align(settings[i].value, LV_ALIGN_RIGHT_MID, -PAD, 0);
	}
}

/* the access points */

//the radio is asked to scan; its list comes some seconds later
static void wifi_scan(){
	struct panel_control *wifi = controls_find("WIFI");

	wifi_is_listed = false;
	wifi_is_to_show = true;
	wifi_said[0] = 0;
	if (wifi == NULL)
		return;
	Serial.println("WIFI SCAN");
	i2c_link_changed(wifi->d.number, 0);
}

static void wifi_show(){
	char text[WIFI_SHOWN_MAX];
	int i;

	wifi_is_to_show = false;
	if (controls_find("WIFI") == NULL)
		snprintf(text, sizeof(text), "this radio does not list access points");
	else if (!wifi_is_listed)
		snprintf(text, sizeof(text), "scanning...");
	else
		snprintf(text, sizeof(text), "%d access points", wifi_count);
	if (wifi_said[0] != 0)
		snprintf(text, sizeof(text), "%s", wifi_said);
	lv_label_set_text(wifi_status, text);
	if (wifi_mark > wifi_count - 1)
		wifi_mark = wifi_count - 1;
	if (wifi_mark < 0)
		wifi_mark = 0;
	for (i = 0; i < LINK_WIFI_POINTS_MAX; i++){
		show_mark(wifi_rows[i].box, wifi_is_listed && i == wifi_mark);
		if (!wifi_is_listed || i >= wifi_count){
			lv_obj_add_flag(wifi_rows[i].box, LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_label_set_text(wifi_rows[i].name, wifi_points[i].name);
		snprintf(text, sizeof(text), "%s%d dBm", wifi_points[i].is_secured ? "" : "open  ",
			wifi_points[i].signal_dbm);
		lv_label_set_text(wifi_rows[i].signal, text);
		lv_obj_remove_flag(wifi_rows[i].box, LV_OBJ_FLAG_HIDDEN);
	}
}

//the access point chosen and its key go to the radio, which joins it. The
//key is not printed
static void wifi_send_choice(const char *key){
	char text[LINK_TEXT_MAX + 1];

	snprintf(text, sizeof(text), "%s%c%s", wifi_points[wifi_mark].name,
		LINK_WIFI_BETWEEN, key);
	Serial.printf("WIFI JOIN %s\n", wifi_points[wifi_mark].name);
	i2c_link_send_text(LINK_WIFI_CHOSEN, text);
	snprintf(wifi_said, sizeof(wifi_said), "sent to the radio");
	wifi_is_to_show = true;
}

//an access point is chosen: one that wants a key is asked for it on the
//text screen, and an open one goes to the radio as it is
static void wifi_choose(int which){
	if (!wifi_is_listed || which < 0 || which >= wifi_count)
		return;
	wifi_mark = which;
	wifi_is_to_show = true;
	if (!wifi_points[which].is_secured){
		wifi_send_choice("");
		return;
	}
	text_input_open(wifi_screen, wifi_points[which].name,
		"The key of this access point, 8 to 63 characters", "",
		LINK_WIFI_KEY_MAX, false, wifi_send_choice);
}

static void on_wifi_row_touched(lv_event_t *e){
	wifi_choose((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_wifi_scan(lv_event_t *e){
	(void)e;
	wifi_scan();
}

static void on_back_to_settings(lv_event_t *e){
	(void)e;
	lv_obj_add_flag(wifi_screen, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(settings_screen, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_create(){
	lv_obj_t *label;
	lv_obj_t *list;
	int i;

	wifi_screen = screen_create();
	label = text_create(wifi_screen, "WIFI", &lv_font_montserrat_16, COLOUR_VALUE);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, TITLE_LEFT, (BAR_HEIGHT - 16) / 2);
	wifi_status = text_create(wifi_screen, "", &lv_font_montserrat_14, COLOUR_TITLE);
	lv_obj_align(wifi_status, LV_ALIGN_TOP_LEFT, WIFI_STATUS_LEFT, (BAR_HEIGHT - 14) / 2);
	button_create(wifi_screen, SCREEN_WIDTH - BUTTON_WIDTH, 0, BUTTON_WIDTH, BAR_HEIGHT,
		LV_SYMBOL_REFRESH "  SCAN", on_wifi_scan);
	back_button_create(wifi_screen, on_back_to_settings);

	//the rows are in a box that a finger moves up and down
	list = lv_obj_create(wifi_screen);
	lv_obj_set_size(list, SCREEN_WIDTH, SCREEN_HEIGHT - BAR_HEIGHT);
	lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, BAR_HEIGHT);
	lv_obj_set_style_bg_color(list, lv_color_hex(COLOUR_SCREEN), 0);
	lv_obj_set_style_border_width(list, 0, 0);
	lv_obj_set_style_radius(list, 0, 0);
	lv_obj_set_style_pad_all(list, 0, 0);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);
	wifi_list = list;
	for (i = 0; i < LINK_WIFI_POINTS_MAX; i++){
		wifi_rows[i].box = box_create(list, 0, i * ROW_HEIGHT, SCREEN_WIDTH, ROW_HEIGHT);
		lv_obj_add_event_cb(wifi_rows[i].box, on_wifi_row_touched, LV_EVENT_CLICKED,
			(void *)(intptr_t)i);
		wifi_rows[i].name = text_create(wifi_rows[i].box, "", &lv_font_montserrat_16,
			COLOUR_VALUE);
		lv_obj_align(wifi_rows[i].name, LV_ALIGN_LEFT_MID, PAD, 0);
		wifi_rows[i].signal = text_create(wifi_rows[i].box, "", &lv_font_montserrat_14,
			COLOUR_TITLE);
		lv_obj_align(wifi_rows[i].signal, LV_ALIGN_RIGHT_MID, -PAD, 0);
	}
}

static void wifi_open(){
	if (wifi_screen == NULL)
		wifi_create();
	lv_obj_add_flag(settings_screen, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(wifi_screen, LV_OBJ_FLAG_HIDDEN);
	wifi_scan();
}

/* what the rest of the panel calls */

//the radio's list of access points has come. This is called with
//interrupts held off, so it only keeps the list; screens_slice() shows it
void screens_wifi_listed(const struct link_wifi_point *points, int count){
	if (count > LINK_WIFI_POINTS_MAX)
		count = LINK_WIFI_POINTS_MAX;
	memcpy(wifi_points, points, count * sizeof(wifi_points[0]));
	wifi_count = count;
	wifi_is_listed = true;
	wifi_is_to_show = true;
}

static bool is_up(lv_obj_t *screen){
	return screen != NULL && !lv_obj_has_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

//the knob, while a screen other than the main one is up: turning moves the
//mark from row to row, the list moving to keep it in view, and a click
//chooses the marked row, as a touch on it does
void screens_knob_turned(int steps){
	if (is_up(wifi_screen) && wifi_is_listed && wifi_count > 0){
		wifi_mark += steps;
		if (wifi_mark < 0)
			wifi_mark = 0;
		if (wifi_mark > wifi_count - 1)
			wifi_mark = wifi_count - 1;
		lv_obj_scroll_to_view(wifi_rows[wifi_mark].box, LV_ANIM_OFF);
		wifi_show();
	}
	if (is_up(settings_screen)){
		settings_mark += steps;
		if (settings_mark < 0)
			settings_mark = 0;
		if (settings_mark > SETTINGS - 1)
			settings_mark = SETTINGS - 1;
		settings_show();
	}
}

void screens_knob_clicked(){
	if (is_up(wifi_screen))
		wifi_choose(wifi_mark);
	else if (is_up(settings_screen) && settings[settings_mark].open != NULL)
		settings[settings_mark].open();
}

//what the radio says for the status bar is shown on the WiFi screen too,
//where it says whether the access point was joined
void screens_status(const char *text){
	if (wifi_screen == NULL)
		return;
	snprintf(wifi_said, sizeof(wifi_said), "%s", text);
	wifi_is_to_show = true;
}

//from loop(): what has changed on a screen that is up is shown
void screens_slice(){
	if (wifi_is_to_show && wifi_screen != NULL)
		wifi_show();
}

//what typing a text and saving it would do, for trying it from the serial
//port: "MYCALLSIGN VU2ESE"
void screens_set_text(char *control_and_text){
	char *text = strchr(control_and_text, ' ');

	if (text == NULL)
		return;
	*text++ = 0;
	set_text(control_and_text, text);
}

//what a touch on a row of the settings, or on BACK, would do, by name
void screens_select(const char *name){
	int i;

	if (!strcmp(name, "BACK"))
		screens_close();
	if (settings_screen == NULL || lv_obj_has_flag(settings_screen, LV_OBJ_FLAG_HIDDEN))
		return;
	for (i = 0; i < SETTINGS; i++){
		if (!strcasecmp(name, settings[i].label) && settings[i].open != NULL)
			settings[i].open();
	}
}

bool screens_is_open(){
	if (settings_screen != NULL && !lv_obj_has_flag(settings_screen, LV_OBJ_FLAG_HIDDEN))
		return true;
	if (wifi_screen != NULL && !lv_obj_has_flag(wifi_screen, LV_OBJ_FLAG_HIDDEN))
		return true;
	return input_screen != NULL && !lv_obj_has_flag(input_screen, LV_OBJ_FLAG_HIDDEN);
}

void screens_open_settings(){
	if (settings_screen == NULL)
		settings_create();
	waterfall_set_painting(false);
	main_hide();
	settings_show();
	lv_obj_remove_flag(settings_screen, LV_OBJ_FLAG_HIDDEN);
}

//whatever is open is closed and the main screen comes back
void screens_close(){
	if (!screens_is_open())
		return;
	if (input_screen != NULL)
		lv_obj_add_flag(input_screen, LV_OBJ_FLAG_HIDDEN);
	if (wifi_screen != NULL)
		lv_obj_add_flag(wifi_screen, LV_OBJ_FLAG_HIDDEN);
	input_done = NULL;
	lv_obj_add_flag(settings_screen, LV_OBJ_FLAG_HIDDEN);
	main_ui();
	waterfall_set_painting(true);
}

//the radio has said something: the settings show it if they are up
void screens_radio_said(){
	if (settings_screen != NULL && !lv_obj_has_flag(settings_screen, LV_OBJ_FLAG_HIDDEN))
		settings_show();
}
