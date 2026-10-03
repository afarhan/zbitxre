// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// messages.cpp — the list of the messages the radio has copied, FT8's,
// which in FT8 is where the spectrum and the waterfall are in other modes.
//
// The author's plan: in FT8 the messages are in a list box that takes the
// place of the spectrum and the waterfall. main.cpp shows it and stops the
// waterfall's painting when the mode is FT8, and puts both back when it is
// not. In FT8 the bottom row has nothing on it and the status line, which
// shows how the contact stands, is above the list; so the list is the
// waterfall's 288 wide and runs from under the status line to the bottom
// of the screen, 208 high: MESSAGES_TOP and MESSAGES_HEIGHT.
//
// A message is a row:
//
//     45  -12  1797  CQ VU2ESE MK97
//
// the second of the minute its slot began at, 00, 15, 30 or 45; how strong
// it was, in dB over the noise; its pitch in the passband, in Hz; and its
// text. Thirteen rows are in view, the newest at the bottom, and the list moves
// up as more come, as a terminal does. The rows of one slot are told from
// those of the next by their ground, which is a little lighter for every
// other slot, and a station calling CQ is in green.
//
// The newest MESSAGES_MAX are kept, sixty: two or three slots of a busy
// band.
//
// A MESSAGE IS SELECTED
//
// The author asked for a way to select a message. A touch on a row marks
// that message, and selects the list for the knob; the knob then moves
// the mark, a message to a step, and the list moves to keep the mark in
// view, back through all that is kept. With no message marked, the first
// step of the knob marks the newest. A click of the knob takes the mark
// off and the list goes back to the newest.
//
// While a message is marked the list stays where it is as more come, so
// that the marked one does not run off under the finger. As on the panel
// zbitxv2 had, a mark that has not been touched for thirty seconds is
// taken off, and the list follows the newest again. So is one whose
// message has been pushed out by newer ones.
//
// A click of the knob on a marked message answers it: the message goes
// back to the radio, as the line it came as, with LINK_FT8_ANSWER, and the
// mark is taken off. What may be answered, and what follows, is the
// radio's business, ft8_qso.c. With nothing marked, a click goes to the
// newest, as before.
//
// What the radio itself sends is in the list too, as WSJT-X shows it: TX
// where the strength would be, and in red.
//
// THE BAR AT THE RIGHT
//
// When more messages are kept than the thirteen in view, a bar at the right
// edge shows it: its length is the part of the kept messages that is in
// view, and its place is where in them the view is, as a scroll bar's.
// With thirteen or fewer there is no bar.
//
// FROM THE RADIO
//
// The radio sends the messages as it copies them, as lines of text, a few
// to a record; link.c says how and panel_messages.c on the radio what is
// in a line. messages_part() is called from the link's interrupt and only
// keeps the lines; messages_slice(), from loop(), shows them.
//
// As everywhere on this panel, a cell is given its text only when the text
// is new, so that LVGL paints no more than has changed; main.cpp says why
// that matters.

#include <Arduino.h>
#include <lvgl.h>
#include "link.h"
#include "waterfall.h"
#include "ui.h"

#define MESSAGES_MAX 60
#define TEXT_ROOM 26
#define MESSAGES_TOP (WATERFALL_TOP + MESSAGES_STATUS_HEIGHT)
#define MESSAGES_HEIGHT (SCREEN_BOTTOM - MESSAGES_TOP)
#define ROWS 13
#define ROW_HEIGHT (MESSAGES_HEIGHT / ROWS)
#define COLUMNS 4
#define SLOT_COLUMN 0
#define SNR_COLUMN 1
#define PITCH_COLUMN 2
#define TEXT_COLUMN 3
#define CELL_ROOM 8
#define SLOT_SECONDS 15
#define SECONDS_IN_MINUTE 60
#define SECOND_DIGITS 2
#define TIME_DIGITS 6
#define FIELD_BETWEEN '|'
#define CALLING "CQ "
#define NO_LINE (-1L)
#define MARK_KEPT_FOR_MS 30000

#define COLOUR_BOX 0x101820
#define COLOUR_OTHER_SLOT 0x1C2834
#define COLOUR_MARKED_ROW 0x604800
#define COLOUR_VALUE 0xFFFFFF
#define COLOUR_CALLING 0x60E060
#define COLOUR_SENT 0xFF6060
#define SENT "TX"
#define ANSWER_ROOM 48
#define COLOUR_SELECTED 0xFFC000
#define COLOUR_BAR 0x90A0B0
#define SELECTED_WIDTH 2
#define CELL_PAD 3
#define BAR_WIDTH 4
#define BAR_SHORTEST 8

