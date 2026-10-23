// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// bins.cpp — JTTY's bins, which in JTTY are where the spectrum and the
// waterfall are in other modes, as FT8's messages are in FT8.
//
// The radio cuts JTTY's passband, 200 to 3200 Hz, into 24 bins of 125 Hz
// and keeps what has been read in each; modems/modem_jtty.c on the radio
// says what a bin is. Here a bin is a row, the lowest at the top, as the
// window and the web page have them:
//
//     1505  VU2ESE DE SBITX THE BINS ARE ON THE SCREEN NOW 73 |
//
// the pitch of the station last heard in it, or where the bin begins if
// none has been, dimmed; and the newest of its text, as much of its end as
// fits. They have the waterfall's place and no more, so that the bottom
// row keeps JTTY's macros, data/jtty.mc's, as CW's row keeps CW's: ten
// rows are in view, and a bar at the right edge says where in the 24 the
// view is, as the messages' does.
//
// The chosen bin is the one the radio listens to, JTTY_RX, and its row is
// lit. A touch on a row chooses it: JTTY_RX is put at the pitch of the
// station heard there, or, in an empty bin, a step above where it begins;
// the radio keeps it to its step of 25 Hz. The touch also selects the
// list for the knob, as the tap, turn and press rule has it, and the knob
// then chooses the next bin up or down, the view following it. The chosen
// bin's text runs along the status line as CW's does: the radio sends it
// so, panel_cw.c on the radio says how.
//
// FROM THE RADIO
//
// The radio sends each bin that has changed as a line of text, the bin,
// its pitch and its text with a | between them, a few to a record, and all
// of them again when the panel says HELLO; panel_jtty.c on the radio says
// how. bins_part() is called from the link's interrupt and only keeps the
// lines; bins_slice(), from loop(), shows them.
//
// As everywhere on this panel, a cell is given its text only when the text
// is new, so that LVGL paints no more than has changed.

#include <Arduino.h>
#include <lvgl.h>
#include "link.h"
#include "waterfall.h"
#include "controls.h"
#include "ui.h"

#define BINS 24
#define BIN_LOWEST_HZ 200
#define BIN_WIDTH_HZ 125
#define IN_BIN_HZ 25
#define BIN_TEXT_ROOM 81
#define BINS_TOP WATERFALL_TOP
#define BINS_HEIGHT WATERFALL_HEIGHT
#define ROWS 10
#define COLUMNS 2
#define HZ_COLUMN 0
#define TEXT_COLUMN 1
#define CELL_ROOM 8
// as much of a bin's text as a row shows, from its end
#define TEXT_SHOWN 30
#define FIELD_BETWEEN '|'
#define RX_CONTROL "JTTY_RX"
#define NO_BIN (-1)

#define COLOUR_BOX 0x101820
#define COLOUR_OTHER_ROW 0x1C2834
#define COLOUR_CHOSEN_ROW 0x604800
#define COLOUR_VALUE 0xFFFFFF
#define COLOUR_HZ 0x00FFFF
#define COLOUR_EMPTY_HZ 0x4A5A68
#define COLOUR_SELECTED 0xFFC000
#define COLOUR_BAR 0x90A0B0
#define SELECTED_WIDTH 2
#define CELL_PAD 3
#define BAR_WIDTH 4

struct bin {
	int16_t hz;
	char text[BIN_TEXT_ROOM];
};

extern lv_obj_t *main_screen;

static const int16_t widths[COLUMNS] = { 42, 246 - BAR_WIDTH };

static struct bin bins[BINS];
static volatile bool is_new;
static int first_row;
static int shown_chosen = NO_BIN;
static int row_bin[ROWS];
static lv_obj_t *table;
static lv_obj_t *frame;
static lv_obj_t *bar;
static bool is_shown;
static bool is_selected;

/* keeping what the radio sends */

static void keep_a_line(const char *sent, int length){
	char whole[LINK_JTTY_LINE_MAX + 1];
	char *hz;
	char *text;
	int which;

	if (length > LINK_JTTY_LINE_MAX)
		length = LINK_JTTY_LINE_MAX;
	memcpy(whole, sent, length);
	whole[length] = 0;
	hz = strchr(whole, FIELD_BETWEEN);
	if (hz == NULL)
		return;
	text = strchr(hz + 1, FIELD_BETWEEN);
	if (text == NULL)
		return;
	which = atoi(whole);
	if (which < 0 || which >= BINS)
		return;
	bins[which].hz = (int16_t)atoi(hz + 1);
	snprintf(bins[which].text, sizeof(bins[which].text), "%s", text + 1);
}

