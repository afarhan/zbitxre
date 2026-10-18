// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// logbook_window.c — the station's logbook, in a window of its own.
//
// LOG on the main window opens it. It does what the LOG screen of the zbitx
// front panel does, front_panel/screens.cpp, and is laid out as that is:
// the title, the entry being written, and the list.
//
// The radio keeps the logbook; the window asks it for the newest entries, a
// hundred at most, with the request server.c describes, "log", and shows
// what comes back, LOG_LIST and then a LOG to an entry. A line to an entry,
// in the order the panel shows them: the date and the time in UTC, the
// frequency in kHz, the mode, the other station's callsign, what was sent
// and what was received, under a line of headings, the newest at the top.
// The radio sends the list again whenever the logbook changes, so an entry
// FT8 logs by itself appears while the window is open.
//
// One entry is marked. A tap on an entry marks it; a finger that moves
// scrolls the list and marks nothing. The knob that works the selected
// control moves the mark, as the panel's knob does, and the list follows.
//
// The author's rule for a logbook: an entry is never changed, but a bad one
// can be deleted. A tap on the marked entry, or a press of the knob, asks
// "Do you want to delete the entry?", with the entry under the question,
// and YES and NO. NO is marked to begin with; the knob moves between the
// two and a press chooses, or either is tapped. On YES the radio is asked
// to delete the entry, by its number in the logbook. What is being searched
// for goes with the request and is kept, so the list that comes back is
// still of that search: the operator may be deleting every entry of one
// callsign. The mark stays where it was.
//
// Above the list is the entry being written, as zbitxv2's logger had it:
// CALL, SENT, RECV, EXCH and NR, the radio's controls of those names, which
// logger.c on the radio describes; and three buttons, "?" beside CALL, and
// WIPE and SAVE at the end. The CW macros fill their callsign, reports and
// exchange from these same fields, so, as the author asked on 18 October
// 2026, they stay as they are when the window is closed and opened again,
// and are cleared only by WIPE or by SAVE; and in CW the macros' buttons
// are a row of their own under the fields, to be sent from here. Each field
// is one line high, and shows its name, dim, in place of a value while it
// is empty.
//
// A tap on a field brings the window's own keyboard up where the list was,
// under the fields, so that it never covers what is typed, and the field is
// typed into there on its row, its edge marked. The radio has no other
// keyboard to count on, and the author asked for one of few keys that comes
// up whenever a text is typed: the digits with the rub-out after the 0, the
// letters in three rows with the space between V and B, and the four marks
// a callsign, a report or an exchange has. What is typed goes to the radio,
// in capitals, when the keyboard's CLOSE is touched, or another field, any
// of the buttons, or a macro; there is nothing to cancel. A keyboard
// plugged into the radio works too: Enter or Escape takes what is typed.
//
// "?" searches the logbook for the callsign in CALL, or the beginning of
// one; an empty CALL lists every station. The author's rule is that the
// request to the radio always carries what is searched for; it is empty
// when the logbook is first opened. WIPE clears the five fields and lists
// the whole logbook again. SAVE asks the radio to log the entry. The
// author's rule is that it needs at least CALL, SENT and RECV; the radio
// checks, and says what is missing, or that the entry is logged, and what
// it says is shown beside the title, a refusal in bold. The entry is logged
// in the mode and on the frequency the radio is on, at the radio's time.
//
// CLOSE, or Escape, puts the window away, giving the radio what was being
// typed; what was searched for, and the fields, are there when it is opened
// again.
//
// The sizes follow the window's width, as the main window's do, so that it
// is as readable on the sbitx's screen as on a desktop.
//
// This file knows nothing of the socket: zbitx_gui.c gives it the lines the
// radio sends and a function to send one with.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "logbook_window.h"

#define ENTRIES_MAX 100
#define LINE_ROOM 192
#define TEXT_ROOM 32
#define SHOWN_ROOM 256
#define CSS_ROOM 2048
#define ENTRY_FIELDS 8
#define FIELDS 5
#define CALL_FIELD 0
#define NOT_EDITING (-1)
#define NO_MARK (-1)
// the plan the sizes are in: the front panel's screen, 480 across
#define PLAN_WIDTH 480
#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 480
#define TITLE_SIZE 11
#define VALUE_SIZE 13
#define ENTRY_SIZE 10
#define KEY_SIZE 14
#define BAR_HEIGHT 24
#define FIELD_HEIGHT 24
#define ANSWER_HEIGHT 40
#define MACRO_HEIGHT 28
#define BUTTON_WIDTH 44
#define BUTTON_PADDING 4
#define KEY_ROWS 4
#define KEY_COLUMNS 11
#define MACROS_MAX 12
#define MACRO_ROOM 16
#define CHOICES_ROOM 256
#define PAGE_LIST "list"
#define PAGE_KEYS "keys"
#define PAGE_ASK "ask"
#define LIST_WORD "LOG_LIST"
#define ENTRY_WORD "LOG"
#define LOGGER_CONTROL "LOGGER"
#define ENTRY_FORMAT "%-10s %-5s %6s %-5s %-11s %-12s %-12s"

