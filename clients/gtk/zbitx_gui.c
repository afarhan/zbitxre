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
// It is laid out as the zbitx's front panel is, place for place, so that
// whoever knows one knows the other; front_panel/main.cpp has the panel's
// plan, and the places here are its places, in its pixels: the panel is
// 480 by 320, and every widget is 48 high. The window opens maximised, to
// fill the radio's screen, and the plan is stretched to whatever size the
// window is, across and down each by its own measure, the letters sized to
// go with it; restored from maximised it is 800 by 480, the size zbitxv2's
// window opened at, and at any size everything keeps its place in the plan.
//
//   y 0     STEP | the frequency, 144 wide | RIT, SPLIT, DRIVE, AGC, IF, VOL
//   y 48    a button for each band, 10M to 80M, and MODE
//   y 96    a column of buttons on the left, 48 wide: LOG, SET, TERM
//           beside it, 288 wide: the status line, 16 high, for what the
//           window and the radio have to say: the radio's remarks, such
//           as why a message could not be answered, and while an FT8
//           contact is under way how it stands, white on red, as on the
//           panel. In CW it has what the radio reads off the air, run
//           through from right to left as on the panel: each letter
//           comes in at the right end and what came before moves left
//           and off; a remark takes the line until the next letter
//           comes. It is the whole width of the graph; the power and
//           the SWR are laid over its right end only while there is
//           power to show; under it the graph, 64
//           high, and the waterfall, 96. The span is in the graph's top
//           left corner
//           on the right, 144 wide: the list, down to the foot of the
//           window
//   y 272   six places under the waterfall for the mode's own
//
// The top row is the frequency and the radio's core controls. Whatever
// else the radio has, that belongs to the radio as a whole or to the mode
// in use, is in the list, a row to each, which scrolls. The list is made
// from the radio's schema, so a control that a modem adds comes into it
// without this program knowing of it beforehand. BW is BW_VOICE in the
// voice modes and BW_CW in CW: the radio keeps one for each, as zbitxv2
// did. A modem's own controls are shown only in its modes: a control in
// the group CW in CW and CWR, one in VOICE in USB, LSB and AM.
//
// The frequency's box has two lines, as zbitxv2's window had, by its
// draw_dial(): a small one over a large one. The large line is the
// frequency in use and the small line the one in waiting, each with what
// it is before it:
//
//   as a rule            small  the other VFO, "B:7.050.000"
//                        large  the VFO in use, "A:7.100.000"; while the
//                               radio transmits, "TX:7.100.000"
//   RIT or SPLIT on      small  where the transmitter is to be, "TX:..."
//                        large  where the receiver listens, "RX:..."
//                        and the two change places while the radio
//                        transmits, so that the large line is always
//                        where the radio is working
//
// Each VFO has a dial of its own, and the one in waiting stays where it
// was left while the other is tuned. The radio keeps the two dials for
// its clients, as its controls VFO_A and VFO_B, which the front panel
// uses too: the window takes the waiting VFO's from there, and tells the
// radio both when the VFOs are changed over. A VFO that has never been
// used is put, once, where the radio was when the window first heard
// from it. In the box's top right corner is the time in UTC, to the
// second, with a Z after it, as on the panel, "01:15:22Z", and "--:--:--Z"
// while the radio does not know the time. It is the radio's time and not
// this computer's: the window asks the radio the time once, on connecting,
// and counts the seconds on from the answer by its own clock; the radio's
// CLOCK changes as each minute begins, and the count is set right by that.
// RIT and SPLIT are red while they are on, as on the
// panel, where the author's reason is given: they are switched on and
// then forgotten.
//
// A band's button goes to that band. The button of the band in use goes
// to the next place in that band's stack, and from the last to the first.
// A tap on the graph or the waterfall goes to the next span, and from the
// narrowest back to the widest: 25K, 10K, 6K, 2.5K, the radio's control
// SPAN, which the panel shares. The radio sends a client the whole of what
// its receiver's bins cover, some 25 kHz, at their full resolution, and
// the window shows the middle of it that the span calls for, so a
// narrower span is the same spectrum drawn larger.
//
// On the bottom row, in USB, LSB and AM there is MIC, and two buttons, TX
// and RX: TX starts transmitting and RX stops it, two buttons apart as the
// author wanted on the panel. TX is red while the radio transmits and RX
// has the bright edge while it receives. 2TONE has the same two. In CW the
// places are the first six of the radio's macros, each sent at a click. In
// FT8 the row is empty, and the column's third and fourth buttons are CQ
// and HALT.
//
// In FT8 the graph and the waterfall are not shown. In their place, and
// down to the foot of the window, is the list of the messages the radio
// has copied, coloured as the panel's is: the time each was sent at, how
// strong it was, its pitch and its text, the newest at the bottom and kept
// in view. The rows of one slot are told from those of the next by their
// ground, a little lighter for every other slot; a station calling CQ is
// in green; and what the radio itself sent is in red, with TX where the
// strength would be. A tap on a message marks it, and the list then stays
// where it is as more come, so that the marked one does not run off under
// the finger, and the status line says how it is answered: by a tap on it
// again, or by a press of the knob that works the selected control, as a
// click of the panel's knob does. Its line then goes back to the radio,
// which begins the contact. A mark not touched for thirty seconds is taken
// off, as on the panel. BW is not shown in FT8: its
// passband is the modem's own, 50 to 4000 Hz, and is not the voice modes'
// bandwidth.
//
// The two lists scroll, by the wheel, their bars, or a finger drawn up or
// down them. A row's tap is a press and a release in one place: a finger
// that moves is scrolling, and selects nothing.
//
// TX, the radio's own switch between receive and transmit, is on the
// bottom row as the two buttons in the modes that have them, and in the
// other modes it is a row of the list, so that there is always a way to
// put the radio on the air and take it off.
//
// SET opens the settings, a screen of their own in the window, as on the
// panel: a row to each of the radio's controls in its group STATION, the
// callsign and the grid, and how FT8 is worked. A tap on a choice moves it
// to its next. A tap on a text opens the screen it is typed on. The arrow
// at the top left of each screen goes back. Without a callsign the radio
// cannot call CQ in FT8 or answer anyone.
//
// TYPING
//
// Wherever a text is to be typed, the window's own keyboard comes up, on a
// screen with the text in a box in its upper part and the keyboard in its
// lower part, as the author's rule for the panel has it, so that the
// keyboard never covers what is typed. The radio has no other keyboard to
// count on. It has only the keys a callsign, a grid or a short message
// needs: the figures, the capital letters, the stroke, the stop, the
// question mark and the dash, a space, and one to rub out. SAVE gives the
// text to the radio and CANCEL drops it. A keyboard plugged into the
// radio works too: what it types goes into the box, in capitals, Enter
// saves and Escape cancels.
//
// What the panel has and the window has not yet: the logbook and the
// keyboard behind LOG and TERM, which are shown dim and do nothing. The
// window has no MENU button.
//
// A control has its name over its value, in cyan, in the middle of its
// tile with a little room above it: the author found the names too small
// and too faint in grey at the tiles' corners. The frequency's two lines
// are both in the fixed-width letters, bold, in which the stops between
// the MHz, the kHz and the Hz are plain to see, the small line in cyan;
// the two are one column in the middle of the box and begin at the same
// place, the small line right over the large one and not in the box's
// corner. The time is in white. Nothing in the window is in small grey
// letters but the span in the graph's corner.
//
// A control is selected by a click on it, or a touch, and has a bright edge
// while it is. The mouse wheel over a control moves its value a step; over
// the frequency, the graph or the waterfall it tunes. A click on a choice
// that is already selected moves it to its next choice, and round from the
// last to the first, so RIT is switched with two clicks; a click on the
// frequency when it is selected goes to the other VFO. On the keyboard, Tab
// and Shift-Tab move the selection, the up and down keys move what is
// selected, and Escape goes back to the frequency.
//
// TUNING
//
// The radio knows only FREQ, the frequency it is tuned to. STEP, RIT,
// SPLIT and VFO are the window's own, as they were zbitxv2's window's, and
// every client keeps its own: the front panel's knob is on the panel, and
// tunes it as the wheel tunes this.
//
//   STEP     how far a turn of the wheel tunes: 10H, 100H, 500H, 1K or
//            10K. The dial moves by STEP, onto a multiple of it.
//   VFO      A or B, each with a dial of its own. Choosing the other keeps
//            the dial of the one left, and goes to the other's; one not
//            yet used opens where the other is.
//   RIT      OFF or ON. While it is on, the wheel moves the receiver's
//            offset, RIT_DELTA, by STEP, up to 25 kHz either way, and the
//            dial stays; the receiver listens at the dial + RIT_DELTA, and
//            the transmitter is to be on the dial.
//   SPLIT    OFF or ON. While it is on, the transmitter is to be on the
//            other VFO's dial. Turning it on goes to VFO A, so that it
//            receives on A and transmits on B.
//
// The window sends the radio, as FREQ, where the receiver listens. The
// large figures are that frequency, and where the transmitter is to be is
// beside them when it is elsewhere, as it is with RIT or SPLIT on. With
// RIT or SPLIT on, FREQ is to change at each switch between receive and
// transmit, to where the transmitter is to be and back: the radio does not
// say yet when it switches, since it has no transmitter yet.
//
// When the radio tells a new FREQ, from a band chosen or the front panel,
// that is where the receiver listens, and the dial is worked back from it.
//
// THE KNOBS
//
// The sbitx has two knobs on its front, wired to the Pi it runs this window
// on, and the window reads them, as zbitxv2's window did. One tunes and
// does nothing else: a notch is a turn of the wheel on the frequency,
// whatever is selected. The other changes the value of the control that is
// selected, the row that has the focus, a notch for a step; with no row
// selected it works VOL. A press of its button answers the FT8 message
// that is marked, if one is; with none marked it selects the next control
// down the list, so the radio can be worked with no touch of the screen.
// The tuning knob's button does nothing yet.
//
// The knobs are the window's alone. The radio program, which runs with no
// screen, knows nothing of them and has none of this in it. All the window
// takes from the radio's side is the radio's name, to know whether it is on
// one that has knobs: it reads data/hw_settings.ini in the folder it is
// started in, the file the radio program reads. On a desktop there is no
// such file and no knobs are looked for. A zbitx has none on the Pi: its
// one knob is on the front panel.
//
// The sbitx's, from sbitx_gtk.c, whose numbers for the pins are wiringPi's;
// here they are the Pi's own. A knob's two contacts are in the order
// sbitx_gtk.c gave them to its enc_init(), which is what makes clockwise
// count upward.
//
//     pin  header  name in sbitx_gtk.c
//      17    11    ENC2_A            the tuning knob
//      27    13    ENC2_B
//      22    15    ENC2_SW           its button
//      10    19    ENC1_B            the knob for the selected control
//       9    21    ENC1_A
//      11    23    ENC1_SW           its button
//
// A knob has to be looked at more often than a window is woken, or notches
// are lost when it is spun, so a thread of its own looks at both, twice a
// millisecond, and counts; the window takes the counts up to act on them
// every few milliseconds. knob.c says how a knob is read.
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
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "board.h"
#include "gpio.h"
#include "knob.h"

