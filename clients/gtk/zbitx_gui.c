// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// zbitx_gui.c — a window on the desktop that works the radio over the
// network.
//
//     zbitx_gui [radio] [port]
//
// The radio is sbitx.local unless another name or address is given, and the
// port 8081.
//
// THE WINDOW
//
// It fits an 800 by 600 screen with the desktop's taskbar still showing,
// maximised or not. It is in two: a main area on the left, and a list of
// the controls down the right side, where the radio's knob is.
//
// The main area has the frequency in large figures, a line under it with
// the settings that matter most (the mode, the band, VOL, IF and the
// mode's own), and below them the spectrum and the
// waterfall. The spectrum is drawn with the passband shaded, the part of it
// the receiver hears, and a line at the frequency that is tuned.
//
// The list has a row to each control, its name and its value, and scrolls
// when there are more than it can show. It is made from the radio's schema,
// so a control that a modem adds comes into the list without this program
// knowing of it beforehand. The rows are in this order:
//
//   VOL, BAND, IF, DRIVE    the radio's core controls, and any other of the
//                           radio's own
//   MODE
//     the mode's own        under the mode, set in: BW, and then PITCH and
//                           WPM in CW, and so on
//
// The rows, and the frequency's readout, are each 50 pixels high.
//
// VOL is the radio's AUDIO. BW is BW_VOICE in the voice modes and BW_CW in
// CW: the radio keeps one for each, as zbitxv2 did. For voice the
// passband's low edge stays at 300 Hz and BW moves the high edge; for CW
// the passband is centred on PITCH. A modem's own controls are shown only
// in its modes: a control in the group CW in CW and CWR, one in VOICE in
// USB, LSB and AM.
//
// Every control is worked as the radio's knob would work it. The mouse
// wheel over a row moves its value a step, a number by its step and a
// choice to the next one; the wheel over the frequency, the spectrum or
// the waterfall tunes, in steps of 1 kHz. On the keyboard, Tab and
// Shift-Tab move the focus down and up the list, from the frequency and
// back to it, and the up and down keys move whatever has the focus. A click
// on a row gives it the focus; Escape, or a click on the frequency, gives
// it back to the frequency.
//
// THE RADIO
//
// It speaks the radio's control protocol, which server.c on the radio
// describes: lines of a name and a value. On connecting it asks for the
// schema, to learn each control's range and step, and for the spectrum. It
// sends a line when a control is moved, and shows the change at once, since
// the radio does not tell a client of its own changes. It shows what the
// radio tells it of everyone else's, so the window follows the radio when
// the front panel is turned.
//
// Showing what the radio says never sends anything back: that is how a
// change goes round in a loop.
//
// If the radio cannot be reached, or goes, the window says so and tries
// again every three seconds.
//
// It is built on the desktop with "make gui", which needs GTK 3.

#include <gtk/gtk.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_RADIO "sbitx.local"
#define DEFAULT_PORT 8081
#define RETRY_EVERY_MS 3000
#define STEP_HZ 1000
#define HZ_PER_MHZ 1000000
#define HZ_PER_KHZ 1000
#define LOWEST_HZ 500000
#define HIGHEST_HZ 30000000
#define LINE_MAX_LENGTH 128
#define TEXT_MAX 64
#define GAP 6

#define WINDOW_WIDTH 780
#define WINDOW_HEIGHT 520
#define SPECTRUM_MIN_HEIGHT 240
#define SIDEBAR_WIDTH 210
#define ROW_HEIGHT 50
#define ROW_MARGIN_SIDE 12
#define UNDER_MODE_MARGIN 28
#define SUMMARY_MAX 256

#define KNOWN_MAX 64
#define CONTROL_NAME_MAX 16
#define CONTROL_VALUE_MAX 32
#define CHOICES_MAX 128
#define NOT_FOUND (-1)
#define ON_FREQUENCY (-1)