// a field of the entry being written: the radio's control, and how much of
// the row it is given
struct field {
	const char *control;
	int share;
	char value[TEXT_ROOM];
	GtkWidget *box;
	GtkWidget *shown;
};

static struct field fields[FIELDS] = {
	{ .control = "CALL", .share = 4 },
	{ .control = "SENT", .share = 2 },
	{ .control = "RECV", .share = 2 },
	{ .control = "EXCH", .share = 3 },
	{ .control = "NR", .share = 2 },
};

// the keys, a row to a line: a character is its key, and the rest stand for
// the keys that are not characters, each one column wide but the space
#define RUB_OUT_KEY '<'
#define SPACE_KEY '_'
#define CLOSE_KEY '>'
#define SPACE_COLUMNS 2
static const char *const key_rows[KEY_ROWS] = {
	"1234567890<", "QWERTYUIOP/", "ASDFGHJKL?-", "ZXCV_BNM.>",
};

static void (*send)(const char *word, const char *rest);
static GtkWidget *window;
static GtkWidget *title;
static GtkWidget *said;
static GtkWidget *pages;
static GtkWidget *list;
static GtkWidget *list_scroller;
static GtkWidget *asked_entry;
static GtkWidget *macro_row;
static char macro_choices[CHOICES_ROOM];
static GtkWidget *yes_button;
static GtkWidget *no_button;
static GtkCssProvider *css;
static int css_width;
static guint css_idle;

static char lines[ENTRIES_MAX][LINE_ROOM];
static GtkWidget *rows[ENTRIES_MAX];
static int line_count;
static int lines_expected;
static int rows_shown;
static int mark;
static int is_yes_marked;
static char searched_for[TEXT_ROOM];
static int editing = NOT_EDITING;
static char typed[TEXT_ROOM];

static void set_class(GtkWidget *widget, const char *name, int on) {
	GtkStyleContext *style = gtk_widget_get_style_context(widget);

	if (on)
		gtk_style_context_add_class(style, name);
	else
		gtk_style_context_remove_class(style, name);
}

static int is_page_shown(const char *name) {
	return !strcmp(gtk_stack_get_visible_child_name(GTK_STACK(pages)), name);
}

// a size of the plan, as it is on this window
static int px(int planned) {
	int width = css_width;

	if (width <= 0)
		width = DEFAULT_WIDTH;
	return planned * width / PLAN_WIDTH;
}

static void load_style(void) {
	char rules[CSS_ROOM];

	if (css == NULL) {
		css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	}
	snprintf(rules, sizeof(rules),
		".log-window { background-color: #000000; color: #FFFFFF; }"
		".log-title { font-size: %dpx; font-weight: bold; color: #FFFFFF; }"
		".log-said { font-size: %dpx; color: #90A0B0; }"
		".log-said.log-refused { font-family: sans-serif; font-weight: bold;"
		" color: #FFE000; }"
		".log-field { background-color: #101820; box-shadow: inset 0 0 0 1px #405060; }"
		".log-field.log-editing { box-shadow: inset 0 0 0 %dpx #FFC000; }"
		".log-value.log-prompt { font-family: sans-serif; font-weight: normal;"
		" color: #406070; }"
		".log-value { font-family: monospace; font-size: %dpx; font-weight: bold;"
		" color: #FFFFFF; }"
		".log-button { background-image: none; background-color: #101820;"
		" border: 1px solid #405060; border-radius: 0; color: #FFFFFF;"
		" font-size: %dpx; font-weight: bold; text-shadow: none; box-shadow: none;"
		" min-width: 0; min-height: 0; padding: 0 %dpx; }"
		".log-button:active { background-color: #604800; }"
		".log-button.log-marked { background-color: #604800; }"
		".log-headings { font-family: monospace; font-size: %dpx; font-weight: bold;"
		" color: #00FFFF; background-color: #101820; }"
		".log-entry { font-family: monospace; font-size: %dpx; color: #FFFFFF; }"
		".log-row { background-color: #101820; }"
		".log-row.log-other { background-color: #1C2834; }"
		".log-row.log-marked { background-color: #604800; }"
		".log-question { font-size: %dpx; font-weight: bold; color: #FFFFFF; }"
		".log-key { background-image: none; background-color: #101820;"
		" border: 1px solid #405060; border-radius: 0; color: #FFFFFF;"
		" font-size: %dpx; font-weight: bold; text-shadow: none; box-shadow: none;"
		" min-width: 0; min-height: 0; padding: 0; }"
		".log-key:active { background-color: #604800; }",
		px(TITLE_SIZE), px(TITLE_SIZE), px(1) + 1, px(VALUE_SIZE),
		px(TITLE_SIZE), px(BUTTON_PADDING), px(ENTRY_SIZE), px(ENTRY_SIZE), px(VALUE_SIZE),
		px(KEY_SIZE));
	gtk_css_provider_load_from_data(css, rules, -1, NULL);
}