#define DEFAULT_RADIO "sbitx.local"
#define DEFAULT_PORT 8081
#define RETRY_EVERY_MS 3000
#define HZ_PER_MHZ 1000000
#define HZ_PER_KHZ 1000
#define LINE_MAX_LENGTH 128
#define TEXT_MAX 64
#define GAP 6

// the panel's plan, in the panel's pixels
#define PANEL_WIDTH 480
#define PANEL_HEIGHT 320
#define ROW_HEIGHT 48
#define TILE_WIDTH 48
#define FREQ_LEFT 48
#define FREQ_WIDTH 144
#define BANDS_TOP 48
#define MIDDLE_TOP 96
#define COLUMN_BUTTONS 4
#define FT8_CQ_ROW 2
#define FT8_HALT_ROW 3
#define GRAPH_LEFT 48
#define GRAPH_WIDTH 288
#define STATUS_HEIGHT 16
#define GRAPH_HEIGHT 64
#define FALL_HEIGHT 96
#define METER_WIDTH 112
#define LIST_LEFT 336
#define LIST_WIDTH 144
#define LIST_HEIGHT (PANEL_HEIGHT - MIDDLE_TOP)
#define FOOTER_TOP 272
#define FOOTER_TILES 6
#define BANDS 9
#define ROW_MARGIN_SIDE 6
#define TITLE_SIZE 11
#define CORNER_SIZE 9
#define CLOCK_SIZE 10
#define WAITING_SIZE 12
#define TITLE_ROOM_ABOVE 4
#define VALUE_SIZE 13
#define FREQUENCY_SIZE 17
#define STATUS_SIZE 10
#define SPAN_LEFT 3
#define SPAN_BASELINE 11

// the size zbitxv2's window opened at
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 480
#define PLACED_MAX 128
#define CSS_MAX 2048
#define MESSAGES_SHOWN_MAX 200
#define MESSAGE_FIELDS 4
#define MESSAGE_SIZE 10
#define MARK_KEPT_FOR_S 30
#define SLOT_SECONDS 15
#define SECONDS_A_MINUTE 60
#define TIME_DIGITS 6
#define TIME_MINUTE_AT 2
#define TIME_SECOND_AT 4
#define DIGITS_OF_TWO 100
#define CLOCK_SHOWN_EVERY_MS 250
#define CW_READ_MAX 80
#define SECONDS_AN_HOUR 3600
#define SECONDS_A_DAY 86400
#define HALF_A_MINUTE 30
#define MICROS_A_SECOND 1000000
#define TENTHS 10

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
#define FT8_LOW_HZ 50
#define FT8_HIGH_HZ 4000
#define PITCH_DASH 3.0

#define HW_SETTINGS_FILE "data/hw_settings.ini"
#define KNOBS_LOOKED_AT_EVERY_US 500
#define KNOBS_TAKEN_UP_EVERY_MS 10
#define BUTTON_DOWN_LOOKS 40
#define VOLUME_CONTROL "AUDIO"

#define LOWEST_HZ 500000
#define HIGHEST_HZ 30000000
#define RIT_MAX_HZ 25000
#define HZ_PER_K 1000
#define VFO_A 0
#define VFO_B 1
#define VFOS 2

// a control: one the radio has told of, its value, and from the schema its
// kind and range; or one of the window's own, which it never sends
struct known {
	char name[CONTROL_NAME_MAX];
	char group[CONTROL_NAME_MAX];
	char value[CONTROL_VALUE_MAX];
	int is_own;
	int is_choice;
	int is_text;
	int is_listed;
	int min;
	int max;
	int step;
	char choices[CHOICES_MAX];
};

// a row of the list: the control it works, or BW, which works BW_VOICE or
// BW_CW with the mode
// where a row is put: at a place of its own in the top two rows, at one on
// the bottom row, or in the list
enum place {
	ON_TOP,
	IN_FOOTER,
	IN_LIST,
};

// a control that has a place of its own, as the panel has it
struct top_place {
	const char *control;
	int x;
	int y;
};

// what a mode has on the bottom row besides its controls: a button that
// puts a control at a choice
struct footer_button {
	const char *group;
	int slot;
	const char *label;
	const char *control;
	const char *choice;
};

struct row {
	char control[CONTROL_NAME_MAX];
	enum place place;
	GtkWidget *box;
	GtkWidget *value_label;
};

static const struct top_place top_places[] = {
	{"STEP", 0, 0},
	{"RIT", 192, 0},
	{"SPLIT", 240, 0},
	{"DRIVE", 288, 0},
	{"AGC", 336, 0},
	{"IF", 384, 0},
	{"AUDIO", 432, 0},
	{"MODE", 432, BANDS_TOP},
	{NULL, 0, 0},
};

// the controls on the bottom row, each in the mode of its group
static const struct top_place footer_places[] = {
	{"MIC", 0, FOOTER_TOP},
	{NULL, 0, 0},
};

static const struct footer_button footer_buttons[] = {
	{"VOICE", 1, "TX", "TX", "ON"},
	{"VOICE", 2, "RX", "TX", "OFF"},
	{"2TONE", 0, "TX", "TX", "ON"},
	{"2TONE", 1, "RX", "TX", "OFF"},
	{NULL, 0, NULL, NULL, NULL},
};

static const char *const band_names[BANDS] = {
	"10M", "12M", "15M", "17M", "20M", "30M", "40M", "60M", "80M",
};

static const char *const column_names[COLUMN_BUTTONS] = { "LOG", "SET", "TERM", "" };
#define MACRO_GROUP "CW"
#define MACRO_CONTROL "MACRO"
#define MESSAGES_GROUP "FT8"
#define FT8_DO_CONTROL "FT8_DO"
#define SET_ROW 1
#define STATION_GROUP "STATION"
#define PAGE_MAIN "main"
#define PAGE_SETTINGS "settings"
#define PAGE_TEXT "text"
#define KEY_ROWS 4
#define KEYS_A_ROW 10
#define CANCEL_KEYS 3
#define SPACE_KEYS 3
#define RUB_OUT_KEYS 2
#define SAVE_KEYS 2
#define TYPED_SIZE 16

static const char *radio_name = DEFAULT_RADIO;
static int radio_port = DEFAULT_PORT;

static GSocketClient *connector;
static GSocketConnection *connection;
static GDataInputStream *reader;
static GOutputStream *writer;

// what has been put on the screen, and where in the panel's plan, so that
// it can be put again when the window's size changes
struct placed {
	GtkWidget *widget;
	int x;
	int y;
	int width;
	int height;
};

static struct placed placed[PLACED_MAX];
static double scale_across = (double)WINDOW_WIDTH / PANEL_WIDTH;
static double scale_down = (double)WINDOW_HEIGHT / PANEL_HEIGHT;
static double scale = (double)WINDOW_HEIGHT / PANEL_HEIGHT;
static int laid_out_width;
static int laid_out_height;
static int is_lay_out_due;
static GtkCssProvider *css;
static GtkWidget *screen;
static GtkWidget *frequency_box;
static GtkWidget *frequency_label;
static GtkWidget *vfo_label;
static GtkWidget *clock_label;
static GtkWidget *spectrum_area;
static GtkWidget *status_label;
static GtkWidget *meter_label;
static GtkWidget *list;
static GtkWidget *list_scroller;
static GtkWidget *messages;
static GtkWidget *messages_scroller;
static GtkWidget *marked_message;
static GtkWidget *pages;
static GtkWidget *settings_list;
static GtkWidget *text_title;
static GtkWidget *typed_label;
// the newest of what the radio has read of CW, the latest at the end
static char cw_read[CW_READ_MAX + 1];
static struct known *typed_for;
// how far the radio's time is ahead of this computer's steady clock, in
// seconds, once the radio has said the time
static gint64 radio_ahead_s;
static int is_time_known;
static char typed[CONTROL_VALUE_MAX];
static guint mark_timer;
static int messages_shown;
static GtkWidget *band_box[BANDS];
static GtkWidget *column_box[COLUMN_BUTTONS];
static GtkWidget *column_label[COLUMN_BUTTONS];
static GtkWidget *footer_box[FOOTER_TILES];
static GtkWidget *footer_label[FOOTER_TILES];

static struct known known[KNOWN_MAX];
static int known_count;
static struct row rows[KNOWN_MAX];
static int row_count;
static int focus = ON_FREQUENCY;

static int dial_hz;
static int vfo_dial_hz[VFOS];
static int vfo_in_use = VFO_A;

static int span_hz;
static int point_count;
static uint8_t points[POINTS_MAX];
static cairo_surface_t *fall;

// a radio's two knobs: each one's two contacts and its button
struct knob_pins {
	int a_pin;
	int b_pin;
	int button_pin;
};

struct radio_knobs {
	const char *radio;
	struct knob_pins tuning;
	struct knob_pins control;
};

static const struct radio_knobs radios_with_knobs[] = {
	{
		.radio = "sbitxv3",
		.tuning = { 17, 27, 22 },
		.control = { 10, 9, 11 },
	},
	{ .radio = NULL },
};

static struct knob tuning_knob;
static struct knob control_knob;
static int control_button_pin;
static atomic_int tuning_notches;
static atomic_int control_notches;
static atomic_int control_presses;

