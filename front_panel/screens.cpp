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
//   Date/Time   the radio's time, in UTC, and where it has it from, which
//               the radio says in its control CLOCK. The radio takes the
//               time from the network or from its clock chip when it can;
//               when the chip has lost the time and there is no network,
//               the operator gives it here: the year, the month, the day,
//               the hour and the minute, each on a roller, and SYNC, which
//               sends them at the moment it is touched. A roller is turned
//               with a finger, or touched and then turned with the knob; a
//               click of the knob goes on to the next roller. What is
//               given is used only until the radio has the time from the
//               network or the chip again; radio_clock.c on the radio says
//               why and how.
//   WiFi        the access points the radio can hear, the strongest first,
//               no more than sixteen. Opening the screen asks the radio to
//               scan, which takes some seconds; SCAN asks again. A touch
//               on one chooses it: its key is typed on the text screen,
//               and the name and the key go to the radio, which joins it
//               and says on this screen's top line how it went. An open
//               one is joined with no key asked for.
//
//   FT8 Operations  NORMAL, RUNNING or POUNCING, the radio's FT8_OPS: how
//               FT8 is worked, ft8_qso.c on the radio says. A touch, or a
//               click of the knob, goes to the next.
//   FT8 Reply   RR73 or RRR, the radio's FT8_REPLY, the same way.
//
// There are more rows than the screen has room for under its title, so
// they are in a list that scrolls, by a finger, or by the knob, which
// keeps the marked row in view.
//
// The radio keeps the callsign and the grid and remembers them; the panel
// only shows what the radio last said and sends what was typed.
//
// TYPING A LINE OF TEXT
//
// One screen serves wherever a line of text is wanted: a title, a prompt,
// a box of one line, and LVGL's keyboard. The author's rule is that the box
// is in the upper part of the screen, so that the keyboard, which takes
// the lower part, never covers it. The button beside the box, or the
// keyboard's own tick, gives the text to whoever asked for it; the button
// says what will be done with it, SAVE for a setting, GO for a search. The
// arrow at the top left, the same as on every other screen, or the
// keyboard's cross, goes back and gives nothing. Either way the screen it
// was opened from comes back.
//
// THE LOGBOOK
//
// LOG on the main screen opens it: the newest entries in the radio's
// logbook, the newest at the top, a hundred of them, which is what the
// panel has memory for. Opening the screen asks the radio for them. A line
// to an entry, in the order zbitxv2's panel showed them: the date and the
// time in UTC, the frequency in kHz, the mode, the other station's
// callsign, what was sent and what was received, under a line of headings. One is marked; the knob moves the mark, and
// when it goes past the top or the bottom the list moves on by a screenful.
// A touch on an entry marks it.
//
// The author's rule for a logbook: an entry is never changed, but a bad one
// can be deleted. A touch on an entry, or a click of the knob on the marked
// one, asks "Do you want to delete the entry?", with the entry shown under
// the question, and YES and NO. NO is marked to begin with; the knob moves
// between the two and a click chooses, or either is touched. On YES the
// radio is asked to delete that entry, by its number in the logbook, and
// sends the list afresh. What is being searched for goes with the request
// and is kept as it is, so the list that comes back is still of that
// search: the operator may be deleting every entry of one callsign. The
// mark stays where it was.
//
// Under the title is the entry being written, as zbitxv2's logger had it:
// CALL, SENT, RECV, EXCH and NR, the radio's controls of those names, which
// logger.c on the radio says what they are; and three buttons: "?" beside
// CALL, and WIPE and SAVE at the end. A touch on a field brings a keyboard up over the list, at the
// author's asking, and the field is typed into there on its row, with its
// edge marked; the keyboard's tick, or a touch on another field, or on any
// of the three buttons, takes what was typed, and its cross drops it. "?" searches
// the logbook for the callsign in CALL, or the beginning of one, and the
// radio sends the newest hundred entries with stations whose callsign
// begins so; an empty CALL lists every station. The author's rule is that
// the request to the radio always carries what is searched for; it is
// empty when the logbook is first opened. WIPE clears the five fields and
// lists the whole logbook again. SAVE asks the radio to log the entry. The
// author's rule is that it needs at least CALL, SENT and RECV; the radio
// checks, and says what is missing, or that the entry is logged, on the
// line beside the title, where the count of entries is otherwise, and what
// they were searched for. Once it is logged the radio clears the fields
// and sends the whole logbook again, the new entry at the top. Eight
// entries are in view, under the fields.
//
// The fields are the radio's, and what the panel shows in them is what the
// radio says they are, as the settings show the callsign. The radio's word
// is taken at once after WIPE or SAVE, which the operator presses when done
// typing, though it might come within a second of a field being typed,
// when the panel would otherwise keep what was typed. And a SAVE waits
// until the last field typed has gone to the radio, a part at a time, so
// that the entry is not saved without it.
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
#define INPUT_BOX_WIDTH 380
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
#define COLOUR_MARKED_ROW 0x604800

#define WIFI_STATUS_LEFT (TITLE_LEFT + 64)
#define WIFI_SHOWN_MAX 48

#define FIRST_YEAR 2026
#define YEARS 75
#define ROLLER_TOP 84
#define ROLLER_LABEL_TOP 60
#define ROLLER_ROWS 3
#define ROLLER_OPTIONS_MAX (YEARS * 5 + 1)
#define DATE_STATUS_TOP 280
#define MINUTES_AN_HOUR 60
#define HOURS_A_DAY 24
#define MONTHS 12

#define CALLSIGN_MAX 12
#define GRID_MAX 6

typedef void (*text_done_fn)(const char *text);

//a row of the settings: what it is called, the radio's control it shows, if
//it has one, and what a touch on it does
struct setting {
	const char *label;
	const char *control;   // the radio's control whose value is shown, or
	const char *fixed;     // the panel's own text, when it has no control
	void (*open)();
	lv_obj_t *value;
	lv_obj_t *box;
};

static bool is_up(lv_obj_t *screen);
static void callsign_open();
static void grid_open();
static void bootsel_open();
// a row that only shows something; a row with no open is drawn dim, as
// not done yet
static void nothing_open(){}
static void date_open();
static void wifi_open();
static void next_choice_open();
static void settings_show_values();

#ifndef PANEL_FIRMWARE
#define PANEL_FIRMWARE "unknown"
#endif