static void size_the_fixed_parts(void) {
	int i;

	gtk_widget_set_size_request(title, -1, px(BAR_HEIGHT));
	for (i = 0; i < FIELDS; i++)
		gtk_widget_set_size_request(fields[i].box, -1, px(FIELD_HEIGHT));
	gtk_widget_set_size_request(macro_row, -1, px(MACRO_HEIGHT));
}

// the sizes are made again, once the window has its new width
static gboolean on_sized_idle(gpointer unused) {
	(void)unused;
	css_idle = 0;
	load_style();
	size_the_fixed_parts();
	return G_SOURCE_REMOVE;
}

static void on_window_sized(GtkWidget *widget, GdkRectangle *allocation, gpointer unused) {
	(void)widget;
	(void)unused;
	if (allocation->width == css_width)
		return;
	css_width = allocation->width;
	if (css_idle == 0)
		css_idle = g_idle_add(on_sized_idle, NULL);
}

/* what the radio says */

static void show_said(const char *text, int is_refused) {
	gtk_label_set_text(GTK_LABEL(said), text);
	set_class(said, "log-refused", is_refused);
}

static void show_title(void) {
	char text[SHOWN_ROOM];

	if (line_count < lines_expected)
		snprintf(text, sizeof(text), "LOGBOOK");
	else if (searched_for[0] != 0)
		snprintf(text, sizeof(text), "LOGBOOK  %d with %s", line_count, searched_for);
	else
		snprintf(text, sizeof(text), "LOGBOOK  %d", line_count);
	gtk_label_set_text(GTK_LABEL(title), text);
}

// each field shows what the radio says it is, but for the one being typed
// into, which shows what is typed
static void show_fields(void) {
	char with_mark[TEXT_ROOM + 2];
	int i;

	for (i = 0; i < FIELDS; i++) {
		set_class(fields[i].box, "log-editing", i == editing);
		if (i == editing) {
			snprintf(with_mark, sizeof(with_mark), "%s_", typed);
			set_class(fields[i].shown, "log-prompt", 0);
			gtk_label_set_text(GTK_LABEL(fields[i].shown), with_mark);
			continue;
		}
		set_class(fields[i].shown, "log-prompt", fields[i].value[0] == 0);
		if (fields[i].value[0] == 0)
			gtk_label_set_text(GTK_LABEL(fields[i].shown), fields[i].control);
		else
			gtk_label_set_text(GTK_LABEL(fields[i].shown), fields[i].value);
	}
}

/* the list */

// an entry's line as it is shown: its fields in columns, the first, the
// entry's number in the logbook, left out
static void entry_text(char *shown, int room, const char *line) {
	char copy[LINE_ROOM];
	char *field[ENTRY_FIELDS];
	char *saved = NULL;
	int i;

	snprintf(copy, sizeof(copy), "%s", line);
	field[0] = strtok_r(copy, "|", &saved);
	for (i = 1; i < ENTRY_FIELDS; i++) {
		field[i] = strtok_r(NULL, "|", &saved);
		if (field[i] == NULL)
			field[i] = "";
	}
	snprintf(shown, room, ENTRY_FORMAT, field[1], field[2], field[3], field[4], field[5],
		field[6], field[7]);
}