static void try_to_connect(void);
static void make_list(void);
static void add_label_class(GtkWidget *label, const char *name);
static void set_class(GtkWidget *widget, const char *name, int on);
static void tell_taps(GtkWidget *widget, GCallback tapped, gpointer data);
static GtkWidget *list_row_new(const char *name, GtkWidget **value);

// a remark on the status line, from its left end
static void show_status(const char *text) {
	set_class(status_label, "warning", 0);
	gtk_label_set_xalign(GTK_LABEL(status_label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_END);
	gtk_label_set_text(GTK_LABEL(status_label), text);
}

// something the radio will not do until the operator puts it right: in bold,
// in a colour of its own, until the next remark
static void show_warning(const char *text) {
	show_status(text);
	set_class(status_label, "qso", 0);
	set_class(status_label, "warning", 1);
}

// CW read off the air: the characters join the end of what was read
// before, the oldest go to make room, and the line shows the end of it,
// against its right end
static void take_cw_line(const char *text) {
	size_t had = strlen(cw_read);
	size_t more = strlen(text);

	if (more > CW_READ_MAX) {
		text += more - CW_READ_MAX;
		more = CW_READ_MAX;
	}
	if (had + more > CW_READ_MAX) {
		memmove(cw_read, cw_read + had + more - CW_READ_MAX, CW_READ_MAX - more);
		had = CW_READ_MAX - more;
	}
	memcpy(cw_read + had, text, more + 1);
	gtk_label_set_xalign(GTK_LABEL(status_label), 1);
	gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_START);
	gtk_label_set_text(GTK_LABEL(status_label), cw_read);
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

// whether the mode in use has a button on the bottom row that works the
// control
static int has_footer_button(const char *control) {
	const struct footer_button *b;

	for (b = footer_buttons; b->group != NULL; b++) {
		if (!strcmp(b->control, control) && !strcmp(b->group, mode_group()))
			return 1;
	}
	return 0;
}

static int is_row_shown(const struct row *r) {
	struct known *k = find_known(row_control(r));

	if (k == NULL)
		return 0;
	if (r->place == IN_LIST && has_footer_button(r->control))
		return 0;
	if (!strcmp(r->control, "BW"))
		return strcmp(mode_group(), MESSAGES_GROUP) != 0;
	if (!strcmp(k->group, "RADIO") || r->place == ON_TOP)
		return 1;
	return !strcmp(k->group, mode_group());
}

static int is_on(const char *name) {
	return !strcmp(value_of(name), "ON");
}

// where the receiver listens: the dial, moved by RIT_DELTA while RIT is on
static int receiver_hz(void) {
	if (is_on("RIT"))
		return dial_hz + atoi(value_of("RIT_DELTA"));
	return dial_hz;
}

// where the transmitter is to be: the dial, or while SPLIT is on the other
// VFO's dial, if that VFO has been used
static int transmitter_hz(void) {
	int other = VFO_B;

	if (!is_on("SPLIT"))
		return dial_hz;
	if (vfo_in_use == VFO_B)
		other = VFO_A;
	if (vfo_dial_hz[other] == 0)
		return dial_hz;
	return vfo_dial_hz[other];
}

static void format_frequency(char *text, int room, int hz) {
	snprintf(text, room, "%d.%03d.%03d", hz / HZ_PER_MHZ,
		(hz % HZ_PER_MHZ) / HZ_PER_KHZ, hz % HZ_PER_KHZ);
}

// the panel's pixels in the window's: across, down, and for what has no
// direction, the letters and the margins, by the smaller of the two
static int px(double panel_pixels) {
	return (int)(panel_pixels * scale + 0.5);
}

static int px_across(double panel_pixels) {
	return (int)(panel_pixels * scale_across + 0.5);
}

static int px_down(double panel_pixels) {
	return (int)(panel_pixels * scale_down + 0.5);
}

static void set_class(GtkWidget *widget, const char *name, int is_wanted) {
	GtkStyleContext *style = gtk_widget_get_style_context(widget);

	if (is_wanted)
		gtk_style_context_add_class(style, name);
	else
		gtk_style_context_remove_class(style, name);
}

// the nth of a control's choices, from 0, "USB/LSB/CW"; 0 if there are not
// so many
static int choice_at(const char *choices, int n, char *choice, int room) {
	char list[CHOICES_MAX];
	char *name;

	snprintf(list, sizeof(list), "%s", choices);
	for (name = strtok(list, "/"); name != NULL; name = strtok(NULL, "/")) {
		if (n == 0) {
			snprintf(choice, room, "%s", name);
			return 1;
		}
		n--;
	}
	return 0;
}

static const char *const vfo_dial_names[VFOS] = { "VFO_A", "VFO_B" };

static int vfo_in_waiting(void) {
	if (vfo_in_use == VFO_B)
		return VFO_A;
	return VFO_B;
}

// one line of the frequency's box: what the frequency is, and the frequency
static void format_dial_line(char *text, int room, const char *what, int hz) {
	char figures[TEXT_MAX / 2];

	format_frequency(figures, sizeof(figures), hz);
	snprintf(text, room, "%.2s:%s", what, figures);
}

// the frequency's two lines, as zbitxv2's draw_dial() had them: the large
// one is where the radio is working and the small one what is in waiting
static void show_frequency(void) {
	int receive_hz = receiver_hz();
	int transmit_hz = transmitter_hz();
	int is_transmitting = is_on("TX");
	int other = vfo_in_waiting();
	const char *other_name = "B";
	char small_line[TEXT_MAX];
	char large_line[TEXT_MAX];

	if (other == VFO_A)
		other_name = "A";
	if (receive_hz != transmit_hz && is_transmitting) {
		format_dial_line(small_line, sizeof(small_line), "RX", receive_hz);
		format_dial_line(large_line, sizeof(large_line), "TX", transmit_hz);
	}
	else if (receive_hz != transmit_hz) {
		format_dial_line(small_line, sizeof(small_line), "TX", transmit_hz);
		format_dial_line(large_line, sizeof(large_line), "RX", receive_hz);
	}
	else {
		format_dial_line(small_line, sizeof(small_line), other_name, dial_hz);
		if (vfo_dial_hz[other] != 0)
			format_dial_line(small_line, sizeof(small_line), other_name,
				vfo_dial_hz[other]);
		format_dial_line(large_line, sizeof(large_line), value_of("VFO"), dial_hz);
		if (is_transmitting)
			format_dial_line(large_line, sizeof(large_line), "TX", dial_hz);
	}
	gtk_label_set_text(GTK_LABEL(vfo_label), small_line);
	gtk_label_set_text(GTK_LABEL(frequency_label), large_line);
	set_class(frequency_box, "focused", focus == ON_FREQUENCY);
}

static gint64 steady_s(void) {
	return g_get_monotonic_time() / MICROS_A_SECOND;
}

// the time in UTC, to the second, in the frequency's box
static gboolean show_clock(gpointer unused) {
	gint64 now = steady_s() + radio_ahead_s;
	int second_of_day = (int)(now % SECONDS_A_DAY);
	char time[TEXT_MAX];

	(void)unused;
	snprintf(time, sizeof(time), "--:--:--Z");
	if (is_time_known)
		snprintf(time, sizeof(time), "%02d:%02d:%02dZ", second_of_day / SECONDS_AN_HOUR,
			(second_of_day % SECONDS_AN_HOUR) / SECONDS_A_MINUTE,
			second_of_day % SECONDS_A_MINUTE);
	gtk_label_set_text(GTK_LABEL(clock_label), time);
	return G_SOURCE_CONTINUE;
}

// TIME 1791397105: the radio's time, or 0 if it does not know it
static void take_time_line(const char *value) {
	gint64 radio_s = g_ascii_strtoll(value, NULL, 10);

	is_time_known = radio_s > 0;
	radio_ahead_s = radio_s - steady_s();
}

// the radio's CLOCK has changed, which it does as a minute begins: the
// count of seconds is moved to the nearest minute
static void take_start_of_minute(void) {
	gint64 now = steady_s() + radio_ahead_s;
	gint64 minute = ((now + HALF_A_MINUTE) / SECONDS_A_MINUTE) * SECONDS_A_MINUTE;

	if (is_time_known)
		radio_ahead_s += minute - now;
}

// what is at a place on the bottom row in the mode in use: the button's
// label and what a click on it sends, or nothing
static int footer_button_at(int slot, char *label, char *control, char *choice, int room) {
	const struct footer_button *b;
	struct known *macros = find_known(MACRO_CONTROL);
	const char *group = mode_group();

	for (b = footer_buttons; b->group != NULL; b++) {
		if (b->slot == slot && !strcmp(b->group, group)) {
			snprintf(label, room, "%s", b->label);
			snprintf(control, room, "%s", b->control);
			snprintf(choice, room, "%s", b->choice);
			return 1;
		}
	}
	if (strcmp(group, MACRO_GROUP) || macros == NULL)
		return 0;
	if (!choice_at(macros->choices, slot, choice, room))
		return 0;
	snprintf(label, room, "%s", choice);
	snprintf(control, room, "%s", MACRO_CONTROL);
	return 1;
}

static void show_buttons(void) {
	char label[CONTROL_VALUE_MAX];
	char control[CONTROL_VALUE_MAX];
	char choice[CONTROL_VALUE_MAX];
	int is_ft8 = !strcmp(mode_group(), MESSAGES_GROUP);
	int is_a_button;
	int is_in_force;
	int i;

	for (i = 0; i < BANDS; i++)
		set_class(band_box[i], "in-use", !strcmp(value_of("BAND"), band_names[i]));
	for (i = 0; i < FOOTER_TILES; i++) {
		is_a_button = footer_button_at(i, label, control, choice, sizeof(label));
		gtk_widget_set_visible(footer_box[i], is_a_button);
		if (!is_a_button)
			continue;
		gtk_label_set_text(GTK_LABEL(footer_label[i]), label);
		is_in_force = !strcmp(control, "TX") && !strcmp(value_of("TX"), choice);
		set_class(footer_box[i], "on", is_in_force && !strcmp(choice, "ON"));
		set_class(footer_box[i], "focused", is_in_force && !strcmp(choice, "OFF"));
	}
	for (i = 0; i < COLUMN_BUTTONS; i++) {
		gtk_label_set_text(GTK_LABEL(column_label[i]), column_names[i]);
		set_class(column_box[i], "dim", i != SET_ROW);
		gtk_widget_set_visible(column_box[i], column_names[i][0] != 0);
	}
	if (!is_ft8)
		return;
	gtk_label_set_text(GTK_LABEL(column_label[FT8_CQ_ROW]), "CQ");
	gtk_label_set_text(GTK_LABEL(column_label[FT8_HALT_ROW]), "HALT");
	set_class(column_box[FT8_CQ_ROW], "dim", 0);
	set_class(column_box[FT8_HALT_ROW], "dim", 0);
	gtk_widget_set_visible(column_box[FT8_HALT_ROW], TRUE);
}

// the power and the SWR, at the right end of the status line while the
// radio says there is power
static void show_meter(void) {
	int power = atoi(value_of("POWER"));
	int swr = atoi(value_of("SWR"));
	char text[TEXT_MAX];

	text[0] = 0;
	if (power > 0)
		snprintf(text, sizeof(text), "%d.%d W  SWR %d.%d", power / TENTHS,
			power % TENTHS, swr / TENTHS, swr % TENTHS);
	gtk_label_set_text(GTK_LABEL(meter_label), text);
	gtk_widget_set_visible(meter_label, text[0] != 0);
}

static void show_values(void) {
	int i;

	if (focus != ON_FREQUENCY && !is_row_shown(&rows[focus]))
		focus = ON_FREQUENCY;
	for (i = 0; i < row_count; i++) {
		gtk_label_set_text(GTK_LABEL(rows[i].value_label),
			value_of(row_control(&rows[i])));
		gtk_widget_set_visible(rows[i].box, is_row_shown(&rows[i]));
		set_class(rows[i].box, "focused", i == focus);
		set_class(rows[i].box, "on", (!strcmp(rows[i].control, "RIT")
			|| !strcmp(rows[i].control, "SPLIT")) && is_on(rows[i].control));
	}
	show_frequency();
	show_buttons();
	show_meter();
	// in FT8 the list of messages is where the graph and the waterfall are
	gtk_widget_set_visible(messages_scroller, !strcmp(mode_group(), MESSAGES_GROUP));
	gtk_widget_set_visible(spectrum_area, strcmp(mode_group(), MESSAGES_GROUP) != 0);
	gtk_widget_queue_draw(spectrum_area);
}

static void send_line(const char *name, const char *value) {
	char line[LINE_MAX_LENGTH];

	if (writer == NULL)
		return;
	snprintf(line, sizeof(line), "%.*s %.*s\n", CONTROL_NAME_MAX - 1, name,
		CONTROL_VALUE_MAX - 1, value);
	g_output_stream_write_all(writer, line, strlen(line), NULL, NULL, NULL);
}

// the radio is sent where the receiver listens
static void send_frequency(void) {
	char text[CONTROL_VALUE_MAX];
	struct known *k = find_known("FREQ");

	snprintf(text, sizeof(text), "%d", receiver_hz());
	if (k != NULL)
		snprintf(k->value, CONTROL_VALUE_MAX, "%s", text);
	send_line("FREQ", text);
}

// STEP in Hz: 10H is 10, 1K is 1000
static int step_hz(void) {
	const char *step = value_of("STEP");
	int hz = atoi(step);

	if (strchr(step, 'K') != NULL)
		hz *= HZ_PER_K;
	if (hz <= 0)
		hz = HZ_PER_K;
	return hz;
}

static int clamp(int value, int lowest, int highest) {
	if (value < lowest)
		return lowest;
	if (value > highest)
		return highest;
	return value;
}

// a turn of the wheel on the frequency, as zbitxv2 tuned: the dial by
// STEP, onto a multiple of it, or while RIT is on, RIT_DELTA by STEP
static void tune(int steps) {
	struct known *k = find_known("RIT_DELTA");
	int step = step_hz();

	if (steps == 0 || k == NULL)
		return;
	if (is_on("RIT"))
		snprintf(k->value, CONTROL_VALUE_MAX, "%d",
			clamp(atoi(k->value) + steps * step, -RIT_MAX_HZ, RIT_MAX_HZ));
	else
		dial_hz = clamp((dial_hz / step + steps) * step, LOWEST_HZ, HIGHEST_HZ);
	send_frequency();
	show_values();
}

// a VFO chosen: the dial of the one left is kept, and the other's taken
// up; the radio is told both dials, to keep
static void choose_vfo(int vfo) {
	char text[CONTROL_VALUE_MAX];
	int i;

	if (vfo == vfo_in_use)
		return;
	vfo_dial_hz[vfo_in_use] = dial_hz;
	if (vfo_dial_hz[vfo] == 0)
		vfo_dial_hz[vfo] = dial_hz;
	vfo_in_use = vfo;
	dial_hz = vfo_dial_hz[vfo];
	for (i = 0; i < VFOS; i++) {
		snprintf(text, sizeof(text), "%d", vfo_dial_hz[i]);
		send_line(vfo_dial_names[i], text);
	}
}

// one of the window's own controls has changed
static void apply_own(struct known *k) {
	struct known *vfo = find_known("VFO");

	if (!strcmp(k->name, "SPLIT") && is_on("SPLIT") && vfo != NULL)
		snprintf(vfo->value, CONTROL_VALUE_MAX, "A");
	if (vfo != NULL && !strcmp(vfo->value, "B"))
		choose_vfo(VFO_B);
	else
		choose_vfo(VFO_A);
	send_frequency();
}

// the window's own controls, which it keeps and never sends
static void add_own_control(const char *name, const char *choices, const char *value) {
	struct known *k = find_known(name);

	if (k == NULL)
		return;
	k->is_own = 1;
	snprintf(k->group, CONTROL_NAME_MAX, "WINDOW");
	snprintf(k->value, CONTROL_VALUE_MAX, "%s", value);
	if (choices == NULL)
		return;
	k->is_choice = 1;
	snprintf(k->choices, CHOICES_MAX, "%s", choices);
}

static void add_own_controls(void) {
	add_own_control("STEP", "10H/100H/500H/1K/10K", "1K");
	add_own_control("RIT", "OFF/ON", "OFF");
	add_own_control("RIT_DELTA", NULL, "0");
	add_own_control("SPLIT", "OFF/ON", "OFF");
	add_own_control("VFO", "A/B", "A");
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

// the next of a control's choices, and round from the last to the first
static void next_choice_round(struct known *k) {
	char choice[CONTROL_VALUE_MAX];
	int now = 0;
	int i;

	for (i = 0; choice_at(k->choices, i, choice, sizeof(choice)); i++) {
		if (!strcmp(choice, k->value))
			now = i;
	}
	if (!choice_at(k->choices, now + 1, choice, sizeof(choice))
			&& !choice_at(k->choices, 0, choice, sizeof(choice)))
		return;
	snprintf(k->value, CONTROL_VALUE_MAX, "%s", choice);
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
	if (k->is_own)
		apply_own(k);
	else
		send_line(k->name, k->value);
	show_values();
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
	// an action has things it can do and no value: they are kept for the
	// buttons that do them, and it is not a row
	if (!strcmp(kind, "action") && sscanf(rest, "%*s %*s %*s %127s", choices) == 1)
		snprintf(k->choices, CHOICES_MAX, "%s", choices);
	k->is_text = !strcmp(kind, "text");
	k->is_listed = !strcmp(kind, "number") || !strcmp(kind, "choice");
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

// the radio tells where the receiver listens; the dial is that, less
// RIT_DELTA while RIT is on
static void mark_message(GtkWidget *row);

static gboolean on_mark_kept_long_enough(gpointer unused) {
	(void)unused;
	mark_timer = 0;
	mark_message(NULL);
	return G_SOURCE_REMOVE;
}

// the mark is put on a message, or with NULL taken off; with none marked
// the list goes back to the newest
static void mark_message(GtkWidget *row) {
	GtkAdjustment *scroll = gtk_scrolled_window_get_vadjustment(
		GTK_SCROLLED_WINDOW(messages_scroller));

	if (marked_message != NULL)
		set_class(marked_message, "marked", 0);
	if (mark_timer != 0)
		g_source_remove(mark_timer);
	mark_timer = 0;
	marked_message = row;
	if (row == NULL) {
		gtk_adjustment_set_value(scroll, gtk_adjustment_get_upper(scroll));
		show_status("");
		return;
	}
	set_class(row, "marked", 1);
	show_status("tap it again, or press the knob, to answer");
	mark_timer = g_timeout_add_seconds(MARK_KEPT_FOR_S, on_mark_kept_long_enough, NULL);
}

// the marked message is answered: its line goes back to the radio, and
// the mark comes off
static void answer_marked(void) {
	const char *line;
	char text[LINE_MAX_LENGTH];

	if (marked_message == NULL)
		return;
	line = g_object_get_data(G_OBJECT(marked_message), "line");
	if (line != NULL && writer != NULL) {
		snprintf(text, sizeof(text), "answer %s\n", line);
		g_output_stream_write_all(writer, text, strlen(text), NULL, NULL, NULL);
	}
	mark_message(NULL);
}

// a tap on a message marks it; on the marked one, answers it
static void on_message_tap(GtkGestureMultiPress *gesture, gint presses, gdouble x,
		gdouble y, gpointer unused) {
	GtkWidget *row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

	(void)presses;
	(void)x;
	(void)y;
	(void)unused;
	if (row != marked_message)
		mark_message(row);
	else
		answer_marked();
}

static void on_message_destroyed(GtkWidget *row, gpointer unused) {
	(void)unused;
	if (row == marked_message)
		mark_message(NULL);
}

// the list is kept at its foot, where the newest message is
static void on_messages_sized(GtkWidget *widget, GdkRectangle *size, gpointer unused) {
	GtkAdjustment *scroll = gtk_scrolled_window_get_vadjustment(
		GTK_SCROLLED_WINDOW(messages_scroller));

	(void)widget;
	(void)size;
	(void)unused;
	if (marked_message == NULL)
		gtk_adjustment_set_value(scroll, gtk_adjustment_get_upper(scroll));
}

// on connecting the radio sends what it has kept, so the list starts empty
static void drop_all_messages(void) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(messages));
	GList *child;

	mark_message(NULL);
	for (child = children; child != NULL; child = child->next)
		gtk_widget_destroy(GTK_WIDGET(child->data));
	g_list_free(children);
	messages_shown = 0;
}

static void drop_oldest_message(void) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(messages));

	if (children != NULL) {
		gtk_widget_destroy(GTK_WIDGET(children->data));
		messages_shown--;
	}
	g_list_free(children);
}