#define POINTS_MAX 1024
#define FALL_ROWS 256
#define FULL_SCALE_DB 80
#define SHADES_PER_DB 2
#define SHADES_A_BAND 32
#define SHADE_STEP 8
#define COLOUR_MAX 255
#define GRAPH_SHARE 3
#define VOICE_LOW_HZ 300

// what the radio has told of a control: its value, and from the schema its
// kind and range
struct known {
	char name[CONTROL_NAME_MAX];
	char group[CONTROL_NAME_MAX];
	char value[CONTROL_VALUE_MAX];
	int is_choice;
	int min;
	int max;
	int step;
	char choices[CHOICES_MAX];
};

// a row of the list: the control it works, or BW, which works BW_VOICE or
// BW_CW with the mode
struct row {
	char control[CONTROL_NAME_MAX];
	GtkWidget *box;
	GtkWidget *value_label;
};

static const char *core_rows[] = { "AUDIO", "BAND", "IF", "DRIVE", NULL };
static const char *summary_rows[] = { "AUDIO", "IF", NULL };

static const char *radio_name = DEFAULT_RADIO;
static int radio_port = DEFAULT_PORT;

static GSocketClient *connector;
static GSocketConnection *connection;
static GDataInputStream *reader;
static GOutputStream *writer;

static GtkWidget *frequency_box;
static GtkWidget *frequency_label;
static GtkWidget *summary_label;
static GtkWidget *spectrum_area;
static GtkWidget *status_label;
static GtkWidget *list;
static GtkWidget *list_scroller;

static struct known known[KNOWN_MAX];
static int known_count;
static struct row rows[KNOWN_MAX];
static int row_count;
static int focus = ON_FREQUENCY;

static int span_hz;
static int point_count;
static uint8_t points[POINTS_MAX];
static cairo_surface_t *fall;

static void try_to_connect(void);
static void make_list(void);

static void show_status(const char *text) {
	gtk_label_set_text(GTK_LABEL(status_label), text);
}

static struct known *find_known(const char *name) {
	int i;

	for (i = 0; i < known_count; i++) {
		if (!strcmp(known[i].name, name))
			return &known[i];
	}
	if (known_count == KNOWN_MAX)
		return NULL;
	memset(&known[known_count], 0, sizeof(known[0]));
	snprintf(known[known_count].name, CONTROL_NAME_MAX, "%s", name);
	return &known[known_count++];
}

static const char *value_of(const char *name) {
	struct known *k = find_known(name);

	if (k == NULL)
		return "";
	return k->value;
}

static int is_cw(void) {
	const char *mode = value_of("MODE");

	return !strcmp(mode, "CW") || !strcmp(mode, "CWR");
}

static const char *row_control(const struct row *r) {
	if (strcmp(r->control, "BW"))
		return r->control;
	if (is_cw())
		return "BW_CW";
	return "BW_VOICE";
}

static const char *label_of(const char *control) {
	if (!strcmp(control, "AUDIO"))
		return "VOL";
	return control;
}

// the group of the modem that has the mode in use: CW for CW and CWR,
// VOICE for the voice modes, and the mode's own name for any other
static const char *mode_group(void) {
	const char *mode = value_of("MODE");

	if (is_cw())
		return "CW";
	if (!strcmp(mode, "USB") || !strcmp(mode, "LSB") || !strcmp(mode, "AM"))
		return "VOICE";
	return mode;
}

static int is_row_shown(const struct row *r) {
	struct known *k = find_known(row_control(r));

	if (k == NULL)
		return 0;
	if (!strcmp(r->control, "BW") || !strcmp(k->group, "RADIO"))
		return 1;
	return !strcmp(k->group, mode_group());
}