// the list is moved, if it must be, to show the marked entry
static gboolean show_the_mark(gpointer unused) {
	GtkAdjustment *scroll = gtk_scrolled_window_get_vadjustment(
		GTK_SCROLLED_WINDOW(list_scroller));
	GtkAllocation where;
	double top = gtk_adjustment_get_value(scroll);
	double page = gtk_adjustment_get_page_size(scroll);

	(void)unused;
	if (mark < 0 || mark >= rows_shown)
		return G_SOURCE_REMOVE;
	gtk_widget_get_allocation(rows[mark], &where);
	if (where.y < top)
		gtk_adjustment_set_value(scroll, where.y);
	else if (where.y + where.height > top + page)
		gtk_adjustment_set_value(scroll, where.y + where.height - page);
	return G_SOURCE_REMOVE;
}

static void mark_at(int which) {
	if (rows_shown == 0) {
		mark = NO_MARK;
		return;
	}
	if (which < 0)
		which = 0;
	if (which >= rows_shown)
		which = rows_shown - 1;
	if (mark >= 0 && mark < rows_shown)
		set_class(rows[mark], "log-marked", 0);
	mark = which;
	set_class(rows[mark], "log-marked", 1);
	g_idle_add(show_the_mark, NULL);
}

static void show_asking(void) {
	set_class(yes_button, "log-marked", is_yes_marked);
	set_class(no_button, "log-marked", !is_yes_marked);
}

// "Do you want to delete the entry?", for the marked one, NO marked
static void ask_to_delete(void) {
	char shown[SHOWN_ROOM];

	if (mark < 0 || mark >= rows_shown)
		return;
	entry_text(shown, sizeof(shown), lines[mark]);
	gtk_label_set_text(GTK_LABEL(asked_entry), shown);
	is_yes_marked = 0;
	show_asking();
	gtk_stack_set_visible_child_name(GTK_STACK(pages), PAGE_ASK);
}

// the radio is asked to delete the marked entry, by its number, and to
// list again what is being searched for
static void delete_marked(void) {
	char asked[SHOWN_ROOM];

	gtk_stack_set_visible_child_name(GTK_STACK(pages), PAGE_LIST);
	if (mark < 0 || mark >= rows_shown)
		return;
	snprintf(asked, sizeof(asked), "%d %s", atoi(lines[mark]), searched_for);
	send("logdel", asked);
}

// a tap on an entry marks it; on the marked one, asks whether to delete it
static void on_entry_tapped(GtkGestureMultiPress *gesture, gint presses, gdouble x,
		gdouble y, gpointer which) {
	int tapped = GPOINTER_TO_INT(which);

	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	if (tapped == mark)
		ask_to_delete();
	else
		mark_at(tapped);
}

// a widget is made to tell a tap: a press and a release in one place, by
// the mouse or a finger. In a list that scrolls, a finger that moves is
// taken by the list for scrolling, and no tap is told
static void tell_taps(GtkWidget *widget, GCallback tapped, gpointer data) {
	GtkGesture *gesture = gtk_gesture_multi_press_new(widget);

	gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
		| GDK_TOUCH_MASK);
	g_signal_connect(gesture, "released", tapped, data);
	g_object_set_data_full(G_OBJECT(widget), "tap", gesture, g_object_unref);
}

static void drop_a_row(GtkWidget *row, gpointer unused) {
	(void)unused;
	gtk_widget_destroy(row);
}

// the list is made again of the lines the radio sent; the mark stays where
// it was, or on the last entry if there are now fewer
static void fill_list(void) {
	char shown[SHOWN_ROOM];
	GtkWidget *label;
	int was_marked = mark;
	int i;

	gtk_container_foreach(GTK_CONTAINER(list), drop_a_row, NULL);
	rows_shown = line_count;
	for (i = 0; i < rows_shown; i++) {
		entry_text(shown, sizeof(shown), lines[i]);
		label = gtk_label_new(shown);
		gtk_label_set_xalign(GTK_LABEL(label), 0);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_style_context_add_class(gtk_widget_get_style_context(label), "log-entry");
		rows[i] = gtk_event_box_new();
		gtk_style_context_add_class(gtk_widget_get_style_context(rows[i]), "log-row");
		set_class(rows[i], "log-other", i % 2);
		gtk_container_add(GTK_CONTAINER(rows[i]), label);
		tell_taps(rows[i], G_CALLBACK(on_entry_tapped), GINT_TO_POINTER(i));
		gtk_box_pack_start(GTK_BOX(list), rows[i], FALSE, FALSE, 0);
	}
	gtk_widget_show_all(list);
	mark = NO_MARK;
	if (was_marked < 0)
		was_marked = 0;
	mark_at(was_marked);
	show_title();
	// the entry asked about may be gone, or another in its place
	if (is_page_shown(PAGE_ASK))
		gtk_stack_set_visible_child_name(GTK_STACK(pages), PAGE_LIST);
}