// MESSAGE 043015|-12|1797|CQ VU2ESE MK97: a row is added to the list
static void take_message_line(const char *line) {
	char copy[LINE_MAX_LENGTH];
	char shown[LINE_MAX_LENGTH];
	char *field[MESSAGE_FIELDS];
	char *saved = NULL;
	GtkWidget *box;
	GtkWidget *label;
	int minute;
	int second;
	int i;

	snprintf(copy, sizeof(copy), "%s", line);
	field[0] = strtok_r(copy, "|", &saved);
	for (i = 1; i < MESSAGE_FIELDS - 1; i++)
		field[i] = strtok_r(NULL, "|", &saved);
	field[MESSAGE_FIELDS - 1] = strtok_r(NULL, "", &saved);
	if (field[MESSAGE_FIELDS - 1] == NULL)
		return;
	snprintf(shown, sizeof(shown), "%s %3s %4s %s", field[0], field[1], field[2],
		field[3]);
	label = gtk_label_new(shown);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	add_label_class(label, "message");
	if (!strcmp(field[1], "TX"))
		add_label_class(label, "sent");
	else if (!strncmp(field[3], "CQ ", strlen("CQ ")))
		add_label_class(label, "calling");
	box = gtk_event_box_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "message-row");
	// the slots are told apart by their ground: every other one is lighter
	if (strlen(field[0]) == TIME_DIGITS) {
		minute = atoi(field[0] + TIME_MINUTE_AT) / DIGITS_OF_TWO;
		second = atoi(field[0] + TIME_SECOND_AT);
		set_class(box, "other-slot",
			((minute * SECONDS_A_MINUTE + second) / SLOT_SECONDS) % 2);
	}
	gtk_container_add(GTK_CONTAINER(box), label);
	g_object_set_data_full(G_OBJECT(box), "line", g_strdup(line), g_free);
	tell_taps(box, G_CALLBACK(on_message_tap), NULL);
	g_signal_connect(box, "destroy", G_CALLBACK(on_message_destroyed), NULL);
	gtk_box_pack_start(GTK_BOX(messages), box, FALSE, FALSE, 0);
	gtk_widget_show_all(box);
	messages_shown++;
	if (messages_shown > MESSAGES_SHOWN_MAX)
		drop_oldest_message();
}