static void show_frequency(void) {
	int hz = atoi(value_of("FREQ"));
	char markup[TEXT_MAX * 2];
	const char *colour = "#d0d0d0";

	if (focus == ON_FREQUENCY)
		colour = "#ffd040";
	snprintf(markup, sizeof(markup),
		"<span font_desc='Monospace Bold 28' foreground='%s'>%d.%03d.%03d</span>",
		colour, hz / HZ_PER_MHZ, (hz % HZ_PER_MHZ) / HZ_PER_KHZ, hz % HZ_PER_KHZ);
	gtk_label_set_markup(GTK_LABEL(frequency_label), markup);
}

// what the summary shows, after the mode and the band: VOL, IF, and the
// mode's own
static int is_row_in_summary(const struct row *r) {
	struct known *k = find_known(row_control(r));
	int i;

	if (!is_row_shown(r) || k == NULL)
		return 0;
	for (i = 0; summary_rows[i] != NULL; i++) {
		if (!strcmp(summary_rows[i], r->control))
			return 1;
	}
	return strcmp(k->group, "RADIO") != 0 || !strcmp(r->control, "BW");
}

static void show_summary(void) {
	char text[SUMMARY_MAX];
	char *markup;
	int length;
	int i;

	length = snprintf(text, sizeof(text), "%s  %s", value_of("MODE"), value_of("BAND"));
	for (i = 0; i < row_count; i++) {
		if (!is_row_in_summary(&rows[i]) || length >= (int)sizeof(text))
			continue;
		length += snprintf(text + length, sizeof(text) - length, "    %s %s",
			label_of(rows[i].control), value_of(row_control(&rows[i])));
	}
	markup = g_markup_printf_escaped("<span font_desc='Sans 12'>%s</span>", text);
	gtk_label_set_markup(GTK_LABEL(summary_label), markup);
	g_free(markup);
}

static void show_values(void) {
	GtkStyleContext *style;
	int i;

	if (focus != ON_FREQUENCY && !is_row_shown(&rows[focus]))
		focus = ON_FREQUENCY;
	for (i = 0; i < row_count; i++) {
		gtk_label_set_text(GTK_LABEL(rows[i].value_label),
			value_of(row_control(&rows[i])));
		gtk_widget_set_visible(rows[i].box, is_row_shown(&rows[i]));
		style = gtk_widget_get_style_context(rows[i].box);
		if (i == focus)
			gtk_style_context_add_class(style, "focused");
		else
			gtk_style_context_remove_class(style, "focused");
	}
	show_frequency();
	show_summary();
	gtk_widget_queue_draw(spectrum_area);
}

static void send_line(const char *name, const char *value) {
	char line[LINE_MAX_LENGTH];

	if (writer == NULL)
		return;
	snprintf(line, sizeof(line), "%s %s\n", name, value);
	g_output_stream_write_all(writer, line, strlen(line), NULL, NULL, NULL);
}

static void tune(int steps) {
	struct known *k = find_known("FREQ");
	int hz = (atoi(k->value) / STEP_HZ + steps) * STEP_HZ;

	if (hz < LOWEST_HZ || hz > HIGHEST_HZ)
		return;
	snprintf(k->value, CONTROL_VALUE_MAX, "%d", hz);
	show_values();
	send_line("FREQ", k->value);
}

// the choice a step away from the present one, in a list such as
// "USB/LSB/CW"; it stops at either end
static void step_choice(struct known *k, int steps) {
	char list[CHOICES_MAX];
	char *names[CHOICES_MAX];
	char *name;
	int count = 0;
	int now = 0;
	int i;

	snprintf(list, sizeof(list), "%s", k->choices);
	for (name = strtok(list, "/"); name != NULL; name = strtok(NULL, "/"))
		names[count++] = name;
	if (count == 0)
		return;
	for (i = 0; i < count; i++) {
		if (!strcmp(names[i], k->value))
			now = i;
	}
	now += steps;
	if (now < 0)
		now = 0;
	if (now >= count)
		now = count - 1;
	snprintf(k->value, CONTROL_VALUE_MAX, "%s", names[now]);
}