// LOG_LIST 2 VU2: a list begins, of so many entries
static void take_list_line(const char *value) {
	const char *searched = strchr(value, ' ');

	lines_expected = atoi(value);
	if (lines_expected > ENTRIES_MAX)
		lines_expected = ENTRIES_MAX;
	line_count = 0;
	if (searched == NULL)
		searched = "";
	else
		searched++;
	snprintf(searched_for, sizeof(searched_for), "%s", searched);
	if (lines_expected == 0)
		fill_list();
}

static void take_entry_line(const char *value) {
	if (line_count >= lines_expected)
		return;
	snprintf(lines[line_count], LINE_ROOM, "%s", value);
	line_count++;
	if (line_count == lines_expected)
		fill_list();
}

// a line the radio sent, its first word and the rest: the window takes what
// is the logbook's, the entry's fields, and what the radio has to say
void logbook_window_took(const char *name, const char *value) {
	const char *why = value;
	int i;

	if (window == NULL)
		return;
	if (!strcmp(name, LIST_WORD)) {
		take_list_line(value);
		return;
	}
	if (!strcmp(name, ENTRY_WORD)) {
		take_entry_line(value);
		return;
	}
	if (!strcmp(name, "SAY")) {
		show_said(value, 0);
		return;
	}
	if (!strcmp(name, "WARN")) {
		show_said(value, 1);
		return;
	}
	// ERROR LOGGER CALL is needed to log the entry
	if (!strcmp(name, "ERROR")) {
		if (!strncmp(why, LOGGER_CONTROL " ", strlen(LOGGER_CONTROL " ")))
			why += strlen(LOGGER_CONTROL " ");
		show_said(why, 1);
		return;
	}
	for (i = 0; i < FIELDS; i++) {
		if (strcmp(name, fields[i].control))
			continue;
		snprintf(fields[i].value, TEXT_ROOM, "%s", value);
		show_fields();
	}
}

/* the entry being written */

// the radio is asked for the newest entries with stations whose callsign
// begins as given, which may be what is searched for already
static void ask_for_list(const char *callsign) {
	char asked[TEXT_ROOM];

	snprintf(asked, sizeof(asked), "%s", callsign);
	snprintf(searched_for, sizeof(searched_for), "%s", asked);
	mark = 0;
	send("log", searched_for);
}

// the field being typed into is let go, and the list comes back
static void end_editing(void) {
	editing = NOT_EDITING;
	show_fields();
	if (is_page_shown(PAGE_KEYS))
		gtk_stack_set_visible_child_name(GTK_STACK(pages), PAGE_LIST);
}

// what is typed in the field goes to the radio, which is told it even when
// it is nothing, so that a field can be emptied
static void done_editing(void) {
	if (editing == NOT_EDITING)
		return;
	snprintf(fields[editing].value, TEXT_ROOM, "%s", typed);
	send(fields[editing].control, typed);
	end_editing();
}

// the keyboard comes up where the list was, and what is typed goes into
// the field tapped, there on its row; a tap on another field takes what was
// typed in this one first
static void begin_editing(int which) {
	if (which == editing)
		return;
	done_editing();
	editing = which;
	snprintf(typed, sizeof(typed), "%s", fields[which].value);
	show_fields();
	gtk_stack_set_visible_child_name(GTK_STACK(pages), PAGE_KEYS);
}

static void on_field_tapped(GtkGestureMultiPress *gesture, gint presses, gdouble x,
		gdouble y, gpointer which) {
	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	begin_editing(GPOINTER_TO_INT(which));
}

static void type_a_character(char c) {
	size_t length = strlen(typed);

	if (editing == NOT_EDITING || length + 1 >= sizeof(typed))
		return;
	typed[length] = (char)toupper((unsigned char)c);
	typed[length + 1] = 0;
	show_fields();
}

static void rub_out(void) {
	size_t length = strlen(typed);

	if (editing == NOT_EDITING || length == 0)
		return;
	typed[length - 1] = 0;
	show_fields();
}

static void on_key_pressed(GtkButton *button, gpointer key) {
	char c = (char)GPOINTER_TO_INT(key);

	(void)button;
	if (c == RUB_OUT_KEY)
		rub_out();
	else if (c == CLOSE_KEY)
		done_editing();
	else if (c == SPACE_KEY)
		type_a_character(' ');
	else
		type_a_character(c);
}