static void take_radio_frequency(int hz) {
	dial_hz = hz;
	if (is_on("RIT"))
		dial_hz -= atoi(value_of("RIT_DELTA"));
	// a VFO never used is put here once, and does not follow after
	if (vfo_dial_hz[vfo_in_waiting()] == 0)
		vfo_dial_hz[vfo_in_waiting()] = dial_hz;
}

// the dial the radio has kept for the VFO in waiting is taken up
static void take_kept_dial(const char *name, int hz) {
	if (hz > 0 && !strcmp(name, vfo_dial_names[vfo_in_waiting()]))
		vfo_dial_hz[vfo_in_waiting()] = hz;
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
	if (!strcmp(name, "MESSAGE")) {
		take_message_line(value);
		return;
	}
	if (!strcmp(name, "TIME")) {
		take_time_line(value);
		return;
	}
	if (!strcmp(name, "CW")) {
		set_class(status_label, "qso", 0);
		take_cw_line(value);
		return;
	}
	if (!strcmp(name, "SAY")) {
		set_class(status_label, "qso", 0);
		show_status(value);
		return;
	}
	if (!strcmp(name, "WARN")) {
		show_warning(value);
		return;
	}
	if (!strcmp(name, "QSO")) {
		set_class(status_label, "qso", value[0] != 0);
		show_status(value);
		return;
	}
	if (!strcmp(name, "CLOCK") && value_of("CLOCK")[0] != 0
			&& strcmp(value_of("CLOCK"), value))
		take_start_of_minute();
	if (!strcmp(name, "ERROR")) {
		show_status(value);
		return;
	}
	k = find_known(name);
	if (k == NULL || k->is_own)
		return;
	snprintf(k->value, CONTROL_VALUE_MAX, "%s", value);
	if (!strcmp(name, "FREQ"))
		take_radio_frequency(atoi(value));
	take_kept_dial(name, atoi(value));
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
	const char *asks = "schema\nspectrum on\nmessages on\ntime\n";

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
	drop_all_messages();
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

// a click on the frequency selects it; on it when it is selected, goes to
// the other VFO
static gboolean on_frequency_click(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
	struct known *vfo = find_known("VFO");

	(void)widget;
	(void)event;
	(void)unused;
	if (focus == ON_FREQUENCY && vfo != NULL) {
		next_choice_round(vfo);
		apply_own(vfo);
	}
	focus = ON_FREQUENCY;
	show_values();
	return TRUE;
}

static gboolean on_row_wheel(GtkWidget *widget, GdkEventScroll *event, gpointer row) {
	(void)widget;
	step_row(GPOINTER_TO_INT(row), wheel_steps(event));
	return TRUE;
}

// a tap on a row selects it; on a choice that is selected, moves it on
static void on_row_tap(GtkGestureMultiPress *gesture, gint presses, gdouble x, gdouble y,
		gpointer row) {
	int tapped = GPOINTER_TO_INT(row);
	struct known *k = find_known(row_control(&rows[tapped]));

	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	if (focus == tapped && k != NULL && k->is_choice) {
		next_choice_round(k);
		if (k->is_own)
			apply_own(k);
		else
			send_line(k->name, k->value);
	}
	focus = tapped;
	show_values();
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

// the control is put at the value, here and on the radio
static void set_and_send(const char *control, const char *value) {
	struct known *k = find_known(control);

	if (k != NULL && k->is_listed)
		snprintf(k->value, CONTROL_VALUE_MAX, "%s", value);
	send_line(control, value);
	show_values();
}

// a band's button: to the band, or on the band in use, to the next place
// in its stack
static gboolean on_band_click(GtkWidget *widget, GdkEventButton *event, gpointer band) {
	const char *name = band_names[GPOINTER_TO_INT(band)];
	struct known *stack = find_known("STACK");
	char place[CONTROL_VALUE_MAX];

	(void)widget;
	(void)event;
	if (strcmp(value_of("BAND"), name) || stack == NULL) {
		set_and_send("BAND", name);
		return TRUE;
	}
	snprintf(place, sizeof(place), "%d", stack->min);
	if (atoi(stack->value) < stack->max)
		snprintf(place, sizeof(place), "%d", atoi(stack->value) + 1);
	set_and_send("STACK", place);
	return TRUE;
}

static gboolean on_footer_click(GtkWidget *widget, GdkEventButton *event, gpointer slot) {
	char label[CONTROL_VALUE_MAX];
	char control[CONTROL_VALUE_MAX];
	char choice[CONTROL_VALUE_MAX];

	(void)widget;
	(void)event;
	if (footer_button_at(GPOINTER_TO_INT(slot), label, control, choice, sizeof(label)))
		set_and_send(control, choice);
	return TRUE;
}

static void fill_settings(void);

static void show_page(const char *name) {
	gtk_stack_set_visible_child_name(GTK_STACK(pages), name);
}

static int is_page_shown(const char *name) {
	const char *shown = gtk_stack_get_visible_child_name(GTK_STACK(pages));

	return shown != NULL && !strcmp(shown, name);
}

static void show_typed(void) {
	char with_mark[CONTROL_VALUE_MAX + 2];

	snprintf(with_mark, sizeof(with_mark), "%s_", typed);
	gtk_label_set_text(GTK_LABEL(typed_label), with_mark);
}

// the screen a text is typed on, for a control, beginning with what it has
static void open_text(struct known *k) {
	typed_for = k;
	snprintf(typed, sizeof(typed), "%s", k->value);
	gtk_label_set_text(GTK_LABEL(text_title), k->name);
	show_typed();
	show_page(PAGE_TEXT);
}

static void type_a_character(char c) {
	size_t length = strlen(typed);

	if (length + 1 >= sizeof(typed))
		return;
	typed[length] = c;
	typed[length + 1] = 0;
	show_typed();
}

static void rub_out(void) {
	size_t length = strlen(typed);

	if (length > 0)
		typed[length - 1] = 0;
	show_typed();
}

// what was typed goes to the radio, if it is something and is new, and the
// settings come back
static void save_typed(void) {
	if (typed_for != NULL && typed[0] != 0 && strcmp(typed, typed_for->value)) {
		snprintf(typed_for->value, CONTROL_VALUE_MAX, "%s", typed);
		send_line(typed_for->name, typed_for->value);
	}
	fill_settings();
	show_page(PAGE_SETTINGS);
}

static void on_key_pressed(GtkButton *button, gpointer character) {
	(void)button;
	type_a_character((char)GPOINTER_TO_INT(character));
}

static void on_rub_out_pressed(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	rub_out();
}

static void on_save_pressed(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	save_typed();
}

static void on_back_pressed(GtkButton *button, gpointer page) {
	(void)button;
	show_page(page);
}

// a tap on a setting: a text is typed, a choice goes to its next
static void on_setting_tap(GtkGestureMultiPress *gesture, gint presses, gdouble x,
		gdouble y, gpointer setting) {
	struct known *k = setting;

	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	if (k->is_text) {
		open_text(k);
		return;
	}
	next_choice_round(k);
	send_line(k->name, k->value);
	fill_settings();
}

// the settings' rows, made afresh each time they are shown: the radio's
// texts and choices in the group STATION
static void fill_settings(void) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(settings_list));
	GList *child;
	GtkWidget *row;
	GtkWidget *value;
	int i;

	for (child = children; child != NULL; child = child->next)
		gtk_widget_destroy(GTK_WIDGET(child->data));
	g_list_free(children);
	for (i = 0; i < known_count; i++) {
		if (strcmp(known[i].group, STATION_GROUP)
				|| (!known[i].is_text && !known[i].is_choice))
			continue;
		row = list_row_new(known[i].name, &value);
		gtk_label_set_text(GTK_LABEL(value), known[i].value);
		gtk_widget_set_size_request(row, -1, px_down(ROW_HEIGHT));
		tell_taps(row, G_CALLBACK(on_setting_tap), &known[i]);
		gtk_box_pack_start(GTK_BOX(settings_list), row, FALSE, FALSE, 0);
	}
	gtk_widget_show_all(settings_list);
}