static void step_number(struct known *k, int steps) {
	int value = atoi(k->value) + steps * k->step;

	if (value < k->min)
		value = k->min;
	if (value > k->max)
		value = k->max;
	snprintf(k->value, CONTROL_VALUE_MAX, "%d", value);
}

static void step_row(int row, int steps) {
	struct known *k = find_known(row_control(&rows[row]));
	char before[CONTROL_VALUE_MAX];

	if (k == NULL)
		return;
	if (!k->is_choice && k->step == 0) {
		show_status("waiting for the radio's schema");
		return;
	}
	snprintf(before, sizeof(before), "%s", k->value);
	if (k->is_choice)
		step_choice(k, steps);
	else
		step_number(k, steps);
	if (!strcmp(before, k->value))
		return;
	show_values();
	send_line(k->name, k->value);
}

static void step_focused(int steps) {
	if (focus == ON_FREQUENCY)
		tune(steps);
	else
		step_row(focus, steps);
}

// CONTROL <group> <name> number <min> <max> <step>, or
// CONTROL <group> <name> choice <a/b/c>
static void take_schema_line(char *rest) {
	char group[CONTROL_NAME_MAX];
	char name[CONTROL_NAME_MAX];
	char kind[CONTROL_NAME_MAX];
	char choices[CHOICES_MAX];
	struct known *k;
	int min;
	int max;
	int step;

	if (sscanf(rest, "%15s %15s %15s", group, name, kind) != 3)
		return;
	k = find_known(name);
	if (k == NULL)
		return;
	snprintf(k->group, CONTROL_NAME_MAX, "%s", group);
	if (!strcmp(kind, "number")
			&& sscanf(rest, "%*s %*s %*s %d %d %d", &min, &max, &step) == 3) {
		k->min = min;
		k->max = max;
		k->step = step;
	}
	if (!strcmp(kind, "choice") && sscanf(rest, "%*s %*s %*s %127s", choices) == 1) {
		k->is_choice = 1;
		snprintf(k->choices, CHOICES_MAX, "%s", choices);
	}
}

static int hex_digit(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0;
}

// blue rises, then green rises, then blue falls, then red rises, then green
// falls: the front panel's colours
static guint32 heat_map(int shade) {
	int band = shade / SHADES_A_BAND;
	int step = (shade % SHADES_A_BAND) * SHADE_STEP;

	if (band == 0)
		return step;
	if (band == 1)
		return (step << 8) | COLOUR_MAX;
	if (band == 2)
		return (COLOUR_MAX << 8) | (COLOUR_MAX - step);
	if (band == 3)
		return (step << 16) | (COLOUR_MAX << 8);
	if (band == 4)
		return (COLOUR_MAX << 16) | ((COLOUR_MAX - step) << 8);
	return COLOUR_MAX << 16;
}

// SPECTRUM <span in Hz> <two hex digits a point>; the newest line goes at
// the top of the waterfall and the rest move down a row
static void take_spectrum_line(const char *rest) {
	const char *hex = strchr(rest, ' ');
	unsigned char *pixels;
	guint32 *row;
	int stride;
	int i;

	if (hex == NULL)
		return;
	span_hz = atoi(rest);
	hex++;
	point_count = strlen(hex) / 2;
	if (point_count > POINTS_MAX)
		point_count = POINTS_MAX;
	for (i = 0; i < point_count; i++)
		points[i] = (uint8_t)(hex_digit(hex[2 * i]) * 16 + hex_digit(hex[2 * i + 1]));

	cairo_surface_flush(fall);
	pixels = cairo_image_surface_get_data(fall);
	stride = cairo_image_surface_get_stride(fall);
	memmove(pixels + stride, pixels, (size_t)stride * (FALL_ROWS - 1));
	row = (guint32 *)pixels;
	for (i = 0; i < point_count; i++)
		row[i] = heat_map(points[i] * SHADES_PER_DB);
	cairo_surface_mark_dirty(fall);
	gtk_widget_queue_draw(spectrum_area);
}