//a record of the radio's lines, each ended by a zero. This is called from
//the link's interrupt
void bins_part(const uint8_t *bytes, int count){
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

/* the chosen bin */

static int bin_of(int hz){
	int which = (hz - BIN_LOWEST_HZ) / BIN_WIDTH_HZ;

	if (which < 0)
		which = 0;
	if (which > BINS - 1)
		which = BINS - 1;
	return which;
}

static int chosen_bin(){
	struct panel_control *rx = controls_find(RX_CONTROL);

	if (rx == NULL)
		return NO_BIN;
	return bin_of(rx->value);
}

//JTTY_RX to the station heard in the bin, or a step into an empty one
static void choose(int which){
	int hz = BIN_LOWEST_HZ + which * BIN_WIDTH_HZ + IN_BIN_HZ;

	if (bins[which].text[0] != 0 && bins[which].hz > 0)
		hz = bins[which].hz;
	main_set_number(RX_CONTROL, hz);
}

/* showing them */

static void cell_show(int row, int column, const char *text){
	if (strcmp(lv_table_get_cell_value(table, row, column), text) != 0)
		lv_table_set_cell_value(table, row, column, text);
}

static void row_show(int row, int which){
	const struct bin *b = &bins[which];
	char cell[CELL_ROOM];
	const char *text = b->text;
	size_t length = strlen(text);

	row_bin[row] = which;
	if (b->text[0] != 0 && b->hz > 0)
		snprintf(cell, sizeof(cell), "%d", b->hz);
	else
		snprintf(cell, sizeof(cell), "%d", BIN_LOWEST_HZ + which * BIN_WIDTH_HZ);
	cell_show(row, HZ_COLUMN, cell);
	if (length > TEXT_SHOWN)
		text += length - TEXT_SHOWN;
	cell_show(row, TEXT_COLUMN, text);
}

//the bar at the right: as long as the part of the bins in view, and placed
//where in them the view is
static void bar_show(){
	int height = BINS_HEIGHT * ROWS / BINS;
	int top = (BINS_HEIGHT - height) * first_row / (BINS - ROWS);

	lv_obj_set_size(bar, BAR_WIDTH, height);
	lv_obj_align(bar, LV_ALIGN_TOP_LEFT, WATERFALL_LEFT + WATERFALL_WIDTH - BAR_WIDTH,
		BINS_TOP + top);
}

//the view is moved, if it must be, to have the chosen bin in it
static void keep_in_view(int which){
	if (which == NO_BIN)
		return;
	if (which < first_row)
		first_row = which;
	if (which > first_row + ROWS - 1)
		first_row = which - ROWS + 1;
}

static void show(){
	int chosen = chosen_bin();
	int row;

	if (chosen != shown_chosen){
		keep_in_view(chosen);
		shown_chosen = chosen;
		lv_obj_invalidate(table);
	}
	for (row = 0; row < ROWS; row++)
		row_show(row, first_row + row);
	bar_show();
}

//each row is given its ground, the chosen one's or every other one's, and
//an empty bin's pitch is dimmed, as the table is drawn
static void on_drawn(lv_event_t *e){
	lv_draw_task_t *task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
	lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
	lv_draw_label_dsc_t *label = lv_draw_task_get_label_dsc(task);
	int which;

	if (base == NULL || base->part != LV_PART_ITEMS || base->id1 >= ROWS)
		return;
	which = row_bin[base->id1];
	if (fill != NULL && which == shown_chosen)
		fill->color = lv_color_hex(COLOUR_CHOSEN_ROW);
	else if (fill != NULL && which % 2 == 1)
		fill->color = lv_color_hex(COLOUR_OTHER_ROW);
	if (label != NULL && base->id2 == HZ_COLUMN && bins[which].text[0] == 0)
		label->color = lv_color_hex(COLOUR_EMPTY_HZ);
	else if (label != NULL && base->id2 == HZ_COLUMN)
		label->color = lv_color_hex(COLOUR_HZ);
}

//a touch on a row chooses its bin, and selects the list for the knob
static void on_touched(lv_event_t *e){
	uint32_t row = LV_TABLE_CELL_NONE;
	uint32_t column = LV_TABLE_CELL_NONE;

	(void)e;
	lv_table_get_selected_cell(table, &row, &column);
	main_select("BINS");
	if (row < ROWS)
		choose(row_bin[row]);
}

/* what the rest of the panel calls */

void bins_create(){
	int row;
	int column;

	table = lv_table_create(main_screen);
	lv_obj_set_size(table, WATERFALL_WIDTH, BINS_HEIGHT);
	lv_obj_align(table, LV_ALIGN_TOP_LEFT, WATERFALL_LEFT, BINS_TOP);
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
		row_bin[row] = row;
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
	lv_obj_set_size(frame, WATERFALL_WIDTH, BINS_HEIGHT);
	lv_obj_align(frame, LV_ALIGN_TOP_LEFT, WATERFALL_LEFT, BINS_TOP);
	lv_obj_set_style_border_color(frame, lv_color_hex(COLOUR_SELECTED), 0);
	lv_obj_set_style_border_width(frame, SELECTED_WIDTH, 0);
	lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(frame, LV_OBJ_FLAG_HIDDEN);
}

//from loop(): what has come is shown, and so is a bin chosen elsewhere
void bins_slice(){
	if (table == NULL || !is_shown)
		return;
	if (!is_new && chosen_bin() == shown_chosen)
		return;
	is_new = false;
	show();
}

void bins_set_shown(bool shown){
	if (table == NULL || is_shown == shown)
		return;
	is_shown = shown;
	if (shown){
		lv_obj_remove_flag(table, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
		shown_chosen = NO_BIN;
		show();
	}
	else {
		lv_obj_add_flag(table, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
		bins_set_selected(false);
	}
}

//the list has the bright edge while it is what the knob moves
void bins_set_selected(bool selected){
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

//the knob: the bin a step up or down the passband is chosen
void bins_scroll(int steps){
	int which = chosen_bin() + steps;

	if (!is_shown || chosen_bin() == NO_BIN)
		return;
	if (which < 0)
		which = 0;
	if (which > BINS - 1)
		which = BINS - 1;
	choose(which);
	show();
}