static struct setting settings[] = {
	{"Callsign", "MYCALLSIGN", NULL, callsign_open, NULL, NULL},
	{"Grid", "MYGRID", NULL, grid_open, NULL, NULL},
	{"Date/Time", "CLOCK", NULL, date_open, NULL, NULL},
	{"WiFi", NULL, NULL, wifi_open, NULL, NULL},
	{"FT8 Operations", "FT8_OPS", NULL, next_choice_open, NULL, NULL},
	{"FT8 Reply", "FT8_REPLY", NULL, next_choice_open, NULL, NULL},
	{"Panel firmware", NULL, PANEL_FIRMWARE, nothing_open, NULL, NULL},
	{"BOOTSEL", NULL, "-", bootsel_open, NULL, NULL},
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

//the rollers of the date screen, left to right
enum {
	YEAR,
	MONTH,
	DAY,
	HOUR,
	MINUTE,
	ROLLERS,
};

struct roller_place {
	const char *label;
	int16_t x;
	int16_t w;
	int first;
	int count;
};

static const struct roller_place roller_places[ROLLERS] = {
	{"Year", 8, 104, FIRST_YEAR, YEARS},
	{"Month", 120, 80, 1, 12},
	{"Day", 208, 80, 1, 31},
	{"Hour", 296, 80, 0, 24},
	{"Minute", 384, 88, 0, 60},
};

#define DAYS_IN_A_YEAR 365
#define DAYS_IN_A_LEAP_YEAR 366

//the log's columns, left to right, their headings and their widths, which
//add up to the screen's
#define LOG_COLUMNS 7
#define LOG_ROWS_IN_VIEW 8
#define LOG_TABLE_ROWS (LOG_ROWS_IN_VIEW + 1)
#define LOG_ROW_PAD 4
#define LOG_FIELDS_BETWEEN '|'
#define ASK_WIDTH 400
#define ASK_HEIGHT 148
#define ASK_BUTTON_WIDTH 112
#define ASK_BUTTON_HEIGHT 48
#define ASK_DIMMED LV_OPA_60
#define LOG_SEARCH_MAX 12
#define LOG_STATUS_LEFT 152
#define LOG_STATUS_WIDTH (SCREEN_WIDTH - LOG_STATUS_LEFT - PAD)
//the entry being written: a row of fields and buttons under the title,
//with a gap around each
#define LOG_GAP 4
#define LOG_FIELDS_HEIGHT 44
#define LOG_FIELD_TITLE_TOP 2
#define LOG_FIELD_VALUE_BOTTOM (-4)
#define LOG_FIELD_MAX 12
#define LOG_TABLE_TOP (BAR_HEIGHT + LOG_FIELDS_HEIGHT + 2 * LOG_GAP)
#define LOG_SAID_MAX 64

static const char *const log_headings[LOG_COLUMNS] = {
	"Date", "UTC", "kHz", "Mode", "Callsign", "Sent", "Rcvd",
};
static const int16_t log_widths[LOG_COLUMNS] = { 80, 44, 48, 42, 90, 88, 88 };

//a field of the entry: the radio's control, its width on the row, and
//the box it is typed into
struct log_field {
	const char *control;
	int16_t width;
	lv_obj_t *box;
	lv_obj_t *value;
};

static struct log_field log_fields[] = {
	{"CALL", 96, NULL, NULL},
	{"SENT", 48, NULL, NULL},
	{"RECV", 48, NULL, NULL},
	{"EXCH", 64, NULL, NULL},
	{"NR", 48, NULL, NULL},
};
#define LOG_FIELDS ((int)(sizeof(log_fields) / sizeof(log_fields[0])))

//the field "?" searches for, and the buttons and their widths: "?" after
//it, WIPE and SAVE after the rest
#define LOG_CALL_FIELD 0
#define LOG_FIND_WIDTH 36
#define LOG_WIPE_WIDTH 52
#define LOG_SAVE_WIDTH 52

static lv_obj_t *log_screen;
static lv_obj_t *log_status;
static char log_searched_for[LOG_SEARCH_MAX + 1];
static char log_said[LOG_SAID_MAX + 1];
#define NOT_EDITING (-1)
static int log_editing = NOT_EDITING;
static lv_obj_t *log_keyboard;
static bool log_is_save_waiting;
static lv_obj_t *log_table;
static char log_lines[LINK_LOG_LINES_MAX][LINK_LOG_LINE_MAX + 1];
static volatile int log_count;
static volatile bool log_is_whole;
static volatile bool log_is_to_show;
static int log_top;
static int log_mark;

//the question asked before an entry is deleted
static lv_obj_t *ask_layer;
static lv_obj_t *ask_detail;
static lv_obj_t *ask_yes;
static lv_obj_t *ask_no;
static bool ask_is_on_yes;

static lv_obj_t *date_screen;
static lv_obj_t *date_status;
static lv_obj_t *rollers[ROLLERS];
static int roller_marked;

static lv_obj_t *settings_screen;
static int settings_mark;
static int wifi_mark;
static lv_obj_t *wifi_list;
static lv_obj_t *input_screen;
static lv_obj_t *input_title;
static lv_obj_t *input_prompt;
static lv_obj_t *input_box;
static lv_obj_t *input_keyboard;
static lv_obj_t *input_accept;
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
	uint32_t colour = COLOUR_EDGE;
	int width = EDGE_WITHOUT;

	if (is_marked){
		colour = COLOUR_MARK;
		width = EDGE_WITH_MARK;
	}
	lv_obj_set_style_border_color(box, lv_color_hex(colour), 0);
	lv_obj_set_style_border_width(box, width, 0);
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
	lv_obj_t *accept;

	input_screen = screen_create();
	back_button_create(input_screen, on_input_cancel);
	input_title = text_create(input_screen, "", &lv_font_montserrat_16, COLOUR_VALUE);
	lv_obj_align(input_title, LV_ALIGN_TOP_LEFT, TITLE_LEFT, INPUT_TITLE_TOP);
	input_prompt = text_create(input_screen, "", &lv_font_montserrat_14, COLOUR_TITLE);
	lv_obj_align(input_prompt, LV_ALIGN_TOP_LEFT, TITLE_LEFT, INPUT_PROMPT_TOP);

	input_box = lv_textarea_create(input_screen);
	lv_textarea_set_one_line(input_box, true);
	lv_obj_set_size(input_box, INPUT_BOX_WIDTH, INPUT_BOX_HEIGHT);
	lv_obj_align(input_box, LV_ALIGN_TOP_LEFT, PAD, INPUT_BOX_TOP);
	lv_obj_set_style_text_font(input_box, &lv_font_montserrat_20, 0);