static void handle_line(const char *name, char *value) {
	struct known *k;

	if (!strcmp(name, "SPECTRUM")) {
		take_spectrum_line(value);
		return;
	}
	if (!strcmp(name, "CONTROL")) {
		if (strcmp(value, "END"))
			take_schema_line(value);
		else
			make_list();
		return;
	}
	if (!strcmp(name, "ERROR")) {
		show_status(value);
		return;
	}
	k = find_known(name);
	if (k == NULL)
		return;
	snprintf(k->value, CONTROL_VALUE_MAX, "%s", value);
	show_values();
}

static void disconnect(const char *why) {
	char text[LINE_MAX_LENGTH];

	g_clear_object(&reader);
	writer = NULL;
	g_clear_object(&connection);
	snprintf(text, sizeof(text), "%s: %s; trying again", radio_name, why);
	show_status(text);
	g_timeout_add(RETRY_EVERY_MS, (GSourceFunc)try_to_connect, NULL);
}

static void on_line(GObject *source, GAsyncResult *result, gpointer unused) {
	GError *error = NULL;
	char *line;
	char *value;

	(void)unused;
	line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source),
		result, NULL, &error);
	if (line == NULL) {
		if (error != NULL)
			g_error_free(error);
		disconnect("gone");
		return;
	}
	value = strchr(line, ' ');
	if (value != NULL) {
		*value = 0;
		handle_line(line, value + 1);
	}
	g_free(line);
	g_data_input_stream_read_line_async(reader, G_PRIORITY_DEFAULT, NULL,
		on_line, NULL);
}

static void on_connected(GObject *source, GAsyncResult *result, gpointer unused) {
	GError *error = NULL;
	char text[LINE_MAX_LENGTH];
	const char *asks = "schema\nspectrum on\n";

	(void)unused;
	connection = g_socket_client_connect_to_host_finish(G_SOCKET_CLIENT(source),
		result, &error);
	if (connection == NULL) {
		disconnect(error->message);
		g_error_free(error);
		return;
	}
	reader = g_data_input_stream_new(
		g_io_stream_get_input_stream(G_IO_STREAM(connection)));
	writer = g_io_stream_get_output_stream(G_IO_STREAM(connection));
	g_output_stream_write_all(writer, asks, strlen(asks), NULL, NULL, NULL);
	snprintf(text, sizeof(text), "connected to %s", radio_name);
	show_status(text);
	g_data_input_stream_read_line_async(reader, G_PRIORITY_DEFAULT, NULL,
		on_line, NULL);
}

static void try_to_connect(void) {
	char text[LINE_MAX_LENGTH];

	snprintf(text, sizeof(text), "connecting to %s", radio_name);
	show_status(text);
	g_socket_client_connect_to_host_async(connector, radio_name, radio_port,
		NULL, on_connected, NULL);
}

static int wheel_steps(const GdkEventScroll *event) {
	if (event->direction == GDK_SCROLL_UP)
		return 1;
	if (event->direction == GDK_SCROLL_DOWN)
		return -1;
	return 0;
}

static gboolean on_frequency_wheel(GtkWidget *widget, GdkEventScroll *event, gpointer unused) {
	(void)widget;
	(void)unused;
	tune(wheel_steps(event));
	return TRUE;
}

static gboolean on_frequency_click(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
	(void)widget;
	(void)event;
	(void)unused;
	focus = ON_FREQUENCY;
	show_values();
	return TRUE;
}

static gboolean on_row_wheel(GtkWidget *widget, GdkEventScroll *event, gpointer row) {
	(void)widget;
	step_row(GPOINTER_TO_INT(row), wheel_steps(event));
	return TRUE;
}

static gboolean on_row_click(GtkWidget *widget, GdkEventButton *event, gpointer row) {
	(void)widget;
	(void)event;
	focus = GPOINTER_TO_INT(row);
	show_values();
	return TRUE;
}