struct line {
	uint8_t slot_second;
	int8_t snr_db;
	int16_t pitch_hz;
	bool is_sent;
	char text[TEXT_ROOM];
};

extern lv_obj_t *main_screen;

static const int16_t widths[COLUMNS] = { 26, 34, 42, 186 - BAR_WIDTH };

static struct line lines[MESSAGES_MAX];
static volatile unsigned long added;
static volatile bool is_new;
static unsigned long shown_up_to;
static long scrolled_back;
static long marked = NO_LINE;
static unsigned long marked_at_ms;
static long row_line[ROWS];
static int bar_top = NO_LINE;
static int bar_height;
static lv_obj_t *table;
static lv_obj_t *frame;
static lv_obj_t *bar;
static bool is_shown;
static bool is_selected;

/* keeping what the radio sends */

static void keep_a_line(const char *sent, int length){
	struct line *l = &lines[added % MESSAGES_MAX];
	char whole[LINK_MESSAGE_LINE_MAX + 1];
	char *snr;
	char *pitch;
	char *text;

	if (length > LINK_MESSAGE_LINE_MAX)
		length = LINK_MESSAGE_LINE_MAX;
	memcpy(whole, sent, length);
	whole[length] = 0;
	snr = strchr(whole, FIELD_BETWEEN);
	if (snr == NULL || snr - whole != TIME_DIGITS)
		return;
	pitch = strchr(snr + 1, FIELD_BETWEEN);
	if (pitch == NULL)
		return;
	text = strchr(pitch + 1, FIELD_BETWEEN);
	if (text == NULL)
		return;
	l->slot_second = (uint8_t)(atoi(whole + TIME_DIGITS - SECOND_DIGITS) % SECONDS_IN_MINUTE);
	l->is_sent = !strncmp(snr + 1, SENT, strlen(SENT));
	l->snr_db = (int8_t)atoi(snr + 1);
	l->pitch_hz = (int16_t)atoi(pitch + 1);
	snprintf(l->text, sizeof(l->text), "%s", text + 1);
	added = added + 1;
}

//a record of the radio's lines, each ended by a zero. This is called from
//the link's interrupt
void messages_part(const uint8_t *bytes, int count){
	int at = 0;
	int length;

	while (at < count){
		length = 0;
		while (at + length < count && bytes[at + length] != 0)
			length++;
		if (at + length >= count)
			break;
		keep_a_line((const char *)bytes + at, length);
		at += length + 1;
	}
	is_new = true;
}

/* showing them */

static void cell_show(int row, int column, const char *text){
	if (strcmp(lv_table_get_cell_value(table, row, column), text) != 0)
		lv_table_set_cell_value(table, row, column, text);
}

static void row_show(int row, long which){
	const struct line *l;
	char cell[CELL_ROOM];

	if (row_line[row] == which)
		return;
	row_line[row] = which;
	if (which == NO_LINE){
		cell_show(row, SLOT_COLUMN, "");
		cell_show(row, SNR_COLUMN, "");
		cell_show(row, PITCH_COLUMN, "");
		cell_show(row, TEXT_COLUMN, "");
		return;
	}
	l = &lines[which % MESSAGES_MAX];
	snprintf(cell, sizeof(cell), "%02d", l->slot_second);
	cell_show(row, SLOT_COLUMN, cell);
	snprintf(cell, sizeof(cell), "%d", l->snr_db);
	if (l->is_sent)
		snprintf(cell, sizeof(cell), SENT);
	cell_show(row, SNR_COLUMN, cell);
	snprintf(cell, sizeof(cell), "%d", l->pitch_hz);
	cell_show(row, PITCH_COLUMN, cell);
	cell_show(row, TEXT_COLUMN, l->text);
}

//the bar at the right: as long as the part of what is kept that is in
//view, and placed where in it the view is. It is moved only when its place
//or its length is new
static void bar_show(long kept, long first_in_view){
	int height = MESSAGES_HEIGHT;
	int top = 0;

	if (kept <= ROWS){
		if (bar_top != NO_LINE)
			lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
		bar_top = NO_LINE;
		return;
	}
	height = (int)(MESSAGES_HEIGHT * ROWS / kept);
	if (height < BAR_SHORTEST)
		height = BAR_SHORTEST;
	top = (int)((MESSAGES_HEIGHT - height) * first_in_view / (kept - ROWS));
	if (top == bar_top && height == bar_height)
		return;
	if (bar_top == NO_LINE)
		lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
	bar_top = top;
	bar_height = height;
	lv_obj_set_size(bar, BAR_WIDTH, height);
	lv_obj_align(bar, LV_ALIGN_TOP_LEFT, WATERFALL_LEFT + WATERFALL_WIDTH - BAR_WIDTH,
		MESSAGES_TOP + top);
}

