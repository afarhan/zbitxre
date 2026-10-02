// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_log.c — the logbook's newest entries, listed for the front panel.
//
// The panel's LOG screen shows the logbook. The panel has no room for a
// logbook, so it asks the radio, which keeps it, for the newest entries:
// LINK_LOG_LINES_MAX of them, a hundred, which is what the panel has
// memory to hold. zbitxv2 sent its panel fifty.
//
// The author's rule: a request for the logbook always carries what is
// searched for, the beginning of a callsign, which is empty when the screen
// is first opened and then means every station. So the request is a text,
// sent as the panel sends any text, in parts, with LINK_LOG_LIST where a
// control's number would be. "log=VU2" on the terminal asks the same, and
// "log=" for all; the list is printed there too.
//
// An entry is never changed, but a bad one can be deleted: the panel asks,
// after the operator has said yes, with the entry's number and the same
// search, and is sent the list of that search afresh.
//
// An entry goes to the panel as a line of text, the fields with a | between
// them, in the order the panel shows them, which is the order zbitxv2's
// panel showed them in:
//
//     972|2026-04-08|04:45|7130|LSB|VU2XZ|59|59
//
// the entry's number in the logbook, the date and the time in UTC, the
// frequency in kHz, the mode, the other station's callsign, what was sent
// and what was received. The last two are the report, and the exchange
// after it if there was one: "-14 MK97".
//
// The lines go in parts, each when the panel has taken the one before;
// front_panel/link.c says what is in a part.
//
// For the thread that owns the controls.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link.h"
#include "logbook.h"
#include "panel.h"
#include "panel_log.h"

#define NOTHING_TO_SEND (-1)
#define TIME_DIGITS 4
#define HOUR_DIGITS 2
// room for every field at its longest, before the line is cut to what the
// link carries
#define WHOLE_LINE_MAX (12 * LOGBOOK_FIELD_MAX)

static struct logbook_row rows[LINK_LOG_LINES_MAX];
static int row_count;
static uint8_t part[LINK_COUNTED_MAX];
static int next_to_send = NOTHING_TO_SEND;
static int is_list_wanted;
static char searched_for[LOGBOOK_FIELD_MAX];

// a report and its exchange, as they are shown together
static void join(char *joined, int room, const char *report, const char *exchange) {
	if (exchange[0] != 0)
		snprintf(joined, room, "%s %s", report, exchange);
	else
		snprintf(joined, room, "%s", report);
}

// 0445 is shown as 04:45; anything else as it is
static void show_time(char *shown, int room, const char *qso_time) {
	if (strlen(qso_time) == TIME_DIGITS)
		snprintf(shown, room, "%.2s:%s", qso_time, qso_time + HOUR_DIGITS);
	else
		snprintf(shown, room, "%s", qso_time);
}

static void make_a_line(char *line, const struct logbook_row *row) {
	char sent[2 * LOGBOOK_FIELD_MAX];
	char received[2 * LOGBOOK_FIELD_MAX];
	char at[LOGBOOK_FIELD_MAX];
	char whole[WHOLE_LINE_MAX];

	join(sent, sizeof(sent), row->rst_sent, row->exch_sent);
	join(received, sizeof(received), row->rst_recv, row->exch_recv);
	show_time(at, sizeof(at), row->qso_time);
	snprintf(whole, sizeof(whole), "%d|%s|%s|%s|%s|%s|%s|%s", row->id,
		row->qso_date, at, row->freq, row->mode, row->callsign_recv, sent, received);
	snprintf(line, LINK_LOG_LINE_MAX + 1, "%.*s", LINK_LOG_LINE_MAX, whole);
}

static void read_the_logbook(void) {
	char line[LINK_LOG_LINE_MAX + 1];
	int i;

	row_count = logbook_find(searched_for, rows, LINK_LOG_LINES_MAX);
	if (row_count < 0) {
		printf("log: the logbook cannot be read\n");
		row_count = 0;
	}
	printf("log: %d entries with \"%s\"\n", row_count, searched_for);
	for (i = 0; i < row_count; i++) {
		make_a_line(line, &rows[i]);
		printf("log: %s\n", line);
	}
	fflush(stdout);
	next_to_send = 0;
}

// the next part of the list goes when the panel has taken the one before
static void send_a_part(void) {
	char line[LINK_LOG_LINE_MAX + 1];
	int used = LINK_LOG_PART_HEADER;
	int length;

	if (next_to_send == NOTHING_TO_SEND || panel_is_waiting(LINK_LOG_LIST))
		return;
	part[0] = (uint8_t)next_to_send;
	while (next_to_send < row_count) {
		make_a_line(line, &rows[next_to_send]);
		length = (int)strlen(line) + 1;
		if (used + length > (int)sizeof(part))
			break;
		memcpy(part + used, line, length);
		used += length;
		next_to_send++;
	}
	part[1] = next_to_send == row_count;
	if (part[1])
		next_to_send = NOTHING_TO_SEND;
	panel_set_kept_bytes(LINK_LOG_LIST, part, used);
}

void panel_log_poll(void) {
	if (is_list_wanted) {
		is_list_wanted = 0;
		read_the_logbook();
	}
	send_a_part();
}

// the newest entries with stations whose callsign begins as given are to
// be listed, on the terminal and to the panel
void panel_log_list(const char *callsign) {
	snprintf(searched_for, sizeof(searched_for), "%.*s", LOGBOOK_FIELD_MAX - 1, callsign);
	is_list_wanted = 1;
}

// the panel asks for an entry to be deleted: its number, a tab, and what
// the list is being searched for. The entry is deleted and the list of
// that same search is made again, so that the panel shows the logbook as it
// now is without the search being lost
static void delete_and_list(char *asked) {
	char *callsign = strchr(asked, LINK_LOG_BETWEEN);
	int id = atoi(asked);

	if (callsign == NULL)
		return;
	callsign++;
	if (logbook_delete(id) < 0) {
		printf("log: entry %d could not be deleted\n", id);
		panel_say("log: the entry could not be deleted");
	}
	else
		printf("log: entry %d deleted\n", id);
	panel_log_list(callsign);
}

// the panel's requests, each a text in parts: when one is whole it is done
void panel_log_took_bytes(int control, const uint8_t *bytes, int count) {
	static char gathered[LINK_TEXT_MAX + 1];
	int number = 0;

	if (control != LINK_TEXT_PART)
		return;
	if (link_get_text_part(bytes, count, &number, gathered, sizeof(gathered)) != 1)
		return;
	if (number == LINK_LOG_LIST)
		panel_log_list(gathered);
	if (number == LINK_LOG_DELETE)
		delete_and_list(gathered);
}

// opens the logbook; a radio with no logbook file lists nothing
void panel_log_open(const char *path) {
	if (logbook_open(path) < 0)
		printf("log: there is no logbook at %s\n", path);
}