// the list is scrolled, if it must be, to show the row with the focus
static void scroll_to_focus(void) {
	GtkAdjustment *scroll;
	GtkAllocation where;
	double top;
	double page;

	if (focus == ON_FREQUENCY)
		return;
	scroll = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(list_scroller));
	gtk_widget_get_allocation(rows[focus].box, &where);
	top = gtk_adjustment_get_value(scroll);
	page = gtk_adjustment_get_page_size(scroll);
	if (where.y < top)
		gtk_adjustment_set_value(scroll, where.y);
	else if (where.y + where.height > top + page)
		gtk_adjustment_set_value(scroll, where.y + where.height - page);
}

// the next row shown, down the list or up it; past either end is the
// frequency
static void move_focus(int direction) {
	int next = focus;

	if (next == ON_FREQUENCY && direction < 0)
		next = row_count;
	do {
		next += direction;
	} while (next >= 0 && next < row_count && !is_row_shown(&rows[next]));
	if (next < 0 || next >= row_count)
		next = ON_FREQUENCY;
	focus = next;
	show_values();
	scroll_to_focus();
}

static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer unused) {
	(void)widget;
	(void)unused;
	if (event->keyval == GDK_KEY_Up) {
		step_focused(1);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Down) {
		step_focused(-1);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Tab) {
		move_focus(1);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_ISO_Left_Tab) {
		move_focus(-1);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Escape) {
		focus = ON_FREQUENCY;
		show_values();
		return TRUE;
	}
	return FALSE;
}

// where the receiver listens, in Hz from the middle of the spectrum, which
// is where the radio's oscillator puts the tuned frequency. In CW the
// oscillator is set off by the pitch, so that a signal on the tuned
// frequency lies the pitch away from the middle
static void passband(int *low_hz, int *high_hz, int *tuned_hz) {
	const char *mode = value_of("MODE");
	int pitch = atoi(value_of("PITCH"));
	int bw_cw = atoi(value_of("BW_CW"));
	int bw_voice = atoi(value_of("BW_VOICE"));

	*tuned_hz = 0;
	*low_hz = VOICE_LOW_HZ;
	*high_hz = VOICE_LOW_HZ + bw_voice;
	if (!strcmp(mode, "LSB")) {
		*low_hz = -VOICE_LOW_HZ - bw_voice;
		*high_hz = -VOICE_LOW_HZ;
	}
	if (!strcmp(mode, "AM")) {
		*low_hz = -bw_voice;
		*high_hz = bw_voice;
	}
	if (!strcmp(mode, "CW")) {
		*tuned_hz = pitch;
		*low_hz = pitch - bw_cw / 2;
		*high_hz = pitch + bw_cw / 2;
	}
	if (!strcmp(mode, "CWR")) {
		*tuned_hz = -pitch;
		*low_hz = -pitch - bw_cw / 2;
		*high_hz = -pitch + bw_cw / 2;
	}
}

static double x_of_hz(int hz, int width) {
	return width / 2.0 + (double)hz * width / span_hz;
}

static void draw_graph(cairo_t *cr, int width, int height) {
	int low_hz;
	int high_hz;
	int tuned_hz;
	double x;
	double y;
	int i;

	cairo_set_source_rgb(cr, 0.0, 0.05, 0.1);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	if (point_count < 2 || span_hz == 0)
		return;

	passband(&low_hz, &high_hz, &tuned_hz);
	cairo_set_source_rgba(cr, 0.4, 0.4, 0.5, 0.35);
	x = x_of_hz(low_hz, width);
	cairo_rectangle(cr, x, 0, x_of_hz(high_hz, width) - x, height);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, 1.0, 0.9, 0.0);
	cairo_set_line_width(cr, 1.0);
	for (i = 0; i < point_count; i++) {
		x = (double)i * width / (point_count - 1);
		y = height - (double)points[i] * height / FULL_SCALE_DB;
		if (y < 0)
			y = 0;
		if (i == 0)
			cairo_move_to(cr, x, y);
		else
			cairo_line_to(cr, x, y);
	}
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 1.0, 0.3, 0.3);
	x = x_of_hz(tuned_hz, width);
	cairo_move_to(cr, x, 0);
	cairo_line_to(cr, x, height);
	cairo_stroke(cr);
}