// the column's buttons: SET, and in FT8 its two; the others do nothing yet
static gboolean on_column_click(GtkWidget *widget, GdkEventButton *event, gpointer row) {
	int clicked = GPOINTER_TO_INT(row);

	(void)widget;
	(void)event;
	if (clicked == SET_ROW) {
		fill_settings();
		show_page(PAGE_SETTINGS);
		return TRUE;
	}
	if (strcmp(mode_group(), MESSAGES_GROUP))
		return TRUE;
	if (clicked == FT8_CQ_ROW)
		send_line(FT8_DO_CONTROL, "CQ");
	if (clicked == FT8_HALT_ROW)
		send_line(FT8_DO_CONTROL, "HALT");
	return TRUE;
}

// a tap on the graph or the waterfall: the next span
static void on_spectrum_tap(GtkGestureMultiPress *gesture, gint presses, gdouble x,
		gdouble y, gpointer unused) {
	struct known *span = find_known("SPAN");

	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	(void)unused;
	if (span == NULL || !span->is_choice)
		return;
	next_choice_round(span);
	send_line(span->name, span->value);
	show_values();
}

// the list is scrolled, if it must be, to show the row with the focus
static void scroll_to_focus(void) {
	GtkAdjustment *scroll;
	GtkAllocation where;
	double top;
	double page;

	if (focus == ON_FREQUENCY || rows[focus].place != IN_LIST)
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

// a key of a keyboard plugged into the radio, while a text is being typed
static gboolean on_key_while_typing(const GdkEventKey *event) {
	guint32 character = gdk_keyval_to_unicode(gdk_keyval_to_upper(event->keyval));

	if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
		save_typed();
	else if (event->keyval == GDK_KEY_Escape)
		show_page(PAGE_SETTINGS);
	else if (event->keyval == GDK_KEY_BackSpace)
		rub_out();
	else if (character >= ' ' && character <= '~')
		type_a_character((char)character);
	return TRUE;
}

static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer unused) {
	(void)widget;
	(void)unused;
	if (is_page_shown(PAGE_TEXT))
		return on_key_while_typing(event);
	if (!is_page_shown(PAGE_MAIN))
		return FALSE;
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

static int row_of(const char *control) {
	int i;

	for (i = 0; i < row_count; i++) {
		if (!strcmp(rows[i].control, control))
			return i;
	}
	return NOT_FOUND;
}

// the knobs' thread: both knobs looked at, and the button, which counts as
// pressed once when it has been down for BUTTON_DOWN_LOOKS looks running
static void *look_at_knobs(void *unused) {
	int down_for = 0;

	(void)unused;
	while (1) {
		atomic_fetch_add(&tuning_notches, knob_read(&tuning_knob));
		atomic_fetch_add(&control_notches, knob_read(&control_knob));
		if (gpio_read(control_button_pin) == 0)
			down_for++;
		else
			down_for = 0;
		if (down_for == BUTTON_DOWN_LOOKS)
			atomic_fetch_add(&control_presses, 1);
		usleep(KNOBS_LOOKED_AT_EVERY_US);
	}
	return NULL;
}

// what the knobs have counted since the last time is acted on
static gboolean take_up_knobs(gpointer unused) {
	int tuned = atomic_exchange(&tuning_notches, 0);
	int turned = atomic_exchange(&control_notches, 0);
	int pressed = atomic_exchange(&control_presses, 0);
	int row = focus;

	(void)unused;
	if (tuned != 0)
		tune(tuned);
	if (row == ON_FREQUENCY)
		row = row_of(VOLUME_CONTROL);
	if (turned != 0 && row != NOT_FOUND)
		step_row(row, turned);
	while (pressed > 0) {
		if (marked_message != NULL)
			answer_marked();
		else
			move_focus(1);
		pressed--;
	}
	return G_SOURCE_CONTINUE;
}

// the knobs of the radio the window is on, if it is on one that has them
static void open_knobs(void) {
	const struct board *board = board_named_in(HW_SETTINGS_FILE);
	const struct radio_knobs *knobs = radios_with_knobs;
	pthread_t thread;

	if (board == NULL)
		return;
	while (knobs->radio != NULL && strcmp(knobs->radio, board->radio))
		knobs++;
	if (knobs->radio == NULL)
		return;
	if (gpio_open() < 0)
		return;
	if (knob_open(&tuning_knob, knobs->tuning.a_pin, knobs->tuning.b_pin) < 0)
		return;
	if (knob_open(&control_knob, knobs->control.a_pin, knobs->control.b_pin) < 0)
		return;
	control_button_pin = knobs->control.button_pin;
	if (gpio_make_input(control_button_pin, GPIO_PULL_UP) < 0)
		return;
	if (pthread_create(&thread, NULL, look_at_knobs, NULL) != 0)
		return;
	g_timeout_add(KNOBS_TAKEN_UP_EVERY_MS, take_up_knobs, NULL);
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
	if (!strcmp(mode, "FT8")) {
		*low_hz = FT8_LOW_HZ;
		*high_hz = FT8_HIGH_HZ;
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

// how many Hz across the graph and the waterfall show: the span chosen,
// "10K" or "2.5K", or all the radio sends if that is less
static int shown_hz(void) {
	const char *span = value_of("SPAN");
	int chosen = (int)(atof(span) * HZ_PER_K);

	if (chosen <= 0 || chosen > span_hz)
		return span_hz;
	return chosen;
}

// the points of the radio's line that are in the span shown: how many, and
// the first of them
static int shown_points(int *first) {
	int count = (int)((double)point_count * shown_hz() / span_hz);

	if (count < 2)
		count = 2;
	if (count > point_count)
		count = point_count;
	*first = (point_count - count) / 2;
	return count;
}

static double x_of_hz(int hz, int width) {
	return width / 2.0 + (double)hz * width / shown_hz();
}

static void draw_graph(cairo_t *cr, int width, int height) {
	int low_hz;
	int high_hz;
	int tuned_hz;
	double x;
	double y;
	int first;
	int count;
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
	count = shown_points(&first);
	for (i = 0; i < count; i++) {
		x = (double)i * width / (count - 1);
		y = height - (double)points[first + i] * height / FULL_SCALE_DB;
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

// in CW the pitch, where a station tuned in is heard, is carried down the
// waterfall from the graph, dotted, so that the station right under it is
// not hidden
static void draw_waterfall(cairo_t *cr, int width, int height) {
	const char *mode = value_of("MODE");
	const double dashes[] = { PITCH_DASH, PITCH_DASH };
	int low_hz;
	int high_hz;
	int tuned_hz;
	int first;
	int count;
	double x;

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	if (point_count < 2 || span_hz == 0)
		return;
	count = shown_points(&first);
	cairo_save(cr);
	cairo_scale(cr, (double)width / count, (double)height / FALL_ROWS);
	cairo_set_source_surface(cr, fall, -first, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
	cairo_rectangle(cr, 0, 0, count, FALL_ROWS);
	cairo_fill(cr);
	cairo_restore(cr);
	if (span_hz == 0 || (strcmp(mode, "CW") && strcmp(mode, "CWR")))
		return;
	passband(&low_hz, &high_hz, &tuned_hz);
	x = x_of_hz(tuned_hz, width);
	cairo_save(cr);
	cairo_set_source_rgb(cr, 1.0, 0.3, 0.3);
	cairo_set_line_width(cr, 1.0);
	cairo_set_dash(cr, dashes, 2, 0);
	cairo_move_to(cr, x, 0);
	cairo_line_to(cr, x, height);
	cairo_stroke(cr);
	cairo_restore(cr);
}

static gboolean on_draw_spectrum(GtkWidget *widget, cairo_t *cr, gpointer unused) {
	int width = gtk_widget_get_allocated_width(widget);
	int height = gtk_widget_get_allocated_height(widget);
	int graph_height = px_down(GRAPH_HEIGHT);

	(void)unused;
	draw_graph(cr, width, graph_height);
	cairo_set_source_rgb(cr, 0.56, 0.63, 0.69);
	cairo_set_font_size(cr, px(CORNER_SIZE));
	cairo_move_to(cr, px(SPAN_LEFT), px(SPAN_BASELINE));
	cairo_show_text(cr, value_of("SPAN"));
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

static void place(const struct placed *it) {
	gtk_widget_set_size_request(it->widget,
		px_across(it->x + it->width) - px_across(it->x),
		px_down(it->y + it->height) - px_down(it->y));
	gtk_layout_move(GTK_LAYOUT(screen), it->widget, px_across(it->x), px_down(it->y));
}

// a widget that has gone is forgotten
static void on_placed_destroyed(GtkWidget *widget, gpointer unused) {
	int i;

	(void)unused;
	for (i = 0; i < PLACED_MAX; i++) {
		if (placed[i].widget == widget)
			placed[i].widget = NULL;
	}
}

// a widget is put on the screen at a place in the panel's plan
static void put(GtkWidget *widget, int x, int y, int width, int height) {
	struct placed *it = placed;

	while (it < placed + PLACED_MAX && it->widget != NULL)
		it++;
	gtk_layout_put(GTK_LAYOUT(screen), widget, 0, 0);
	if (it == placed + PLACED_MAX)
		return;
	it->widget = widget;
	it->x = x;
	it->y = y;
	it->width = width;
	it->height = height;
	g_signal_connect(widget, "destroy", G_CALLBACK(on_placed_destroyed), NULL);
	place(it);
}

static void add_label_class(GtkWidget *label, const char *name) {
	gtk_style_context_add_class(gtk_widget_get_style_context(label), name);
}

// a tile: a box with something in it, that takes clicks and the wheel
static GtkWidget *tile_new(GtkWidget *inside) {
	GtkWidget *box = gtk_event_box_new();

	gtk_style_context_add_class(gtk_widget_get_style_context(box), "tile");
	gtk_container_add(GTK_CONTAINER(box), inside);
	gtk_widget_add_events(box, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK);
	return box;
}

// a button: a tile with a label alone
static GtkWidget *button_new(const char *text, GtkWidget **label) {
	*label = gtk_label_new(text);
	add_label_class(*label, "tile-value");
	return tile_new(*label);
}

// a control's tile, as on the panel: its name small in the top left corner
// and its value under it
static GtkWidget *control_tile_new(const char *name, GtkWidget **value) {
	GtkWidget *inside = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *title = gtk_label_new(name);

	*value = gtk_label_new("");
	gtk_widget_set_margin_top(title, px(TITLE_ROOM_ABOVE));
	add_label_class(title, "tile-title");
	add_label_class(*value, "tile-value");
	gtk_box_pack_start(GTK_BOX(inside), title, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(inside), *value, TRUE, TRUE, 0);
	return tile_new(inside);
}

// a row of the list: the control's name at the left and its value at the
// right
static GtkWidget *list_row_new(const char *name, GtkWidget **value) {
	GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *title = gtk_label_new(name);

	*value = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_label_set_xalign(GTK_LABEL(*value), 1);
	add_label_class(title, "tile-title");
	add_label_class(*value, "tile-value");
	gtk_widget_set_margin_start(line, px(ROW_MARGIN_SIDE));
	gtk_widget_set_margin_end(line, px(ROW_MARGIN_SIDE));
	gtk_box_pack_start(GTK_BOX(line), title, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(line), *value, FALSE, FALSE, 0);
	return tile_new(line);
}

static const struct top_place *place_of(const struct top_place *places, const char *control) {
	const struct top_place *p;

	for (p = places; p->control != NULL; p++) {
		if (!strcmp(p->control, control))
			return p;
	}
	return NULL;
}

static void add_row(const char *control, enum place place) {
	const struct top_place *where = place_of(top_places, control);
	GtkWidget *box;
	GtkWidget *value;
	int index = row_count;

	if (row_count == KNOWN_MAX || has_row(control))
		return;
	if (place == IN_FOOTER)
		where = place_of(footer_places, control);
	if (place == IN_LIST) {
		box = list_row_new(label_of(control), &value);
		gtk_widget_set_size_request(box, -1, px_down(ROW_HEIGHT));
		gtk_box_pack_start(GTK_BOX(list), box, FALSE, FALSE, 0);
	}
	else {
		box = control_tile_new(label_of(control), &value);
		put(box, where->x, where->y, TILE_WIDTH, ROW_HEIGHT);
	}
	gtk_widget_set_no_show_all(box, TRUE);
	gtk_widget_show_all(gtk_bin_get_child(GTK_BIN(box)));
	g_signal_connect(box, "scroll-event", G_CALLBACK(on_row_wheel), GINT_TO_POINTER(index));
	tell_taps(box, G_CALLBACK(on_row_tap), GINT_TO_POINTER(index));
	snprintf(rows[index].control, CONTROL_NAME_MAX, "%s", control);
	rows[index].place = place;
	rows[index].box = box;
	rows[index].value_label = value;
	row_count++;
}

// what is never a row of the list: what has a place or a button of its own,
// and the two bandwidths, which BW stands for
static int is_in_list(const struct known *k) {
	static const char *const elsewhere[] = {
		"FREQ", "BAND", "BW_VOICE", "BW_CW", NULL,
	};
	int i;

	if (!k->is_listed && !k->is_own)
		return 0;
	for (i = 0; elsewhere[i] != NULL; i++) {
		if (!strcmp(k->name, elsewhere[i]))
			return 0;
	}
	return 1;
}

// the rows, made again each time the radio sends its schema: those with a
// place of their own, at their places; and in the list, the rest of the
// radio's own, then BW and the modems' own, of which only the mode in
// use's are shown
static void make_list(void) {
	const struct top_place *p;
	int i;

	for (i = 0; i < row_count; i++)
		gtk_widget_destroy(rows[i].box);
	row_count = 0;
	focus = ON_FREQUENCY;
	for (p = top_places; p->control != NULL; p++)
		add_row(p->control, ON_TOP);
	for (p = footer_places; p->control != NULL; p++)
		add_row(p->control, IN_FOOTER);
	for (i = 0; i < known_count; i++) {
		if (!strcmp(known[i].group, "RADIO") && is_in_list(&known[i]))
			add_row(known[i].name, IN_LIST);
	}
	add_row("BW", IN_LIST);
	for (i = 0; i < known_count; i++) {
		if (known[i].group[0] != 0 && strcmp(known[i].group, "RADIO")
				&& strcmp(known[i].group, "WINDOW") && is_in_list(&known[i]))
			add_row(known[i].name, IN_LIST);
	}
	show_values();
}

// the panel's colours; the sizes of the letters go with the scale
static void load_style(void) {
	char rules[CSS_MAX];

	if (css == NULL) {
		css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}
	snprintf(rules, sizeof(rules),
		"window { background-color: #000000; color: #FFFFFF; }"
		".tile { background-color: #101820; box-shadow: inset 0 0 0 1px #405060; }"
		".tile.in-use { background-color: #604800; }"
		".tile.on { background-color: #B02020; }"
		".tile.focused { box-shadow: inset 0 0 0 %dpx #FFC000; }"
		".tile-title { font-size: %dpx; color: #00FFFF; }"
		".clock { font-size: %dpx; color: #FFFFFF; }"
		".dial-waiting { font-family: monospace; font-size: %dpx; font-weight: bold;"
		" color: #00FFFF; }"
		".status.qso { color: #FFFFFF; background-color: #B02020; }"
		".status.warning { font-family: sans-serif; font-weight: bold; color: #FFE000;"
		" background-color: #000000; }"
		".status.meter { color: #FFC000; background-color: #000000; }"
		".tile-value { font-size: %dpx; font-weight: bold; color: #FFFFFF; }"
		".dim .tile-value { color: #506070; }"
		".frequency { font-family: monospace; font-size: %dpx; font-weight: bold;"
		" color: #FFFFFF; }"
		".status { font-size: %dpx; color: #90A0B0; }"
		".message { font-family: monospace; font-size: %dpx; color: #FFFFFF; }"
		".message.sent { color: #FF6060; }"
		".message.calling { color: #60E060; }"
		".message-row { background-color: #101820; }"
		".message-row.other-slot { background-color: #1C2834; }"
		".message-row.marked { background-color: #604800; }"
		".key { font-size: %dpx; font-weight: bold; color: #FFFFFF;"
		" background-image: none; background-color: #101820;"
		" border: 1px solid #405060; border-radius: 0; box-shadow: none; }"
		".key:active { background-color: #604800; }"
		".typed { font-family: monospace; font-size: %dpx; font-weight: bold;"
		" color: #FFFFFF; background-color: #101820;"
		" box-shadow: inset 0 0 0 %dpx #FFC000; }",
		px(1), px(TITLE_SIZE), px(CLOCK_SIZE), px(WAITING_SIZE), px(VALUE_SIZE),
		px(FREQUENCY_SIZE),
		px(STATUS_SIZE),
		px(MESSAGE_SIZE), px(VALUE_SIZE), px(TYPED_SIZE), px(1));
	gtk_css_provider_load_from_data(css, rules, -1, NULL);
}

// the plan is stretched to the size the window has now: everything is put
// again and the letters sized again. It is done when the window is next
// idle and not while GTK is giving out the new size, when a widget moved
// or sized is not seen to until the size changes once more: the buttons
// then stayed as they were when the window was maximised, and took the
// maximised size when it was restored.
static gboolean lay_out_again(gpointer unused) {
	int i;

	(void)unused;
	is_lay_out_due = 0;
	scale_across = (double)laid_out_width / PANEL_WIDTH;
	scale_down = (double)laid_out_height / PANEL_HEIGHT;
	scale = scale_down;
	if (scale_across < scale)
		scale = scale_across;
	load_style();
	for (i = 0; i < PLACED_MAX; i++) {
		if (placed[i].widget != NULL)
			place(&placed[i]);
	}
	for (i = 0; i < row_count; i++) {
		if (rows[i].place == IN_LIST)
			gtk_widget_set_size_request(rows[i].box, -1, px_down(ROW_HEIGHT));
	}
	return G_SOURCE_REMOVE;
}

// the window has a new size
static void on_screen_sized(GtkWidget *widget, GdkRectangle *size, gpointer unused) {
	(void)widget;
	(void)unused;
	if (size->width == laid_out_width && size->height == laid_out_height)
		return;
	laid_out_width = size->width;
	laid_out_height = size->height;
	if (!is_lay_out_due)
		g_idle_add(lay_out_again, NULL);
	is_lay_out_due = 1;
}

// the frequency's box: the VFO and the time over the figures
static void create_frequency(void) {
	GtkWidget *layers = gtk_overlay_new();
	GtkWidget *lines = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	// the two lines are one column in the middle of the box, as wide as
	// the large line, and both begin at its left edge, so that what the
	// small line is, "B:", stands right over what the large one is, "A:"
	vfo_label = gtk_label_new("");
	frequency_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(vfo_label), 0);
	gtk_label_set_xalign(GTK_LABEL(frequency_label), 0);
	add_label_class(vfo_label, "dial-waiting");
	add_label_class(frequency_label, "frequency");
	gtk_box_pack_start(GTK_BOX(lines), vfo_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(lines), frequency_label, FALSE, FALSE, 0);
	gtk_widget_set_halign(lines, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(lines, GTK_ALIGN_CENTER);

	// the time is laid over the box's top right corner
	clock_label = gtk_label_new("");
	add_label_class(clock_label, "clock");
	gtk_widget_set_halign(clock_label, GTK_ALIGN_END);
	gtk_widget_set_valign(clock_label, GTK_ALIGN_START);
	gtk_widget_set_margin_top(clock_label, px(TITLE_ROOM_ABOVE));
	gtk_widget_set_margin_end(clock_label, px(SPAN_LEFT));

	gtk_container_add(GTK_CONTAINER(layers), lines);
	gtk_overlay_add_overlay(GTK_OVERLAY(layers), clock_label);
	gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(layers), clock_label, TRUE);
	frequency_box = tile_new(layers);
	g_signal_connect(frequency_box, "scroll-event", G_CALLBACK(on_frequency_wheel), NULL);
	g_signal_connect(frequency_box, "button-press-event",
		G_CALLBACK(on_frequency_click), NULL);
	put(frequency_box, FREQ_LEFT, 0, FREQ_WIDTH, ROW_HEIGHT);
}

// the buttons that are always there: the bands, the column and the bottom
// row's
static void create_buttons(void) {
	GtkWidget *label;
	int i;

	for (i = 0; i < BANDS; i++) {
		band_box[i] = button_new(band_names[i], &label);
		g_signal_connect(band_box[i], "button-press-event", G_CALLBACK(on_band_click),
			GINT_TO_POINTER(i));
		put(band_box[i], i * TILE_WIDTH, BANDS_TOP, TILE_WIDTH, ROW_HEIGHT);
	}
	for (i = 0; i < COLUMN_BUTTONS; i++) {
		column_box[i] = button_new(column_names[i], &column_label[i]);
		gtk_widget_set_no_show_all(column_box[i], TRUE);
		gtk_widget_show(column_label[i]);
		g_signal_connect(column_box[i], "button-press-event",
			G_CALLBACK(on_column_click), GINT_TO_POINTER(i));
		put(column_box[i], 0, MIDDLE_TOP + i * ROW_HEIGHT, TILE_WIDTH, ROW_HEIGHT);
	}
	for (i = 0; i < FOOTER_TILES; i++) {
		footer_box[i] = button_new("", &footer_label[i]);
		gtk_widget_set_no_show_all(footer_box[i], TRUE);
		gtk_widget_show(footer_label[i]);
		g_signal_connect(footer_box[i], "button-press-event",
			G_CALLBACK(on_footer_click), GINT_TO_POINTER(i));
		put(footer_box[i], i * TILE_WIDTH, FOOTER_TOP, TILE_WIDTH, ROW_HEIGHT);
	}
}

// the status line, and under it the graph and the waterfall
static void create_middle(void) {
	status_label = gtk_label_new("");
	meter_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(status_label), 0);
	gtk_label_set_xalign(GTK_LABEL(meter_label), 1);
	gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_END);
	add_label_class(status_label, "status");
	add_label_class(meter_label, "status");
	// the status line is the whole width; the meter is laid over its right
	// end, on a ground of its own, only while there is power to show
	put(status_label, GRAPH_LEFT, MIDDLE_TOP, GRAPH_WIDTH, STATUS_HEIGHT);
	add_label_class(meter_label, "meter");
	gtk_widget_set_no_show_all(meter_label, TRUE);
	put(meter_label, GRAPH_LEFT + GRAPH_WIDTH - METER_WIDTH, MIDDLE_TOP, METER_WIDTH,
		STATUS_HEIGHT);

	spectrum_area = gtk_drawing_area_new();
	gtk_widget_add_events(spectrum_area, GDK_SCROLL_MASK);
	g_signal_connect(spectrum_area, "draw", G_CALLBACK(on_draw_spectrum), NULL);
	g_signal_connect(spectrum_area, "scroll-event", G_CALLBACK(on_frequency_wheel), NULL);
	tell_taps(spectrum_area, G_CALLBACK(on_spectrum_tap), NULL);
	put(spectrum_area, GRAPH_LEFT, MIDDLE_TOP + STATUS_HEIGHT, GRAPH_WIDTH,
		GRAPH_HEIGHT + FALL_HEIGHT);

	// FT8's messages, in the same place and on down to the foot
	messages = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	messages_scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(messages_scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(messages_scroller),
		FALSE);
	gtk_container_add(GTK_CONTAINER(messages_scroller), messages);
	gtk_widget_set_no_show_all(messages_scroller, TRUE);
	gtk_widget_show_all(gtk_bin_get_child(GTK_BIN(messages_scroller)));
	g_signal_connect(messages, "size-allocate", G_CALLBACK(on_messages_sized), NULL);
	put(messages_scroller, GRAPH_LEFT, MIDDLE_TOP + STATUS_HEIGHT, GRAPH_WIDTH,
		PANEL_HEIGHT - MIDDLE_TOP - STATUS_HEIGHT);
}

static void create_list(void) {
	list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	list_scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	// the scroll bar has a strip of its own beside the rows, and is not
	// drawn over the values at their right ends
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(list_scroller), FALSE);
	gtk_container_add(GTK_CONTAINER(list_scroller), list);
	put(list_scroller, LIST_LEFT, MIDDLE_TOP, LIST_WIDTH, LIST_HEIGHT);
}

static GtkWidget *key_new(const char *text, GCallback pressed, gpointer data) {
	GtkWidget *key = gtk_button_new_with_label(text);

	gtk_style_context_add_class(gtk_widget_get_style_context(key), "key");
	gtk_widget_set_can_focus(key, FALSE);
	gtk_widget_set_hexpand(key, TRUE);
	gtk_widget_set_vexpand(key, TRUE);
	g_signal_connect(key, "clicked", pressed, data);
	return key;
}

// the top of a screen that is not the main one: the arrow that goes back,
// and what the screen is
static GtkWidget *create_top_bar(const char *back_to, GtkWidget **title) {
	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *back = key_new("\u2190", G_CALLBACK(on_back_pressed), (gpointer)back_to);

	*title = gtk_label_new("");
	add_label_class(*title, "tile-value");
	gtk_widget_set_hexpand(back, FALSE);
	gtk_widget_set_vexpand(back, FALSE);
	gtk_widget_set_size_request(back, px_across(TILE_WIDTH), px_down(ROW_HEIGHT));
	gtk_box_pack_start(GTK_BOX(bar), back, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), *title, TRUE, TRUE, 0);
	return bar;
}

static GtkWidget *create_settings_page(void) {
	GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget *title;

	gtk_box_pack_start(GTK_BOX(page), create_top_bar(PAGE_MAIN, &title), FALSE, FALSE, 0);
	gtk_label_set_text(GTK_LABEL(title), "SETTINGS");
	settings_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
		GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroller), FALSE);
	gtk_container_add(GTK_CONTAINER(scroller), settings_list);
	gtk_box_pack_start(GTK_BOX(page), scroller, TRUE, TRUE, 0);
	return page;
}