//the rows are filled from what is kept: the newest at the bottom, or as far
//back from it as the list has been moved. With fewer messages than rows,
//they are from the top down
static void show(){
	unsigned long total = added;
	long kept = (long)total;
	long oldest;
	long after_last;
	long first;
	long furthest_back;
	int row;

	if (kept > MESSAGES_MAX)
		kept = MESSAGES_MAX;
	oldest = (long)total - kept;
	if (marked != NO_LINE && marked < oldest){
		marked = NO_LINE;
		scrolled_back = 0;
		lv_obj_invalidate(table);
	}
	if (scrolled_back > 0 || marked != NO_LINE)
		scrolled_back += (long)(total - shown_up_to);
	shown_up_to = total;
	furthest_back = kept - ROWS;
	if (furthest_back < 0)
		furthest_back = 0;
	if (scrolled_back > furthest_back)
		scrolled_back = furthest_back;
	if (scrolled_back < 0)
		scrolled_back = 0;
	after_last = (long)total - scrolled_back;
	first = after_last - ROWS;
	if (first < oldest)
		first = oldest;
	for (row = 0; row < ROWS; row++){
		if (first + row < after_last)
			row_show(row, first + row);
		else
			row_show(row, NO_LINE);
	}
	bar_show(kept, first - oldest);
}

//the mark is put on a message, or taken off with NO_LINE, and the list is
//moved, if it must be, to have the marked message in view
static void mark(long which){
	long total;
	long first;

	if (is_new){
		is_new = false;
		show();
	}
	total = (long)shown_up_to;
	marked = which;
	marked_at_ms = millis();
	if (marked == NO_LINE)
		scrolled_back = 0;
	else {
		first = total - scrolled_back - ROWS;
		if (marked < first)
			scrolled_back = total - ROWS - marked;
		if (marked > first + ROWS - 1)
			scrolled_back = total - 1 - marked;
	}
	lv_obj_invalidate(table);
	show();
}

//each row is given its ground: the marked one's, or its slot's; and a CQ
//its green, as the table is drawn
static void on_drawn(lv_event_t *e){
	lv_draw_task_t *task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
	lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
	lv_draw_label_dsc_t *label = lv_draw_task_get_label_dsc(task);
	const struct line *l;

	if (base == NULL || base->part != LV_PART_ITEMS || base->id1 >= ROWS)
		return;
	if (row_line[base->id1] == NO_LINE)
		return;
	l = &lines[row_line[base->id1] % MESSAGES_MAX];
	if (fill != NULL && row_line[base->id1] == marked)
		fill->color = lv_color_hex(COLOUR_MARKED_ROW);
	else if (fill != NULL && (l->slot_second / SLOT_SECONDS) % 2 == 1)
		fill->color = lv_color_hex(COLOUR_OTHER_SLOT);
	if (label != NULL && l->is_sent)
		label->color = lv_color_hex(COLOUR_SENT);
	else if (label != NULL && !strncmp(l->text, CALLING, strlen(CALLING)))
		label->color = lv_color_hex(COLOUR_CALLING);
}

//a touch on the list selects it for the knob, and marks the message that
//was touched, if the touch was on one
static void on_touched(lv_event_t *e){
	uint32_t row = LV_TABLE_CELL_NONE;
	uint32_t column = LV_TABLE_CELL_NONE;

	(void)e;
	lv_table_get_selected_cell(table, &row, &column);
	main_select("MESSAGES");
	if (row < ROWS && row_line[row] != NO_LINE)
		mark(row_line[row]);
}

/* what the rest of the panel calls */

