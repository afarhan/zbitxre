// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// logger.c — an entry written by hand for the logbook, as zbitxv2's
// logger did: the other station's callsign, the reports and the
// exchanges, typed in as the contact goes, and saved.
//
// FT8 logs its contacts by itself, ft8_qso.c. A contact in CW or on the
// voice is logged by the operator, with these.
//
// THE CONTROLS
//
// They are in a group of their own, LOGGER, so that the panel's list and
// the desktop's do not show them among the radio's, and they are not kept
// from one start to the next. The names are zbitxv2's, which its macros
// use, and macros.c puts them in as zbitxv2's did: {CALL} is CALL, {SENTRST}
// is SENT and {EXCH} is NR.
//
//   CALL    the other station's callsign.
//   SENT    the report we sent, "599" or "59".
//   RECV    the report we received.
//   EXCH    the exchange we received, a serial number or a grid or a name.
//   NR      the exchange we sent; zbitxv2's name for it, from the contest
//           serial number it most often is.
//   LOGGER  an action: SAVE or WIPE.
//
// SAVE writes the entry to the logbook. The author's rule is that an entry
// needs at least CALL, SENT and RECV, and without them it is refused and
// what is missing is said. The entry is in the mode and on the frequency
// the radio is in when it is saved, at the time by the radio's clock, in
// UTC, to the minute; a radio whose clock is not set refuses, since an
// entry without its time is of no use. The fields are kept when the entry
// is refused, so that nothing typed is lost; when it is saved they are
// wiped for the next, and the panel is sent the newest entries afresh, the
// new one at the top.
//
// WIPE clears the five fields, and nothing is written.
//
// The callsigns are logged in capitals, however they were typed.
//
// For the thread that owns the controls.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "control.h"
#include "logbook.h"
#include "logger.h"
#include "panel.h"
#include "panel_log.h"
#include "radio_clock.h"

#define HZ_PER_KHZ 1000
#define SAID_ROOM 80
#define EVERY_STATION ""

static struct control fields[] = {
	{ .group = "LOGGER", .name = "CALL", .kind = CONTROL_TEXT },
	{ .group = "LOGGER", .name = "SENT", .kind = CONTROL_TEXT },
	{ .group = "LOGGER", .name = "RECV", .kind = CONTROL_TEXT },
	{ .group = "LOGGER", .name = "EXCH", .kind = CONTROL_TEXT },
	{ .group = "LOGGER", .name = "NR", .kind = CONTROL_TEXT },
};
#define FIELDS ((int)(sizeof(fields) / sizeof(fields[0])))

// what an entry cannot be saved without
static const char *const needed[] = { "CALL", "SENT", "RECV" };
#define NEEDED ((int)(sizeof(needed) / sizeof(needed[0])))

static void upper(char *to, int room, const char *from) {
	int i;

	for (i = 0; from[i] != 0 && i < room - 1; i++)
		to[i] = (char)toupper((unsigned char)from[i]);
	to[i] = 0;
}

static void wipe(void) {
	int i;

	for (i = 0; i < FIELDS; i++)
		control_update(fields[i].name, "");
}

static void say(const char *text) {
	printf("logger: %s\n", text);
	fflush(stdout);
	panel_say(text);
}

static int save(char *why, int room) {
	struct logbook_row row;
	char call[LOGBOOK_FIELD_MAX];
	char said[SAID_ROOM];
	struct tm utc;
	time_t now;
	int i;

	for (i = 0; i < NEEDED; i++) {
		if (control_get(needed[i])[0] == 0) {
			snprintf(why, room, "%s is needed to log the entry", needed[i]);
			return -1;
		}
	}
	if (radio_clock_now(&now) == RADIO_CLOCK_NOT_SET) {
		snprintf(why, room, "the clock is not set; the entry needs the time");
		return -1;
	}
	memset(&row, 0, sizeof(row));
	gmtime_r(&now, &utc);
	snprintf(row.mode, sizeof(row.mode), "%s", control_get("MODE"));
	snprintf(row.freq, sizeof(row.freq), "%d", control_get_int("FREQ") / HZ_PER_KHZ);
	strftime(row.qso_date, sizeof(row.qso_date), "%Y-%m-%d", &utc);
	strftime(row.qso_time, sizeof(row.qso_time), "%H%M", &utc);
	snprintf(row.rst_sent, sizeof(row.rst_sent), "%s", control_get("SENT"));
	snprintf(row.exch_sent, sizeof(row.exch_sent), "%s", control_get("NR"));
	upper(row.callsign_recv, sizeof(row.callsign_recv), control_get("CALL"));
	snprintf(row.rst_recv, sizeof(row.rst_recv), "%s", control_get("RECV"));
	snprintf(row.exch_recv, sizeof(row.exch_recv), "%s", control_get("EXCH"));
	upper(call, sizeof(call), control_get("MYCALLSIGN"));
	if (logbook_add(&row, call) < 0) {
		snprintf(why, room, "the logbook could not be written");
		return -1;
	}
	snprintf(said, sizeof(said), "%s logged, sent %s, received %s", row.callsign_recv,
		row.rst_sent, row.rst_recv);
	say(said);
	wipe();
	panel_log_list(EVERY_STATION);
	return 0;
}

static int apply_logger(struct control *c, const char *value, char *why, int room) {
	(void)c;
	if (!strcmp(value, "SAVE"))
		return save(why, room);
	wipe();
	return 0;
}

static struct control logger = {
	.group = "LOGGER", .name = "LOGGER", .kind = CONTROL_ACTION,
	.choices = "SAVE/WIPE", .apply = apply_logger,
};

int logger_open(void) {
	int i;

	for (i = 0; i < FIELDS; i++) {
		if (control_register(&fields[i]) < 0)
			return -1;
	}
	return control_register(&logger);
}