// the screen a text is typed on: what is typed in the upper part, and in
// the lower part the keyboard, which has no more keys than are needed
static GtkWidget *create_text_page(void) {
	static const char *const key_rows[KEY_ROWS] = {
		"1234567890", "QWERTYUIOP", "ASDFGHJKL/", "ZXCVBNM.?-",
	};
	GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *keys = gtk_grid_new();
	char text[2] = "";
	int column = 0;
	int row;
	int i;

	gtk_box_pack_start(GTK_BOX(page), create_top_bar(PAGE_SETTINGS, &text_title), FALSE,
		FALSE, 0);
	typed_label = gtk_label_new("");
	add_label_class(typed_label, "typed");
	gtk_widget_set_size_request(typed_label, -1, px_down(ROW_HEIGHT));
	gtk_box_pack_start(GTK_BOX(page), typed_label, FALSE, FALSE, 0);

	gtk_grid_set_row_homogeneous(GTK_GRID(keys), TRUE);
	gtk_grid_set_column_homogeneous(GTK_GRID(keys), TRUE);
	for (row = 0; row < KEY_ROWS; row++) {
		for (i = 0; i < KEYS_A_ROW; i++) {
			text[0] = key_rows[row][i];
			gtk_grid_attach(GTK_GRID(keys), key_new(text, G_CALLBACK(on_key_pressed),
				GINT_TO_POINTER(text[0])), i, row, 1, 1);
		}
	}
	gtk_grid_attach(GTK_GRID(keys), key_new("CANCEL", G_CALLBACK(on_back_pressed),
		PAGE_SETTINGS), column, KEY_ROWS, CANCEL_KEYS, 1);
	column += CANCEL_KEYS;
	gtk_grid_attach(GTK_GRID(keys), key_new("SPACE", G_CALLBACK(on_key_pressed),
		GINT_TO_POINTER(' ')), column, KEY_ROWS, SPACE_KEYS, 1);
	column += SPACE_KEYS;
	gtk_grid_attach(GTK_GRID(keys), key_new("\u232B", G_CALLBACK(on_rub_out_pressed),
		NULL), column, KEY_ROWS, RUB_OUT_KEYS, 1);
	column += RUB_OUT_KEYS;
	gtk_grid_attach(GTK_GRID(keys), key_new("SAVE", G_CALLBACK(on_save_pressed), NULL),
		column, KEY_ROWS, SAVE_KEYS, 1);
	gtk_box_pack_end(GTK_BOX(page), keys, TRUE, TRUE, 0);
	return page;
}

static GtkWidget *create_window(void) {
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	load_style();
	gtk_window_set_title(GTK_WINDOW(window), "zbitx");
	gtk_window_set_default_size(GTK_WINDOW(window), WINDOW_WIDTH, WINDOW_HEIGHT);
	gtk_window_maximize(GTK_WINDOW(window));
	screen = gtk_layout_new(NULL, NULL);
	create_frequency();
	create_buttons();
	create_middle();
	create_list();

	pages = gtk_stack_new();
	gtk_stack_add_named(GTK_STACK(pages), screen, PAGE_MAIN);
	gtk_stack_add_named(GTK_STACK(pages), create_settings_page(), PAGE_SETTINGS);
	gtk_stack_add_named(GTK_STACK(pages), create_text_page(), PAGE_TEXT);
	gtk_container_add(GTK_CONTAINER(window), pages);

	g_signal_connect(screen, "size-allocate", G_CALLBACK(on_screen_sized), NULL);
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
	add_own_controls();
	window = create_window();
	gtk_widget_show_all(window);
	show_values();

	g_timeout_add(CLOCK_SHOWN_EVERY_MS, show_clock, NULL);
	open_knobs();
	connector = g_socket_client_new();
	try_to_connect();
	gtk_main();
	return 0;
}