void messages_create(){
	int row;
	int column;

	table = lv_table_create(main_screen);
	lv_obj_set_size(table, WATERFALL_WIDTH, MESSAGES_HEIGHT);
	lv_obj_align(table, LV_ALIGN_TOP_LEFT, WATERFALL_LEFT, MESSAGES_TOP);
	lv_table_set_column_count(table, COLUMNS);
	lv_table_set_row_count(table, ROWS);
	lv_obj_set_style_radius(table, 0, 0);
	lv_obj_set_style_border_width(table, 0, 0);
	lv_obj_set_style_pad_all(table, 0, 0);
	lv_obj_set_style_bg_color(table, lv_color_hex(COLOUR_BOX), 0);
	lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(table, lv_color_hex(COLOUR_BOX), LV_PART_ITEMS);
	lv_obj_set_style_text_color(table, lv_color_hex(COLOUR_VALUE), LV_PART_ITEMS);
	lv_obj_set_style_text_font(table, &lv_font_montserrat_14, LV_PART_ITEMS);
	lv_obj_set_style_border_width(table, 0, LV_PART_ITEMS);
	lv_obj_set_style_pad_ver(table, 0, LV_PART_ITEMS);
	lv_obj_set_style_pad_hor(table, CELL_PAD, LV_PART_ITEMS);
	for (column = 0; column < COLUMNS; column++){
		lv_table_set_column_width(table, column, widths[column]);
		for (row = 0; row < ROWS; row++){
			lv_table_set_cell_value(table, row, column, "");
			lv_table_set_cell_ctrl(table, row, column, LV_TABLE_CELL_CTRL_TEXT_CROP);
		}
	}
	for (row = 0; row < ROWS; row++)
		row_line[row] = NO_LINE;
	lv_obj_add_event_cb(table, on_drawn, LV_EVENT_DRAW_TASK_ADDED, NULL);
	lv_obj_add_flag(table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
	lv_obj_add_event_cb(table, on_touched, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_add_flag(table, LV_OBJ_FLAG_HIDDEN);

	bar = lv_obj_create(main_screen);
	lv_obj_remove_style_all(bar);
	lv_obj_set_style_bg_color(bar, lv_color_hex(COLOUR_BAR), 0);
	lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
	lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);

	frame = lv_obj_create(main_screen);
	lv_obj_remove_style_all(frame);
	lv_obj_set_size(frame, WATERFALL_WIDTH, MESSAGES_HEIGHT);
	lv_obj_align(frame, LV_ALIGN_TOP_LEFT, WATERFALL_LEFT, MESSAGES_TOP);
	lv_obj_set_style_border_color(frame, lv_color_hex(COLOUR_SELECTED), 0);
	lv_obj_set_style_border_width(frame, SELECTED_WIDTH, 0);
	lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(frame, LV_OBJ_FLAG_HIDDEN);
}

//from loop(): what has come since the list was last shown is shown, and a
//mark left alone for long enough is taken off
void messages_slice(){
	if (table == NULL)
		return;
	if (is_shown && marked != NO_LINE && millis() - marked_at_ms > MARK_KEPT_FOR_MS)
		mark(NO_LINE);
	if (!is_new)
		return;
	is_new = false;
	if (is_shown)
		show();
}

void messages_set_shown(bool shown){
	if (table == NULL || is_shown == shown)
		return;
	is_shown = shown;
	if (shown){
		marked = NO_LINE;
		scrolled_back = 0;
		lv_obj_remove_flag(table, LV_OBJ_FLAG_HIDDEN);
		show();
	}
	else {
		lv_obj_add_flag(table, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
		bar_top = NO_LINE;
		messages_set_selected(false);
	}
}

//the list has the bright edge while it is what the knob moves. The edge is
//a frame of its own laid over the list: a border on the table would take
//two pixels from its rows, and the tenth would not fit
void messages_set_selected(bool selected){
	if (table == NULL)
		return;
	selected = selected && is_shown;
	if (is_selected == selected)
		return;
	is_selected = selected;
	if (selected)
		lv_obj_remove_flag(frame, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_add_flag(frame, LV_OBJ_FLAG_HIDDEN);
}

//the knob: the mark goes a message back for each step back and a message
//on for each step on, no further than the oldest kept or the newest. With
//nothing marked, the newest is
void messages_scroll(int steps){
	long total;
	long oldest;
	long which = marked + steps;

	if (!is_shown)
		return;
	if (is_new){
		is_new = false;
		show();
	}
	total = (long)shown_up_to;
	oldest = total - MESSAGES_MAX;
	if (total == 0)
		return;
	if (oldest < 0)
		oldest = 0;
	if (marked == NO_LINE)
		which = total - 1;
	if (which < oldest)
		which = oldest;
	if (which > total - 1)
		which = total - 1;
	mark(which);
}

//a click of the knob: the mark is taken off and the newest are in view
void messages_show_newest(){
	if (is_shown)
		mark(NO_LINE);
}

//a click of the knob on a marked message: it is sent to the radio to be
//answered, and the mark is taken off. Says whether there was one to send
bool messages_answer_marked(){
	const struct line *l;
	char line[ANSWER_ROOM];

	if (!is_shown || marked == NO_LINE)
		return false;
	l = &lines[marked % MESSAGES_MAX];
	if (l->is_sent)
		return false;
	snprintf(line, sizeof(line), "%02d|%d|%d|%s", l->slot_second, l->snr_db, l->pitch_hz,
		l->text);
	Serial.printf("ANSWER %s\n", line);
	i2c_link_send_text(LINK_FT8_ANSWER, line);
	mark(NO_LINE);
	return true;
}
