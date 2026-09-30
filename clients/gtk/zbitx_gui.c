// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// zbitx_gui.c — a small window on the desktop that works the radio over the
// network.
//
//     zbitx_gui [radio] [port]
//
// The radio is sbitx.local unless another name or address is given, and the
// port 8081. The window has four controls:
//
//   the frequency   in large figures. The mouse wheel over it, the up and
//                   down keys, and the buttons either side, tune in steps
//                   of 1 kHz, onto the kilohertz.
//   the band        a drop-down of the bands. Choosing one goes to where the
//                   operator last was on that band.
//   the mode        a drop-down: USB, LSB, CW, CWR, AM.
//   the volume      a slider.
//
// It speaks the radio's control protocol, which server.c on the radio
// describes: lines of a name and a value. It sends a line when a control is
// moved, and shows the change at once, since the radio does not tell a
// client of its own changes. It shows what the radio tells it of everyone
// else's, so the window follows the radio when the front panel is turned.
//
// Showing what the radio says must not send it back again: that is how a
// change goes round in a loop. So while the window is being brought up to
// date the controls' own handlers do nothing.
//
// If the radio cannot be reached, or goes, the window says so and tries
// again every three seconds.
//
// It is built on the desktop with "make gui", which needs GTK 3.

#include <gtk/gtk.h>
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
#define VOLUME_MAX 100
#define LINE_MAX_LENGTH 128
#define TEXT_MAX 64
#define WINDOW_WIDTH 520
#define GAP 12
#define NOTHING_CHOSEN (-1)

static const char *band_names[] = {
	"80M", "60M", "40M", "30M", "20M", "17M", "15M", "12M", "10M", NULL
};
static const char *mode_names[] = { "USB", "LSB", "CW", "CWR", "AM", NULL };

static const char *radio_name = DEFAULT_RADIO;
static int radio_port = DEFAULT_PORT;

static GSocketClient *connector;
static GSocketConnection *connection;
static GDataInputStream *reader;
static GOutputStream *writer;

static GtkWidget *frequency_label;
static GtkWidget *band_choice;
static GtkWidget *mode_choice;
static GtkWidget *volume_slider;
static GtkWidget *status_label;

static int hz_now;
static gboolean is_catching_up;

static void try_to_connect(void);

static void show_status(const char *text) {
	gtk_label_set_text(GTK_LABEL(status_label), text);
}

static void show_frequency(void) {
	char text[TEXT_MAX];
	char markup[TEXT_MAX * 2];

	snprintf(text, sizeof(text), "%d.%03d.%03d", hz_now / HZ_PER_MHZ,
		(hz_now % HZ_PER_MHZ) / HZ_PER_KHZ, hz_now % HZ_PER_KHZ);
	snprintf(markup, sizeof(markup),
		"<span font_desc='Monospace Bold 36'>%s</span>", text);
	gtk_label_set_markup(GTK_LABEL(frequency_label), markup);
}

static int index_of(const char **names, const char *name) {
	int i;

	for (i = 0; names[i] != NULL; i++) {
		if (!strcmp(names[i], name))
			return i;
	}
	return NOTHING_CHOSEN;
}

static void send_line(const char *label, const char *value) {
	char line[LINE_MAX_LENGTH];

	if (writer == NULL)
		return;
	snprintf(line, sizeof(line), "%s %s\n", label, value);
	g_output_stream_write_all(writer, line, strlen(line), NULL, NULL, NULL);
}

static void send_number(const char *label, int value) {
	char text[TEXT_MAX];

	snprintf(text, sizeof(text), "%d", value);
	send_line(label, text);
}

static void tune(int steps) {
	int hz = (hz_now / STEP_HZ + steps) * STEP_HZ;

	if (hz < LOWEST_HZ || hz > HIGHEST_HZ)
		return;
	hz_now = hz;
	show_frequency();
	send_number("FREQ", hz);
}

static void handle_setting(const char *label, const char *value) {
	is_catching_up = TRUE;
	if (!strcmp(label, "FREQ")) {
		hz_now = atoi(value);
		show_frequency();
	}
	else if (!strcmp(label, "BAND"))
		gtk_combo_box_set_active(GTK_COMBO_BOX(band_choice),
			index_of(band_names, value));
	else if (!strcmp(label, "MODE"))
		gtk_combo_box_set_active(GTK_COMBO_BOX(mode_choice),
			index_of(mode_names, value));
	else if (!strcmp(label, "AUDIO"))
		gtk_range_set_value(GTK_RANGE(volume_slider), atoi(value));
	else if (!strcmp(label, "ERROR"))
		show_status(value);
	is_catching_up = FALSE;
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
		handle_setting(line, value + 1);
	}
	g_free(line);
	g_data_input_stream_read_line_async(reader, G_PRIORITY_DEFAULT, NULL,
		on_line, NULL);
}