// a macro is sent with what is typed given to the radio first, so that the
// callsign just typed is the one it sends
static void on_macro_pressed(GtkButton *button, gpointer unused) {
	(void)unused;
	done_editing();
	send("MACRO", gtk_button_get_label(button));
}

// "?": the logbook is searched for the callsign in CALL, as it is typed if
// it is being typed
static void on_find_pressed(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	done_editing();
	ask_for_list(fields[CALL_FIELD].value);
}

// the five fields are cleared, here and on the radio, and the whole logbook
// is listed again
static void on_wipe_pressed(GtkButton *button, gpointer unused) {
	int i;

	(void)button;
	(void)unused;
	end_editing();
	for (i = 0; i < FIELDS; i++)
		fields[i].value[0] = 0;
	show_fields();
	show_said("", 0);
	send(LOGGER_CONTROL, "WIPE");
	ask_for_list("");
}

// the radio is asked to log the entry; it says what is missing, or that it
// is logged, and then the whole logbook is listed, the new entry at its top
static void on_save_pressed(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	done_editing();
	show_said("", 0);
	send(LOGGER_CONTROL, "SAVE");
	ask_for_list("");
}

static void on_yes_pressed(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	delete_marked();
}

static void on_no_pressed(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	gtk_stack_set_visible_child_name(GTK_STACK(pages), PAGE_LIST);
}

static void on_close_pressed(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	done_editing();
	gtk_widget_hide(window);
}

/* the knob, and a keyboard plugged in */

// the knob that works the selected control: the mark moves, or, while the
// question is asked, goes between YES and NO
void logbook_window_turned(int notches) {
	if (!logbook_window_is_open() || notches == 0)
		return;
	if (is_page_shown(PAGE_ASK)) {
		is_yes_marked = !is_yes_marked;
		show_asking();
		return;
	}
	if (is_page_shown(PAGE_LIST))
		mark_at(mark + notches);
}

// a press of that knob: the marked entry is asked about, the answer marked
// is given, or what is typed is taken
void logbook_window_pressed(void) {
	if (!logbook_window_is_open())
		return;
	if (is_page_shown(PAGE_KEYS)) {
		done_editing();
		return;
	}
	if (!is_page_shown(PAGE_ASK)) {
		ask_to_delete();
		return;
	}
	if (is_yes_marked)
		delete_marked();
	else
		gtk_stack_set_visible_child_name(GTK_STACK(pages), PAGE_LIST);
}

static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer unused) {
	guint32 character = gdk_keyval_to_unicode(gdk_keyval_to_upper(event->keyval));
	int is_enter = event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter;

	(void)widget;
	(void)unused;
	if (editing != NOT_EDITING) {
		if (is_enter || event->keyval == GDK_KEY_Escape)
			done_editing();
		else if (event->keyval == GDK_KEY_BackSpace)
			rub_out();
		else if (character >= ' ' && character <= '~')
			type_a_character((char)character);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Escape && is_page_shown(PAGE_ASK))
		gtk_stack_set_visible_child_name(GTK_STACK(pages), PAGE_LIST);
	else if (event->keyval == GDK_KEY_Escape)
		gtk_widget_hide(window);
	else if (event->keyval == GDK_KEY_Up)
		logbook_window_turned(-1);
	else if (event->keyval == GDK_KEY_Down)
		logbook_window_turned(1);
	else if (is_enter)
		logbook_window_pressed();
	else
		return FALSE;
	return TRUE;
}

/* making the window */

static GtkWidget *button_new(const char *text, const char *class, GCallback pressed,
		gpointer data) {
	GtkWidget *button = gtk_button_new_with_label(text);

	gtk_style_context_add_class(gtk_widget_get_style_context(button), class);
	gtk_widget_set_can_focus(button, FALSE);
	g_signal_connect(button, "clicked", pressed, data);
	return button;
}

static GtkWidget *label_new(const char *text, const char *class) {
	GtkWidget *label = gtk_label_new(text);

	gtk_style_context_add_class(gtk_widget_get_style_context(label), class);
	return label;
}

// the title, what the radio says, and CLOSE
static GtkWidget *create_bar(void) {
	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, px(4));

	title = label_new("LOGBOOK", "log-title");
	said = label_new("", "log-said");
	gtk_label_set_xalign(GTK_LABEL(said), 0);
	gtk_label_set_ellipsize(GTK_LABEL(said), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(bar), title, FALSE, FALSE, px(4));
	gtk_box_pack_start(GTK_BOX(bar), said, TRUE, TRUE, px(4));
	gtk_box_pack_end(GTK_BOX(bar), button_new("CLOSE", "log-button",
		G_CALLBACK(on_close_pressed), NULL), FALSE, FALSE, 0);
	return bar;
}