static void draw_waterfall(cairo_t *cr, int width, int height) {
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	if (point_count == 0)
		return;
	cairo_save(cr);
	cairo_scale(cr, (double)width / point_count, (double)height / FALL_ROWS);
	cairo_set_source_surface(cr, fall, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
	cairo_rectangle(cr, 0, 0, point_count, FALL_ROWS);
	cairo_fill(cr);
	cairo_restore(cr);
}

static gboolean on_draw_spectrum(GtkWidget *widget, cairo_t *cr, gpointer unused) {
	int width = gtk_widget_get_allocated_width(widget);
	int height = gtk_widget_get_allocated_height(widget);
	int graph_height = height / GRAPH_SHARE;

	(void)unused;
	draw_graph(cr, width, graph_height);
	cairo_translate(cr, 0, graph_height);
	draw_waterfall(cr, width, height - graph_height);
	return TRUE;
}

static int has_row(const char *control) {
	int i;

	for (i = 0; i < row_count; i++) {
		if (!strcmp(rows[i].control, control))
			return 1;
	}
	return 0;
}

static void add_row(const char *control, int is_under_mode) {
	GtkWidget *box;
	GtkWidget *line;
	GtkWidget *label;
	GtkWidget *value;
	int index = row_count;

	if (row_count == KNOWN_MAX || has_row(control))
		return;
	box = gtk_event_box_new();
	line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GAP);
	label = gtk_label_new(label_of(control));
	value = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_xalign(GTK_LABEL(value), 1);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "row");
	gtk_style_context_add_class(gtk_widget_get_style_context(label), "row-label");
	gtk_style_context_add_class(gtk_widget_get_style_context(value), "row-value");
	gtk_box_pack_start(GTK_BOX(line), label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(line), value, FALSE, FALSE, 0);
	gtk_widget_set_margin_start(line, ROW_MARGIN_SIDE);
	if (is_under_mode)
		gtk_widget_set_margin_start(line, UNDER_MODE_MARGIN);
	gtk_widget_set_margin_end(line, ROW_MARGIN_SIDE);
	gtk_widget_set_size_request(box, -1, ROW_HEIGHT);
	gtk_container_add(GTK_CONTAINER(box), line);
	gtk_widget_set_no_show_all(box, TRUE);
	gtk_widget_show_all(line);
	gtk_widget_add_events(box, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK);
	g_signal_connect(box, "scroll-event", G_CALLBACK(on_row_wheel), GINT_TO_POINTER(index));
	g_signal_connect(box, "button-press-event", G_CALLBACK(on_row_click),
		GINT_TO_POINTER(index));
	gtk_box_pack_start(GTK_BOX(list), box, FALSE, FALSE, 0);
	snprintf(rows[index].control, CONTROL_NAME_MAX, "%s", control);
	rows[index].box = box;
	rows[index].value_label = value;
	row_count++;
}

static int is_in_list(const char *control) {
	return strcmp(control, "FREQ") && strcmp(control, "BW_VOICE")
		&& strcmp(control, "BW_CW");
}