static void on_connected(GObject *source, GAsyncResult *result, gpointer unused) {
	GError *error = NULL;
	char text[LINE_MAX_LENGTH];

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

static void on_band_chosen(GtkComboBox *box, gpointer unused) {
	int chosen = gtk_combo_box_get_active(box);

	(void)unused;
	if (is_catching_up || chosen == NOTHING_CHOSEN)
		return;
	send_line("BAND", band_names[chosen]);
}

static void on_mode_chosen(GtkComboBox *box, gpointer unused) {
	int chosen = gtk_combo_box_get_active(box);

	(void)unused;
	if (is_catching_up || chosen == NOTHING_CHOSEN)
		return;
	send_line("MODE", mode_names[chosen]);
}

static void on_volume_moved(GtkRange *range, gpointer unused) {
	(void)unused;
	if (is_catching_up)
		return;
	send_number("AUDIO", (int)gtk_range_get_value(range));
}

static void on_down(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	tune(-1);
}

static void on_up(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	tune(1);
}

static gboolean on_wheel(GtkWidget *widget, GdkEventScroll *event, gpointer unused) {
	(void)widget;
	(void)unused;
	if (event->direction == GDK_SCROLL_UP)
		tune(1);
	else if (event->direction == GDK_SCROLL_DOWN)
		tune(-1);
	return TRUE;
}

static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer unused) {
	(void)widget;
	(void)unused;
	if (event->keyval == GDK_KEY_Up) {
		tune(1);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Down) {
		tune(-1);
		return TRUE;
	}
	return FALSE;
}

static GtkWidget *combo_new(const char **names) {
	GtkWidget *box = gtk_combo_box_text_new();
	int i;

	for (i = 0; names[i] != NULL; i++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(box), names[i]);
	return box;
}

static GtkWidget *labelled_row(const char *label, GtkWidget *control) {
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GAP);

	gtk_box_pack_start(GTK_BOX(row), gtk_label_new(label), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), control, TRUE, TRUE, 0);
	return row;
}

static GtkWidget *create_window(void) {
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, GAP);
	GtkWidget *tuning = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GAP);
	GtkWidget *choices = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, GAP);
	GtkWidget *down = gtk_button_new_with_label("- 1 kHz");
	GtkWidget *up = gtk_button_new_with_label("+ 1 kHz");
	GtkWidget *wheel_area = gtk_event_box_new();

	gtk_window_set_title(GTK_WINDOW(window), "zbitx");
	gtk_window_set_default_size(GTK_WINDOW(window), WINDOW_WIDTH, -1);
	gtk_container_set_border_width(GTK_CONTAINER(window), GAP);

	frequency_label = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(wheel_area), frequency_label);
	gtk_widget_add_events(wheel_area, GDK_SCROLL_MASK);
	g_signal_connect(wheel_area, "scroll-event", G_CALLBACK(on_wheel), NULL);
	g_signal_connect(down, "clicked", G_CALLBACK(on_down), NULL);
	g_signal_connect(up, "clicked", G_CALLBACK(on_up), NULL);
	gtk_box_pack_start(GTK_BOX(tuning), down, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(tuning), wheel_area, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(tuning), up, FALSE, FALSE, 0);

	band_choice = combo_new(band_names);
	mode_choice = combo_new(mode_names);
	g_signal_connect(band_choice, "changed", G_CALLBACK(on_band_chosen), NULL);
	g_signal_connect(mode_choice, "changed", G_CALLBACK(on_mode_chosen), NULL);
	gtk_box_pack_start(GTK_BOX(choices), labelled_row("Band", band_choice), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(choices), labelled_row("Mode", mode_choice), TRUE, TRUE, 0);

	volume_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0,
		VOLUME_MAX, 1);
	g_signal_connect(volume_slider, "value-changed", G_CALLBACK(on_volume_moved), NULL);

	status_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(status_label), 0);

	gtk_box_pack_start(GTK_BOX(column), tuning, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(column), choices, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(column), labelled_row("Volume", volume_slider), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(column), status_label, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), column);

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

	window = create_window();
	show_frequency();
	gtk_widget_show_all(window);

	connector = g_socket_client_new();
	try_to_connect();
	gtk_main();
	return 0;
}