// the entry being written: the five fields, "?" after CALL, and WIPE and
// SAVE at the end
static GtkWidget *create_fields(void) {
	GtkWidget *row = gtk_grid_new();
	int column = 0;
	int i;

	gtk_grid_set_column_homogeneous(GTK_GRID(row), TRUE);
	for (i = 0; i < FIELDS; i++) {
		fields[i].shown = label_new("", "log-value");
		fields[i].box = gtk_event_box_new();
		gtk_style_context_add_class(gtk_widget_get_style_context(fields[i].box),
			"log-field");
		gtk_container_add(GTK_CONTAINER(fields[i].box), fields[i].shown);
		tell_taps(fields[i].box, G_CALLBACK(on_field_tapped), GINT_TO_POINTER(i));
		gtk_grid_attach(GTK_GRID(row), fields[i].box, column, 0, fields[i].share, 1);
		column += fields[i].share;
		if (i != CALL_FIELD)
			continue;
		gtk_grid_attach(GTK_GRID(row), button_new("?", "log-button",
			G_CALLBACK(on_find_pressed), NULL), column, 0, 1, 1);
		column++;
	}
	gtk_grid_attach(GTK_GRID(row), button_new("WIPE", "log-button",
		G_CALLBACK(on_wipe_pressed), NULL), column, 0, 2, 1);
	column += 2;
	gtk_grid_attach(GTK_GRID(row), button_new("SAVE", "log-button",
		G_CALLBACK(on_save_pressed), NULL), column, 0, 2, 1);
	return row;
}

// the headings, and under them the entries, which scroll
static GtkWidget *create_list_page(void) {
	GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *headings;
	char shown[SHOWN_ROOM];

	snprintf(shown, sizeof(shown), ENTRY_FORMAT, "Date", "UTC", "kHz", "Mode", "Callsign",
		"Sent", "Rcvd");
	headings = label_new(shown, "log-headings");
	gtk_label_set_xalign(GTK_LABEL(headings), 0);
	gtk_label_set_ellipsize(GTK_LABEL(headings), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(page), headings, FALSE, FALSE, 0);

	list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	list_scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroller), GTK_POLICY_NEVER,
		GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(list_scroller), FALSE);
	gtk_container_add(GTK_CONTAINER(list_scroller), list);
	gtk_box_pack_start(GTK_BOX(page), list_scroller, TRUE, TRUE, 0);
	return page;
}

static const char *key_label(char c, char *text) {
	if (c == RUB_OUT_KEY)
		return "⌫";
	if (c == SPACE_KEY)
		return "SPACE";
	if (c == CLOSE_KEY)
		return "CLOSE";
	text[0] = c;
	text[1] = 0;
	return text;
}

// the keyboard, which has no more keys than are needed
static GtkWidget *create_keys_page(void) {
	GtkWidget *keys = gtk_grid_new();
	GtkWidget *key;
	char text[2];
	const char *at;
	int column;
	int width;
	int row;

	gtk_grid_set_row_homogeneous(GTK_GRID(keys), TRUE);
	gtk_grid_set_column_homogeneous(GTK_GRID(keys), TRUE);
	for (row = 0; row < KEY_ROWS; row++) {
		column = 0;
		for (at = key_rows[row]; *at != 0; at++) {
			width = 1;
			if (*at == SPACE_KEY)
				width = SPACE_COLUMNS;
			key = button_new(key_label(*at, text), "log-key", G_CALLBACK(on_key_pressed),
				GINT_TO_POINTER(*at));
			gtk_widget_set_hexpand(key, TRUE);
			gtk_widget_set_vexpand(key, TRUE);
			gtk_grid_attach(GTK_GRID(keys), key, column, row, width, 1);
			column += width;
		}
	}
	return keys;
}

static void drop_a_macro(GtkWidget *button, gpointer unused) {
	(void)unused;
	gtk_widget_destroy(button);
}

// the macros' buttons, in CW: made again when the radio's macros change,
// and the row put away in the other modes, where a macro is refused
void logbook_window_set_macros(const char *choices) {
	char copy[CHOICES_ROOM];
	char *saved = NULL;
	char *one;
	int count = 0;

	if (window == NULL || !strcmp(choices, macro_choices))
		return;
	snprintf(macro_choices, sizeof(macro_choices), "%s", choices);
	gtk_container_foreach(GTK_CONTAINER(macro_row), drop_a_macro, NULL);
	snprintf(copy, sizeof(copy), "%s", choices);
	for (one = strtok_r(copy, "/", &saved); one != NULL && count < MACROS_MAX;
			one = strtok_r(NULL, "/", &saved)) {
		gtk_box_pack_start(GTK_BOX(macro_row), button_new(one, "log-button",
			G_CALLBACK(on_macro_pressed), NULL), TRUE, TRUE, 0);
		count++;
	}
	gtk_widget_show_all(macro_row);
	gtk_widget_set_visible(macro_row, count > 0);
}