// the list, made again each time the radio sends its schema: the core
// controls, the rest of the radio's own, the mode, and under it BW and the
// modems' own; of those, only the mode in use's are shown
static void make_list(void) {
	int i;

	for (i = 0; i < row_count; i++)
		gtk_widget_destroy(rows[i].box);
	row_count = 0;
	focus = ON_FREQUENCY;
	for (i = 0; core_rows[i] != NULL; i++)
		add_row(core_rows[i], 0);
	for (i = 0; i < known_count; i++) {
		if (!strcmp(known[i].group, "RADIO") && strcmp(known[i].name, "MODE")
				&& is_in_list(known[i].name))
			add_row(known[i].name, 0);
	}
	add_row("MODE", 0);
	add_row("BW", 1);
	for (i = 0; i < known_count; i++) {
		if (known[i].group[0] != 0 && strcmp(known[i].group, "RADIO")
				&& is_in_list(known[i].name))
			add_row(known[i].name, 1);
	}
	show_values();
}

static void load_style(void) {
	GtkCssProvider *css = gtk_css_provider_new();
	const char *rules =
		"window { background-color: #101418; color: #d0d0d0; }"
		".sidebar { background-color: #161c22; }"
		".row { border-bottom: 1px solid #283038; border-left: 3px solid transparent; }"
		".row.focused { background-color: #283038; border-left-color: #ffd040; }"
		".row-label { font-size: 11pt; color: #8090a0; }"
		".row-value { font-size: 14pt; font-weight: bold; }";

	gtk_css_provider_load_from_data(css, rules, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);
}

static GtkWidget *create_main_area(void) {
	GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, GAP);
	GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GAP);

	frequency_box = gtk_event_box_new();
	frequency_label = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(frequency_box), frequency_label);
	gtk_widget_set_size_request(frequency_box, -1, ROW_HEIGHT);
	gtk_widget_add_events(frequency_box, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK);
	g_signal_connect(frequency_box, "scroll-event", G_CALLBACK(on_frequency_wheel), NULL);
	g_signal_connect(frequency_box, "button-press-event",
		G_CALLBACK(on_frequency_click), NULL);

	status_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(status_label), 1);
	gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_END);

	summary_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(summary_label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(summary_label), PANGO_ELLIPSIZE_END);

	spectrum_area = gtk_drawing_area_new();
	gtk_widget_set_size_request(spectrum_area, -1, SPECTRUM_MIN_HEIGHT);
	gtk_widget_set_vexpand(spectrum_area, TRUE);
	gtk_widget_add_events(spectrum_area, GDK_SCROLL_MASK);
	g_signal_connect(spectrum_area, "draw", G_CALLBACK(on_draw_spectrum), NULL);
	g_signal_connect(spectrum_area, "scroll-event", G_CALLBACK(on_frequency_wheel), NULL);

	gtk_box_pack_start(GTK_BOX(top), frequency_box, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(top), status_label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(column), top, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(column), summary_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(column), spectrum_area, TRUE, TRUE, 0);
	return column;
}

static GtkWidget *create_sidebar(void) {
	list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	list_scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(list_scroller), list);
	gtk_widget_set_size_request(list_scroller, SIDEBAR_WIDTH, -1);
	gtk_style_context_add_class(gtk_widget_get_style_context(list_scroller), "sidebar");
	return list_scroller;
}

static GtkWidget *create_window(void) {
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget *halves = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GAP);

	load_style();
	gtk_window_set_title(GTK_WINDOW(window), "zbitx");
	gtk_window_set_default_size(GTK_WINDOW(window), WINDOW_WIDTH, WINDOW_HEIGHT);
	gtk_container_set_border_width(GTK_CONTAINER(window), GAP);

	gtk_box_pack_start(GTK_BOX(halves), create_main_area(), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(halves), create_sidebar(), FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), halves);

	g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	return window;
}

int main(int argc, char **argv) {
	GtkWidget *window;

	gtk_init(&argc, &argv);
	if (argc > 1)
		radio_name = argv[1];
	if (argc > 2)
		radio_port = atoi(argv[2]);

	fall = cairo_image_surface_create(CAIRO_FORMAT_RGB24, POINTS_MAX, FALL_ROWS);
	window = create_window();
	gtk_widget_show_all(window);
	show_values();

	connector = g_socket_client_new();
	try_to_connect();
	gtk_main();
	return 0;
}