	accept = button_create(input_screen, PAD + INPUT_BOX_WIDTH + PAD, INPUT_BOX_TOP,
		INPUT_BUTTON_WIDTH, INPUT_BOX_HEIGHT, "", on_input_save);
	input_accept = lv_obj_get_child(accept, 0);

	input_keyboard = lv_keyboard_create(input_screen);
	lv_obj_set_size(input_keyboard, SCREEN_WIDTH, INPUT_KEYBOARD_HEIGHT);
	lv_obj_align(input_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_keyboard_set_textarea(input_keyboard, input_box);
	lv_obj_add_event_cb(input_keyboard, on_input_save, LV_EVENT_READY, NULL);
	lv_obj_add_event_cb(input_keyboard, on_input_cancel, LV_EVENT_CANCEL, NULL);
}

//opens the screen for a line of text, over the screen it is opened from,
//which comes back when it closes. accept is the word on the button that
//takes the text: SAVE for a setting, GO for a search. done is given the
//text if it is taken, and is not called if the arrow goes back instead
static void text_input_open(lv_obj_t *from, const char *title, const char *prompt,
		const char *initial, int max_length, bool capitals, const char *accept,
		text_done_fn done){
	if (input_screen == NULL)
		input_create();
	lv_label_set_text(input_title, title);
	lv_label_set_text(input_prompt, prompt);
	lv_label_set_text(input_accept, accept);
	lv_textarea_set_max_length(input_box, max_length);
	lv_textarea_set_text(input_box, initial);
	if (capitals)
		lv_keyboard_set_mode(input_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
	else
		lv_keyboard_set_mode(input_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
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

	if (settings[settings_mark].box != NULL)
		lv_obj_scroll_to_view(settings[settings_mark].box, LV_ANIM_OFF);
	for (i = 0; i < SETTINGS; i++){
		show_mark(settings[i].box, i == settings_mark);
		if (settings[i].fixed != NULL)
			lv_label_set_text(settings[i].value, settings[i].fixed);
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
		CALLSIGN_MAX, true, "SAVE", callsign_done);
}

static void grid_open(){
	char now[INPUT_TEXT_MAX];

	text_of("MYGRID", now, sizeof(now));
	text_input_open(settings_screen, "Grid", "The station's grid locator, four or six characters", now,
		GRID_MAX, false, "SAVE", grid_done);
}

//a setting that is a choice goes to its next choice, and from the last to
//the first, and the radio is told
static void next_choice_open(){
	struct panel_control *c = controls_find(settings[settings_mark].control);
	int next;

	if (c == NULL || c->d.kind != LINK_KIND_CHOICE || controls_choices(c) < 1)
		return;
	next = ((int)c->value + 1) % controls_choices(c);
	c->value = next;
	c->last_user_change = millis();
	Serial.printf("%s %d\n", c->d.name, next);
	i2c_link_changed(c->d.number, next);
	settings_show();
}

// BOOTSEL restarts the panel as the USB drive its chip has in ROM, for an
// owner who puts new firmware on it with the cable: the author asked for it
// among the settings on 16 October 2026, with the version beside it. The
// panel does it itself, with or without the radio program. Two taps within
// BOOTSEL_AGAIN_MS, since a panel left as a drive shows nothing until it is
// given firmware or switched off and on
#define BOOTSEL_AGAIN_MS 5000
static uint32_t bootsel_tapped_ms;

static struct setting *setting_called(const char *label){
	for (int i = 0; i < SETTINGS; i++){
		if (!strcmp(settings[i].label, label))
			return &settings[i];
	}
	return NULL;
}

static void bootsel_open(){
	struct setting *s = setting_called("BOOTSEL");

	if (bootsel_tapped_ms != 0 && millis() - bootsel_tapped_ms < BOOTSEL_AGAIN_MS){
		s->fixed = "USB DRIVE";
		settings_show();
		lv_timer_handler();
		delay(200);
		rp2040.rebootToBootloader();
		return;
	}
	bootsel_tapped_ms = millis();
	s->fixed = "TAP AGAIN";
	settings_show();
}

// TAP AGAIN goes back to - when the second tap does not come
static void bootsel_slice(){
	struct setting *s;

	if (bootsel_tapped_ms == 0 || millis() - bootsel_tapped_ms < BOOTSEL_AGAIN_MS)
		return;
	bootsel_tapped_ms = 0;
	s = setting_called("BOOTSEL");
	s->fixed = "-";
	settings_show();
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
	lv_obj_t *list;
	lv_obj_t *row;
	lv_obj_t *label;
	uint32_t colour;
	int i;

	settings_screen = screen_create();
	label = text_create(settings_screen, "SETTINGS", &lv_font_montserrat_16, COLOUR_VALUE);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, TITLE_LEFT, (BAR_HEIGHT - 16) / 2);
	back_button_create(settings_screen, on_back);

	//the rows, in a list under the title that scrolls
	list = lv_obj_create(settings_screen);
	lv_obj_set_size(list, SCREEN_WIDTH, SCREEN_HEIGHT - BAR_HEIGHT);
	lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, BAR_HEIGHT);
	lv_obj_set_style_bg_color(list, lv_color_hex(COLOUR_SCREEN), 0);
	lv_obj_set_style_border_width(list, 0, 0);
	lv_obj_set_style_radius(list, 0, 0);
	lv_obj_set_style_pad_all(list, 0, 0);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);