// the question, the entry it is asked about, and YES and NO
static GtkWidget *create_ask_page(void) {
	GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, px(8));
	GtkWidget *answers = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, px(16));

	asked_entry = label_new("", "log-entry");
	gtk_label_set_ellipsize(GTK_LABEL(asked_entry), PANGO_ELLIPSIZE_END);
	yes_button = button_new("YES", "log-button", G_CALLBACK(on_yes_pressed), NULL);
	no_button = button_new("NO", "log-button", G_CALLBACK(on_no_pressed), NULL);
	gtk_widget_set_size_request(yes_button, px(2 * BUTTON_WIDTH), px(ANSWER_HEIGHT));
	gtk_widget_set_size_request(no_button, px(2 * BUTTON_WIDTH), px(ANSWER_HEIGHT));
	gtk_box_pack_start(GTK_BOX(answers), yes_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(answers), no_button, FALSE, FALSE, 0);
	gtk_widget_set_halign(answers, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(page), label_new("Do you want to delete the entry?",
		"log-question"), FALSE, FALSE, px(16));
	gtk_box_pack_start(GTK_BOX(page), asked_entry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(page), answers, FALSE, FALSE, px(8));
	return page;
}

static void create_window(GtkWindow *main_window) {
	GtkWidget *all = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	int width = DEFAULT_WIDTH;
	int height = DEFAULT_HEIGHT;

	if (main_window != NULL)
		gtk_window_get_size(main_window, &width, &height);
	css_width = width;
	load_style();
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_style_context_add_class(gtk_widget_get_style_context(window), "log-window");
	gtk_window_set_title(GTK_WINDOW(window), "zbitx logbook");
	gtk_window_set_default_size(GTK_WINDOW(window), width, height);
	if (main_window != NULL && gtk_window_is_maximized(main_window))
		gtk_window_maximize(GTK_WINDOW(window));

	pages = gtk_stack_new();
	gtk_stack_add_named(GTK_STACK(pages), create_list_page(), PAGE_LIST);
	gtk_stack_add_named(GTK_STACK(pages), create_keys_page(), PAGE_KEYS);
	gtk_stack_add_named(GTK_STACK(pages), create_ask_page(), PAGE_ASK);
	gtk_box_pack_start(GTK_BOX(all), create_bar(), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(all), create_fields(), FALSE, FALSE, 0);
	macro_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_homogeneous(GTK_BOX(macro_row), TRUE);
	gtk_box_pack_start(GTK_BOX(all), macro_row, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(all), pages, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(window), all);
	size_the_fixed_parts();

	g_signal_connect(window, "size-allocate", G_CALLBACK(on_window_sized), NULL);
	g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);
	g_signal_connect(window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
}

int logbook_window_is_open(void) {
	return window != NULL && gtk_widget_get_visible(window);
}

// the window is opened, or brought to the front, and the radio is asked for
// the entries of what was last searched for, every station at first. The
// first time, the fields are as the radio has them, which the caller says;
// after that the window keeps them, and follows what the radio says of them
// while it is put away. The caller's copy is not used again: the radio does
// not tell a client of a change it made itself, so the caller has not seen
// what was typed here, and opening the window had emptied the fields
void logbook_window_open(GtkWindow *main_window,
		void (*send_line)(const char *word, const char *rest),
		const char *(*value_of)(const char *control), const char *macros) {
	int i;

	send = send_line;
	if (window == NULL) {
		create_window(main_window);
		for (i = 0; i < FIELDS; i++)
			snprintf(fields[i].value, TEXT_ROOM, "%s", value_of(fields[i].control));
	}
	editing = NOT_EDITING;
	gtk_widget_show_all(window);
	logbook_window_set_macros(macros);
	gtk_widget_set_visible(macro_row, macro_choices[0] != 0);
	gtk_stack_set_visible_child_name(GTK_STACK(pages), PAGE_LIST);
	show_fields();
	show_said("", 0);
	gtk_window_present(GTK_WINDOW(window));
	ask_for_list(searched_for);
}