	for (i = 0; i < SETTINGS; i++){
		row = box_create(list, 0, i * ROW_HEIGHT, SCREEN_WIDTH, ROW_HEIGHT);
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

/* the logbook */

//the radio is asked for the newest entries with what is searched for,
//which is every station's if nothing is; they come in parts
static void log_ask(){
	log_is_whole = false;
	log_is_to_show = true;
	log_top = 0;
	log_mark = 0;
	log_said[0] = 0;
	Serial.printf("LOG %s\n", log_searched_for);
	i2c_link_send_text(LINK_LOG_LIST, log_searched_for);
}

static void log_search_done(const char *text){
	snprintf(log_searched_for, sizeof(log_searched_for), "%s", text);
	log_ask();
}

//an action of the radio's is told to it, as the main screen's buttons do
static void send_action(const char *name, const char *choice){
	struct panel_control *c = controls_find(name);
	struct panel_control tried;
	char text[CONTROL_TEXT_MAX];
	int i;

	if (c == NULL)
		return;
	tried = *c;
	for (i = 0; i < controls_choices(c); i++){
		tried.value = i;
		controls_value_text(&tried, text, sizeof(text));
		if (!strcmp(text, choice)){
			Serial.printf("%s %s\n", name, choice);
			i2c_link_changed(c->d.number, i);
		}
	}
}

//each field shows what the radio says it is, but for the one being typed
//into, which shows what is typed
static void log_fields_show(){
	char text[INPUT_TEXT_MAX];
	int i;

	if (log_screen == NULL)
		return;
	for (i = 0; i < LOG_FIELDS; i++){
		if (i == log_editing)
			continue;
		text_of(log_fields[i].control, text, sizeof(text));
		lv_textarea_set_text(log_fields[i].value, text);
	}
}

//the operator is done typing: what the radio says the fields are is taken
//at once from now
static void log_fields_given_back(){
	struct panel_control *c;
	int i;

	for (i = 0; i < LOG_FIELDS; i++){
		c = controls_find(log_fields[i].control);
		if (c != NULL)
			c->last_user_change = 0;
	}
}

//the field being typed into is let go: its edge goes back to the others',
//and the keyboard goes down, if nothing else is to be typed
static void log_editing_end(){
	if (log_editing == NOT_EDITING)
		return;
	lv_obj_remove_state(log_fields[log_editing].value, LV_STATE_FOCUSED);
	show_mark(log_fields[log_editing].box, false);
	log_editing = NOT_EDITING;
	lv_obj_add_flag(log_keyboard, LV_OBJ_FLAG_HIDDEN);
	log_fields_show();
}

//what is typed in the field goes to the radio, in capitals, as callsigns
//and reports are
static void log_editing_done(){
	char text[INPUT_TEXT_MAX];
	char *p;

	if (log_editing == NOT_EDITING)
		return;
	snprintf(text, sizeof(text), "%s", lv_textarea_get_text(log_fields[log_editing].value));
	for (p = text; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	set_text(log_fields[log_editing].control, text);
	log_editing_end();
}

//the keyboard comes up over the list, under the fields, and what is typed
//goes into the field touched, there on the row; a touch on another field
//takes what was typed in this one first
static void log_editing_begin(int which){
	if (which == log_editing)
		return;
	log_editing_done();
	log_editing = which;
	show_mark(log_fields[which].box, true);
	lv_obj_add_state(log_fields[which].value, LV_STATE_FOCUSED);
	lv_textarea_set_cursor_pos(log_fields[which].value, LV_TEXTAREA_CURSOR_LAST);
	lv_keyboard_set_textarea(log_keyboard, log_fields[which].value);
	lv_keyboard_set_mode(log_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
	lv_obj_remove_flag(log_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void on_log_field_touched(lv_event_t *e){
	log_editing_begin((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_log_keyboard_ready(lv_event_t *e){
	(void)e;
	log_editing_done();
}

//the keyboard's cross: what was typed is dropped, and the field shows what
//the radio has
static void on_log_keyboard_cancel(lv_event_t *e){
	(void)e;
	log_editing_end();
}

//"?": the logbook is searched for the callsign in CALL, as it is typed if
//it is being typed
static void log_find(){
	char call[INPUT_TEXT_MAX];

	log_editing_done();
	text_of("CALL", call, sizeof(call));
	log_search_done(call);
}

static void log_wipe(){
	struct panel_control *c;
	int i;

	log_editing_end();
	log_fields_given_back();
	for (i = 0; i < LOG_FIELDS; i++){
		c = controls_find(log_fields[i].control);
		if (c != NULL)
			c->text[0] = 0;
	}
	send_action("LOGGER", "WIPE");
	log_fields_show();
	log_search_done("");
}

//the radio sends the whole logbook once the entry is saved, so what was
//searched for goes; the SAVE itself goes from screens_slice(), when the
//last field typed has gone
static void log_save(){
	log_editing_done();
	log_fields_given_back();
	log_searched_for[0] = 0;
	log_said[0] = 0;
	log_is_save_waiting = true;
}

static void on_log_find_touched(lv_event_t *e){
	(void)e;
	log_find();
}

static void on_log_wipe_touched(lv_event_t *e){
	(void)e;
	log_wipe();
}

static void on_log_save_touched(lv_event_t *e){
	(void)e;
	log_save();
}

//an entry's line is put into a row of the table, a field to a column; the
//first field, the entry's number in the logbook, is not shown
static void log_fill_row(int table_row, const char *line){
	char field[LINK_LOG_LINE_MAX + 1];
	const char *at = strchr(line, LOG_FIELDS_BETWEEN);
	const char *end;
	int column;
	int length;

	for (column = 0; column < LOG_COLUMNS; column++){
		field[0] = 0;
		if (at != NULL){
			at++;
			end = strchr(at, LOG_FIELDS_BETWEEN);
			length = (int)strlen(at);
			if (end != NULL)
				length = (int)(end - at);
			memcpy(field, at, length);
			field[length] = 0;
			at = end;
		}
		lv_table_set_cell_value(log_table, table_row, column, field);
	}
}

//the entries in view are put into the table
static void log_fill(){
	int row;

	for (row = 0; row < LOG_ROWS_IN_VIEW; row++){
		if (log_is_whole && log_top + row < log_count)
			log_fill_row(row + 1, log_lines[log_top + row]);
		else
			log_fill_row(row + 1, "");
	}
}

static void log_show(){
	char text[WIFI_SHOWN_MAX];

	log_is_to_show = false;
	//what the radio said comes first: an entry logged, or what it needs
	if (log_said[0] != 0)
		snprintf(text, sizeof(text), "%s", log_said);
	else if (!log_is_whole)
		snprintf(text, sizeof(text), "loading...");
	else if (log_searched_for[0] != 0)
		snprintf(text, sizeof(text), "%d entries of %s", (int)log_count, log_searched_for);
	else
		snprintf(text, sizeof(text), "%d entries", (int)log_count);
	lv_label_set_text(log_status, text);
	//a list that has come again after an entry was deleted keeps the mark
	//where it was, or on the last entry if that one is gone
	if (log_is_whole){
		if (log_mark > log_count - 1)
			log_mark = log_count - 1;
		if (log_mark < 0)
			log_mark = 0;
		log_top = log_mark - log_mark % LOG_ROWS_IN_VIEW;
	}
	log_fill();
	lv_obj_invalidate(log_table);
}

//an entry's row is painted again, and no more of the table than that: the
//rows are all of one height, a line of text each
static void log_row_repaint(int which){
	lv_area_t area;
	int row_height = lv_obj_get_self_height(log_table) / LOG_TABLE_ROWS;
	int table_row = which - log_top + 1;

	if (table_row < 1 || table_row >= LOG_TABLE_ROWS)
		return;
	lv_obj_get_coords(log_table, &area);
	area.y1 += table_row * row_height;
	area.y2 = area.y1 + row_height;
	lv_obj_invalidate_area(log_table, &area);
}

//the mark is moved to an entry; if that is out of view, the list moves on
//by a screenful, and only then is the table filled again
static void log_mark_at(int which){
	int top = log_top;
	int was_marked = log_mark;

	if (!log_is_whole || log_count == 0)
		return;
	if (which < 0)
		which = 0;
	if (which > log_count - 1)
		which = log_count - 1;
	log_mark = which;
	while (log_mark < top)
		top -= LOG_ROWS_IN_VIEW;
	while (log_mark >= top + LOG_ROWS_IN_VIEW)
		top += LOG_ROWS_IN_VIEW;
	if (top < 0)
		top = 0;
	if (top != log_top){
		log_top = top;
		log_fill();
		lv_obj_invalidate(log_table);
		return;
	}
	log_row_repaint(was_marked);
	log_row_repaint(log_mark);
}

//the headings and the marked entry are given their own colours as the
//table is drawn
static void on_log_drawn(lv_event_t *e){
	lv_draw_task_t *task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
	lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);

	if (base == NULL || base->part != LV_PART_ITEMS || fill == NULL)
		return;
	if (base->id1 == 0)
		fill->color = lv_color_hex(COLOUR_EDGE);
	else if (log_is_whole && log_top + (int)base->id1 - 1 == log_mark
			&& log_mark < log_count)
		fill->color = lv_color_hex(COLOUR_MARKED_ROW);
}

static void ask_show(){
	show_mark(ask_yes, ask_is_on_yes);
	show_mark(ask_no, !ask_is_on_yes);
}

//the marked entry is shown under the question: who, when, and where
static void log_ask_to_delete(){
	char fields[LINK_LOG_LINE_MAX + 1];
	char *field[LOG_COLUMNS + 1];
	char text[2 * LINK_LOG_LINE_MAX];
	int i;

	if (!log_is_whole || log_mark < 0 || log_mark >= log_count)
		return;
	snprintf(fields, sizeof(fields), "%s", log_lines[log_mark]);
	field[0] = fields;
	for (i = 1; i <= LOG_COLUMNS; i++){
		field[i] = strchr(field[i - 1], LOG_FIELDS_BETWEEN);
		if (field[i] == NULL)
			return;
		*field[i]++ = 0;
	}
	//the fields: its number, the date, the time, kHz, the mode, the callsign
	snprintf(text, sizeof(text), "%s   %s %s   %s kHz %s", field[5], field[1], field[2],
		field[3], field[4]);
	lv_label_set_text(ask_detail, text);
	ask_is_on_yes = false;
	ask_show();
	lv_obj_remove_flag(ask_layer, LV_OBJ_FLAG_HIDDEN);
}

//the radio is asked to delete the marked entry and to send the list of the
//same search again; the mark and the search stay as they are
static void log_delete_marked(){
	char text[LINK_TEXT_MAX + 1];

	snprintf(text, sizeof(text), "%d%c%s", atoi(log_lines[log_mark]), LINK_LOG_BETWEEN,
		log_searched_for);
	Serial.printf("LOG DELETE %d\n", atoi(log_lines[log_mark]));
	i2c_link_send_text(LINK_LOG_DELETE, text);
	log_is_whole = false;
	log_is_to_show = true;
}

static void ask_answered(bool is_yes){
	lv_obj_add_flag(ask_layer, LV_OBJ_FLAG_HIDDEN);
	if (is_yes)
		log_delete_marked();
}

static void on_ask_yes(lv_event_t *e){
	(void)e;
	ask_answered(true);
}

static void on_ask_no(lv_event_t *e){
	(void)e;
	ask_answered(false);
}

//the question, over the list, which is dimmed behind it and cannot be
//touched while it is asked
static void ask_create(){
	lv_obj_t *box;
	lv_obj_t *label;

	ask_layer = lv_obj_create(log_screen);
	lv_obj_remove_style_all(ask_layer);
	lv_obj_set_size(ask_layer, SCREEN_WIDTH, SCREEN_HEIGHT);
	lv_obj_align(ask_layer, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_set_style_bg_color(ask_layer, lv_color_hex(COLOUR_SCREEN), 0);
	lv_obj_set_style_bg_opa(ask_layer, ASK_DIMMED, 0);
	lv_obj_add_flag(ask_layer, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(ask_layer, LV_OBJ_FLAG_HIDDEN);

	box = box_create(ask_layer, (SCREEN_WIDTH - ASK_WIDTH) / 2,
		(SCREEN_HEIGHT - ASK_HEIGHT) / 2, ASK_WIDTH, ASK_HEIGHT);
	label = text_create(box, "Do you want to delete the entry?", &lv_font_montserrat_16,
		COLOUR_VALUE);
	lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 2 * PAD);
	ask_detail = text_create(box, "", &lv_font_montserrat_14, COLOUR_TITLE);
	lv_obj_align(ask_detail, LV_ALIGN_TOP_MID, 0, 5 * PAD);
	ask_yes = button_create(box, ASK_WIDTH / 2 - ASK_BUTTON_WIDTH - PAD,
		ASK_HEIGHT - ASK_BUTTON_HEIGHT - 2 * PAD, ASK_BUTTON_WIDTH, ASK_BUTTON_HEIGHT,
		"YES", on_ask_yes);
	ask_no = button_create(box, ASK_WIDTH / 2 + PAD,
		ASK_HEIGHT - ASK_BUTTON_HEIGHT - 2 * PAD, ASK_BUTTON_WIDTH, ASK_BUTTON_HEIGHT,
		"NO", on_ask_no);
}

//a touch on an entry marks it and asks whether it is to be deleted
static void on_log_touched(lv_event_t *e){
	uint32_t row;
	uint32_t column;

	(void)e;
	lv_table_get_selected_cell(log_table, &row, &column);
	if (row == LV_TABLE_CELL_NONE || row == 0 || log_top + (int)row - 1 >= log_count)
		return;
	log_mark_at(log_top + (int)row - 1);
	log_ask_to_delete();
}

static void on_back_from_log(lv_event_t *e){
	(void)e;
	screens_close();
}

//where a field's value is typed: a line of text that looks like the
//others on the screen, with a cursor while it is being typed into. A touch
//goes through it to the field's box, which starts the typing
static lv_obj_t *log_value_create(lv_obj_t *box, int width){
	lv_obj_t *value = lv_textarea_create(box);

	lv_textarea_set_one_line(value, true);
	lv_textarea_set_max_length(value, LOG_FIELD_MAX);
	lv_obj_set_width(value, width);
	lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(value, lv_color_hex(COLOUR_VALUE), 0);
	lv_obj_set_style_bg_opa(value, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(value, 0, 0);
	lv_obj_set_style_pad_all(value, 0, 0);
	lv_obj_set_style_radius(value, 0, 0);
	lv_obj_set_style_border_color(value, lv_color_hex(COLOUR_VALUE),
		LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_remove_flag(value, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_remove_flag(value, LV_OBJ_FLAG_SCROLLABLE);
	return value;
}

//the keyboard for the fields, over the list and under the row of fields,
//so that what is typed is seen where it goes
static void log_keyboard_create(){
	log_keyboard = lv_keyboard_create(log_screen);
	lv_obj_set_size(log_keyboard, SCREEN_WIDTH, SCREEN_HEIGHT - LOG_TABLE_TOP);
	lv_obj_align(log_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_keyboard_set_textarea(log_keyboard, NULL);
	lv_obj_add_event_cb(log_keyboard, on_log_keyboard_ready, LV_EVENT_READY, NULL);
	lv_obj_add_event_cb(log_keyboard, on_log_keyboard_cancel, LV_EVENT_CANCEL, NULL);
	lv_obj_add_flag(log_keyboard, LV_OBJ_FLAG_HIDDEN);
}

//the fields and the buttons, left to right, each its width and a gap:
//CALL, "?", SENT, RECV, EXCH, NR, WIPE, SAVE
static void log_fields_create(){
	lv_obj_t *box;
	lv_obj_t *title;
	int x = LOG_GAP;
	int y = BAR_HEIGHT + LOG_GAP;
	int i;

	for (i = 0; i < LOG_FIELDS; i++){
		box = box_create(log_screen, x, y, log_fields[i].width, LOG_FIELDS_HEIGHT);
		log_fields[i].box = box;
		lv_obj_add_event_cb(box, on_log_field_touched, LV_EVENT_CLICKED,
			(void *)(intptr_t)i);
		title = text_create(box, log_fields[i].control, &lv_font_montserrat_12, COLOUR_TITLE);
		lv_obj_align(title, LV_ALIGN_TOP_LEFT, LOG_GAP, LOG_FIELD_TITLE_TOP);
		log_fields[i].value = log_value_create(box, log_fields[i].width - 2 * LOG_GAP);
		lv_obj_align(log_fields[i].value, LV_ALIGN_BOTTOM_LEFT, LOG_GAP,
			LOG_FIELD_VALUE_BOTTOM);
		x += log_fields[i].width + LOG_GAP;
		//"?" searches for what is in CALL, so it is beside it, the author's
		//asking: the eye takes what is close together as belonging together
		if (i == LOG_CALL_FIELD){
			button_create(log_screen, x, y, LOG_FIND_WIDTH, LOG_FIELDS_HEIGHT, "?",
				on_log_find_touched);
			x += LOG_FIND_WIDTH + LOG_GAP;
		}
	}
	button_create(log_screen, x, y, LOG_WIPE_WIDTH, LOG_FIELDS_HEIGHT, "WIPE",
		on_log_wipe_touched);
	x += LOG_WIPE_WIDTH + LOG_GAP;
	button_create(log_screen, x, y, LOG_SAVE_WIDTH, LOG_FIELDS_HEIGHT, "SAVE",
		on_log_save_touched);
}

static void log_create(){
	lv_obj_t *label;
	int row;
	int i;

	log_screen = screen_create();
	label = text_create(log_screen, "LOGBOOK", &lv_font_montserrat_16, COLOUR_VALUE);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, TITLE_LEFT, (BAR_HEIGHT - 16) / 2);
	log_status = text_create(log_screen, "", &lv_font_montserrat_14, COLOUR_TITLE);
	lv_obj_set_width(log_status, LOG_STATUS_WIDTH);
	lv_label_set_long_mode(log_status, LV_LABEL_LONG_DOT);
	lv_obj_align(log_status, LV_ALIGN_TOP_LEFT, LOG_STATUS_LEFT, (BAR_HEIGHT - 14) / 2);
	back_button_create(log_screen, on_back_from_log);
	log_fields_create();

	log_table = lv_table_create(log_screen);
	lv_obj_set_size(log_table, SCREEN_WIDTH, SCREEN_HEIGHT - LOG_TABLE_TOP);
	lv_obj_align(log_table, LV_ALIGN_TOP_LEFT, 0, LOG_TABLE_TOP);
	lv_table_set_column_count(log_table, LOG_COLUMNS);
	lv_table_set_row_count(log_table, LOG_TABLE_ROWS);
	lv_obj_set_style_radius(log_table, 0, 0);
	lv_obj_set_style_border_width(log_table, 0, 0);
	lv_obj_set_style_pad_all(log_table, 0, 0);
	lv_obj_set_style_bg_color(log_table, lv_color_hex(COLOUR_SCREEN), 0);
	lv_obj_remove_flag(log_table, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(log_table, lv_color_hex(COLOUR_BOX), LV_PART_ITEMS);
	lv_obj_set_style_text_color(log_table, lv_color_hex(COLOUR_VALUE), LV_PART_ITEMS);
	lv_obj_set_style_text_font(log_table, &lv_font_montserrat_12, LV_PART_ITEMS);
	lv_obj_set_style_border_color(log_table, lv_color_hex(COLOUR_EDGE), LV_PART_ITEMS);
	lv_obj_set_style_border_width(log_table, 1, LV_PART_ITEMS);
	lv_obj_set_style_pad_ver(log_table, LOG_ROW_PAD, LV_PART_ITEMS);
	lv_obj_set_style_pad_hor(log_table, PAD / 2, LV_PART_ITEMS);
	//the headings are put in after the table has its font and its padding,
	//so that their row is no higher than the rest; and no cell wraps its
	//text onto a second line
	for (i = 0; i < LOG_COLUMNS; i++){
		lv_table_set_column_width(log_table, i, log_widths[i]);
		lv_table_set_cell_value(log_table, 0, i, log_headings[i]);
		for (row = 0; row < LOG_TABLE_ROWS; row++)
			lv_table_set_cell_ctrl(log_table, row, i, LV_TABLE_CELL_CTRL_TEXT_CROP);
	}
	lv_obj_add_event_cb(log_table, on_log_drawn, LV_EVENT_DRAW_TASK_ADDED, NULL);
	lv_obj_add_flag(log_table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
	lv_obj_add_event_cb(log_table, on_log_touched, LV_EVENT_VALUE_CHANGED, NULL);
	log_keyboard_create();
	ask_create();
}

/* the date and the time */

static bool is_leap(int year){
	return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_year(int year){
	if (is_leap(year))
		return DAYS_IN_A_LEAP_YEAR;
	return DAYS_IN_A_YEAR;
}

static int days_in(int year, int month){
	static const int days[MONTHS] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (month == 2 && is_leap(year))
		return 29;
	return days[month - 1];
}

//what a roller is at, as its number: the year, the day of the month
static int roller_value(int which){
	return roller_places[which].first + (int)lv_roller_get_selected(rollers[which]);
}

static void roller_set(int which, int value){
	int index = value - roller_places[which].first;

	if (index < 0)
		index = 0;
	if (index > roller_places[which].count - 1)
		index = roller_places[which].count - 1;
	lv_roller_set_selected(rollers[which], index, LV_ANIM_OFF);
}

static void date_show(){
	char text[WIFI_SHOWN_MAX];
	char clock[CONTROL_TEXT_ROOM];
	int i;

	text_of("CLOCK", clock, sizeof(clock));
	if (controls_find("CLOCK") == NULL)
		snprintf(text, sizeof(text), "this radio does not keep a clock of its own");
	else
		snprintf(text, sizeof(text), "The radio's clock (UTC): %s", clock);
	lv_label_set_text(date_status, text);
	for (i = 0; i < ROLLERS; i++)
		show_mark(rollers[i], i == roller_marked);
}

//the rollers are put at the radio's time, if it has one
static void rollers_to_the_radios_time(){
	char clock[CONTROL_TEXT_ROOM];
	int year, month, day, hour, minute;

	text_of("CLOCK", clock, sizeof(clock));
	if (sscanf(clock, "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) != 5)
		return;
	roller_set(YEAR, year);
	roller_set(MONTH, month);
	roller_set(DAY, day);
	roller_set(HOUR, hour);
	roller_set(MINUTE, minute);
}

//the minutes from the start of FIRST_YEAR to what the rollers are at; a day
//the month does not have is taken as its last
static int32_t minutes_on_the_rollers(){
	int year = roller_value(YEAR);
	int month = roller_value(MONTH);
	int day = roller_value(DAY);
	int32_t days = 0;
	int y;
	int m;

	if (day > days_in(year, month))
		day = days_in(year, month);
	for (y = FIRST_YEAR; y < year; y++)
		days += days_in_year(y);
	for (m = 1; m < month; m++)
		days += days_in(year, m);
	days += day - 1;
	return (days * HOURS_A_DAY + roller_value(HOUR)) * MINUTES_AN_HOUR
		+ roller_value(MINUTE);
}

//SYNC: the time on the rollers goes to the radio, as of this moment
static void on_date_sync(lv_event_t *e){
	struct panel_control *time_set = controls_find("TIME_SET");
	int32_t minutes = minutes_on_the_rollers();

	(void)e;
	if (time_set == NULL)
		return;
	time_set->value = minutes;
	Serial.printf("TIME_SET %d\n", (int)minutes);
	i2c_link_changed(time_set->d.number, minutes);
}

static void on_roller_touched(lv_event_t *e){
	roller_marked = (int)(intptr_t)lv_event_get_user_data(e);
	date_show();
}

static void on_back_from_date(lv_event_t *e){
	(void)e;
	lv_obj_add_flag(date_screen, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(settings_screen, LV_OBJ_FLAG_HIDDEN);
}

//a roller's choices: its numbers, one to a line
static void roller_fill(int which){
	static char options[ROLLER_OPTIONS_MAX];
	const struct roller_place *place = &roller_places[which];
	int used = 0;
	int i;

	for (i = 0; i < place->count; i++){
		if (i > 0)
			options[used++] = '\n';
		used += snprintf(options + used, sizeof(options) - used, "%02d", place->first + i);
	}
	lv_roller_set_options(rollers[which], options, LV_ROLLER_MODE_NORMAL);
}

static void date_create(){
	lv_obj_t *label;
	int i;

	date_screen = screen_create();
	label = text_create(date_screen, "DATE AND TIME, IN UTC", &lv_font_montserrat_16,
		COLOUR_VALUE);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, TITLE_LEFT, (BAR_HEIGHT - 16) / 2);
	back_button_create(date_screen, on_back_from_date);
	button_create(date_screen, SCREEN_WIDTH - BUTTON_WIDTH, 0, BUTTON_WIDTH, BAR_HEIGHT,
		LV_SYMBOL_OK "  SYNC", on_date_sync);

	for (i = 0; i < ROLLERS; i++){
		label = text_create(date_screen, roller_places[i].label, &lv_font_montserrat_14,
			COLOUR_TITLE);
		lv_obj_align(label, LV_ALIGN_TOP_LEFT, roller_places[i].x, ROLLER_LABEL_TOP);
		rollers[i] = lv_roller_create(date_screen);
		roller_fill(i);
		lv_roller_set_visible_row_count(rollers[i], ROLLER_ROWS);
		lv_obj_set_width(rollers[i], roller_places[i].w);
		lv_obj_align(rollers[i], LV_ALIGN_TOP_LEFT, roller_places[i].x, ROLLER_TOP);
		lv_obj_set_style_text_font(rollers[i], &lv_font_montserrat_20, 0);
		lv_obj_set_style_text_font(rollers[i], &lv_font_montserrat_20, LV_PART_SELECTED);
		lv_obj_add_event_cb(rollers[i], on_roller_touched, LV_EVENT_PRESSED,
			(void *)(intptr_t)i);
	}
	date_status = text_create(date_screen, "", &lv_font_montserrat_14, COLOUR_TITLE);
	lv_obj_align(date_status, LV_ALIGN_TOP_LEFT, PAD, DATE_STATUS_TOP);
}

static void date_open(){
	if (date_screen == NULL)
		date_create();
	rollers_to_the_radios_time();
	date_show();
	lv_obj_add_flag(settings_screen, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(date_screen, LV_OBJ_FLAG_HIDDEN);
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
		if (wifi_points[i].is_secured)
			snprintf(text, sizeof(text), "%d dBm", wifi_points[i].signal_dbm);
		else
			snprintf(text, sizeof(text), "open  %d dBm", wifi_points[i].signal_dbm);
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
		LINK_WIFI_KEY_MAX, false, "JOIN", wifi_send_choice);
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

//a part of the radio's list of entries. This is called from the link's
//interrupt, so it only keeps the lines; screens_slice() shows them when the
//part marked last has come
void screens_log_part(const uint8_t *bytes, int count){
	int at = LINK_LOG_PART_HEADER;
	int length;

	if (count < LINK_LOG_PART_HEADER)
		return;
	if (bytes[0] == 0){
		log_count = 0;
		log_is_whole = false;
	}
	while (at < count && log_count < LINK_LOG_LINES_MAX){
		length = 0;
		while (at + length < count && bytes[at + length] != 0)
			length++;
		if (at + length >= count)
			break;
		if (length > LINK_LOG_LINE_MAX)
			length = LINK_LOG_LINE_MAX;
		memcpy(log_lines[log_count], bytes + at, length);
		log_lines[log_count][length] = 0;
		log_count = log_count + 1;
		while (bytes[at] != 0)
			at++;
		at++;
	}
	if (bytes[1]){
		log_is_whole = true;
		log_is_to_show = true;
	}
}

//the logbook is opened searching for nothing, with the newest entries of
//every station: what was searched for the last time it was open is not kept
void screens_open_log(){
	if (log_screen == NULL)
		log_create();
	waterfall_set_painting(false);
	main_hide();
	lv_obj_remove_flag(log_screen, LV_OBJ_FLAG_HIDDEN);
	log_fields_show();
	log_search_done("");
}

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
	if (is_up(log_screen) && is_up(ask_layer)){
		ask_is_on_yes = !ask_is_on_yes;
		ask_show();
	}
	else if (is_up(log_screen) && !is_up(log_keyboard))
		log_mark_at(log_mark + steps);
	if (is_up(date_screen)){
		roller_set(roller_marked, roller_value(roller_marked) + steps);
		date_show();
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
	if (is_up(log_screen) && is_up(ask_layer))
		ask_answered(ask_is_on_yes);
	else if (is_up(log_screen) && !is_up(log_keyboard))
		log_ask_to_delete();
	else if (is_up(date_screen)){
		roller_marked = (roller_marked + 1) % ROLLERS;
		date_show();
	}
	else if (is_up(wifi_screen))
		wifi_choose(wifi_mark);
	else if (is_up(settings_screen) && settings[settings_mark].open != NULL)
		settings[settings_mark].open();
}

//what the radio says for the status bar is shown on the WiFi screen too,
//where it says whether the access point was joined
void screens_status(const char *text){
	if (log_screen != NULL){
		snprintf(log_said, sizeof(log_said), "%s", text);
		log_is_to_show = true;
	}
	if (wifi_screen == NULL)
		return;
	snprintf(wifi_said, sizeof(wifi_said), "%s", text);
	wifi_is_to_show = true;
}

//from loop(): what has changed on a screen that is up is shown
void screens_slice(){
	bootsel_slice();
	if (wifi_is_to_show && wifi_screen != NULL)
		wifi_show();
	if (log_is_to_show && log_screen != NULL)
		log_show();
	if (log_is_save_waiting && !i2c_link_is_sending_text()){
		log_is_save_waiting = false;
		send_action("LOGGER", "SAVE");
	}
}

//what typing a text and saving it would do, for trying it from the serial
//port: "MYCALLSIGN VU2ESE", or "LOG VU2" for a search of the logbook
void screens_set_text(char *control_and_text){
	char *text = strchr(control_and_text, ' ');

	if (text == NULL)
		return;
	*text++ = 0;
	if (!strcmp(control_and_text, "LOG") && log_screen != NULL){
		log_search_done(text);
		return;
	}
	set_text(control_and_text, text);
}

//what a touch on a row of the settings, on the logbook's buttons, or on
//BACK, would do, by name
void screens_select(const char *name){
	int i;

	if (!strcmp(name, "BACK"))
		screens_close();
	if (log_screen != NULL && !lv_obj_has_flag(log_screen, LV_OBJ_FLAG_HIDDEN)){
		if (!strcmp(name, "?"))
			log_find();
		if (!strcasecmp(name, "WIPE"))
			log_wipe();
		if (!strcasecmp(name, "SAVE"))
			log_save();
		for (i = 0; i < LOG_FIELDS; i++){
			if (!strcasecmp(name, log_fields[i].control))
				log_editing_begin(i);
		}
	}
	if (settings_screen == NULL || lv_obj_has_flag(settings_screen, LV_OBJ_FLAG_HIDDEN))
		return;
	for (i = 0; i < SETTINGS; i++){
		if (!strcasecmp(name, settings[i].label) && settings[i].open != NULL){
			settings_mark = i;
			settings[i].open();
		}
	}
}

bool screens_is_open(){
	if (settings_screen != NULL && !lv_obj_has_flag(settings_screen, LV_OBJ_FLAG_HIDDEN))
		return true;
	if (wifi_screen != NULL && !lv_obj_has_flag(wifi_screen, LV_OBJ_FLAG_HIDDEN))
		return true;
	if (date_screen != NULL && !lv_obj_has_flag(date_screen, LV_OBJ_FLAG_HIDDEN))
		return true;
	if (log_screen != NULL && !lv_obj_has_flag(log_screen, LV_OBJ_FLAG_HIDDEN))
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
	if (date_screen != NULL)
		lv_obj_add_flag(date_screen, LV_OBJ_FLAG_HIDDEN);
	if (log_screen != NULL){
		log_editing_end();
		lv_obj_add_flag(ask_layer, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(log_screen, LV_OBJ_FLAG_HIDDEN);
	}
	input_done = NULL;
	if (settings_screen != NULL)
		lv_obj_add_flag(settings_screen, LV_OBJ_FLAG_HIDDEN);
	main_ui();
}

//the radio has said something: the settings and the logbook's fields
//show it if they are up
void screens_radio_said(){
	if (log_screen != NULL && !lv_obj_has_flag(log_screen, LV_OBJ_FLAG_HIDDEN))
		log_fields_show();
	if (date_screen != NULL && !lv_obj_has_flag(date_screen, LV_OBJ_FLAG_HIDDEN))
		date_show();
	if (settings_screen != NULL && !lv_obj_has_flag(settings_screen, LV_OBJ_FLAG_HIDDEN))
		settings_show();
}
