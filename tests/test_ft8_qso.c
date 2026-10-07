// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_ft8_qso.c — FT8 contacts, through every branch of the states the
// design at the top of modems/modem_ft8.c sets out, and the moment each
// is logged. It needs no radio.
//
// The modem is stood in for: what ft8_qso.c arms is kept here with its
// slot, the clock is the test's, and a slot is searched when the test says
// so. What the other station sends is added to messages.c, as the modem
// adds what it copies. The logbook is a real one, made for the test.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sqlite3.h>

#include "control.h"
#include "ft8_qso.h"
#include "logbook.h"
#include "messages.h"
#include "panel.h"

#define LOGBOOK_PATH "build/test_ft8_qso.db"
// 2026-10-02 04:30:00 UTC, the start of an even slot; each check begins
// an hour of slots after the last, so that what was heard in one is not in
// the slots of the next
#define FIRST_T0 1790915400L
#define SLOTS_APART 240
#define SLOT 15
#define SEARCHED_AT 14.7
#define ROOM 64
#define ROWS 8
#define PITCH 1000

static double now;
static time_t T0 = FIRST_T0;
static time_t searched;
static char armed[ROOM];
static time_t armed_slot;
static int arms;
static int is_cancelled;
static char said[ROOM];
static char qso_line[ROOM];
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

/* the stand-ins */

double ft8_time_now(void) {
	return now;
}

time_t ft8_searched_slot(void) {
	return searched;
}

int ft8_tx_arm(const char *message, int pitch_hz, time_t slot_start) {
	(void)pitch_hz;
	snprintf(armed, sizeof(armed), "%s", message);
	armed_slot = slot_start;
	arms++;
	is_cancelled = 0;
	return 0;
}

void ft8_tx_cancel(void) {
	is_cancelled = 1;
}

void ft8_callsign_remember(const char *callsign) {
	(void)callsign;
}

void panel_show_qso(const char *text) {
	snprintf(qso_line, sizeof(qso_line), "%s", text);
}

void panel_say(const char *text) {
	snprintf(said, sizeof(said), "%s", text);
}

/* the radio's controls that ft8_qso.c reads */

static struct control mycallsign = {
	.group = "STATION", .name = "MYCALLSIGN", .kind = CONTROL_TEXT, .value = "vu2ese",
};
static struct control mygrid = {
	.group = "STATION", .name = "MYGRID", .kind = CONTROL_TEXT, .value = "MK97fx",
};
static struct control mode = {
	.group = "RADIO", .name = "MODE", .kind = CONTROL_CHOICE, .choices = "USB/FT8",
	.value = "FT8",
};
static struct control freq = {
	.group = "RADIO", .name = "FREQ", .kind = CONTROL_NUMBER, .min = 0, .max = 30000000,
	.step = 1, .value = "14074000",
};

/* what happens on the air */

static void set(const char *name, const char *value) {
	char why[ROOM];

	control_set(name, value, ORIGIN_NONE, why, sizeof(why));
}

static void hear(time_t slot, int snr, const char *text) {
	message_add("FT8", slot, snr, PITCH, text);
}

// the slot has been searched, and the contact looks at it
static void searched_slot(time_t slot) {
	searched = slot;
	now = (double)slot + SEARCHED_AT;
	ft8_qso_poll();
}

static int is_armed(const char *message, time_t slot) {
	return !strcmp(armed, message) && armed_slot == slot && !is_cancelled;
}

static int logged(struct logbook_row *rows) {
	return logbook_find("", rows, ROWS);
}

static void fresh_logbook(void) {
	sqlite3 *db;

	logbook_close();
	remove(LOGBOOK_PATH);
	sqlite3_open(LOGBOOK_PATH, &db);
	sqlite3_exec(db,
		"CREATE TABLE logbook (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, mode TEXT, "
		"freq TEXT, qso_date TEXT, qso_time TEXT, callsign_sent TEXT, rst_sent TEXT, "
		"exch_sent TEXT DEFAULT \"\", callsign_recv TEXT, rst_recv TEXT, "
		"exch_recv TEXT DEFAULT \"\", tx_id TEXT DEFAULT \"\", comments TEXT DEFAULT \"\", "
		"deleted default 0);", NULL, NULL, NULL);
	sqlite3_close(db);
	logbook_open(LOGBOOK_PATH);
}

// a new start: the logbook empty, nothing armed, the contact stopped
static void afresh(const char *ops, const char *reply) {
	T0 += SLOTS_APART * SLOT;
	fresh_logbook();
	set("FT8_OPS", ops);
	set("FT8_REPLY", reply);
	set("FT8_REPEAT", "4");
	set("FT8_AUTO", "OFF");
	set("FT8_TX1ST", "ON");
	now = (double)T0 - SLOT;
	set("FT8_DO", "HALT");
	armed[0] = 0;
	arms = 0;
}

static void call_cq_at(double t) {
	now = t;
	set("FT8_DO", "CQ");
}

/* the contacts */

static void check_calling_cq(void) {
	struct logbook_row rows[ROWS];
	int arms_then;

	printf("calling CQ, FT8 Reply RR73:\n");
	afresh("NORMAL", "RR73");
	call_cq_at(T0 + 0.2);
	expect(is_armed("CQ VU2ESE MK97", T0), "CQ is sent in the slot just begun");

	hear(T0 + SLOT, -12, "VU2ESE G0XYZ IO91");
	hear(T0 + SLOT, -3, "VU2ESE K1ABC FN42");
	searched_slot(T0 + SLOT);
	expect(is_armed("G0XYZ VU2ESE -12", T0 + 2 * SLOT),
		"the first to answer is sent its report, in the next slot");

	hear(T0 + 3 * SLOT, -1, "VU2ESE K1ABC R-03");
	searched_slot(T0 + 3 * SLOT);
	expect(is_armed("G0XYZ VU2ESE -12", T0 + 4 * SLOT) && logged(rows) == 0,
		"another station's R and report are not taken: the report is sent again");

	hear(T0 + 5 * SLOT, -11, "VU2ESE G0XYZ R-07");
	searched_slot(T0 + 5 * SLOT);
	expect(is_armed("G0XYZ VU2ESE RR73", T0 + 6 * SLOT),
		"the R and report come: RR73 is sent");
	expect(!strcmp(qso_line, "G0XYZ > IO91 > -12 x2 > R-07 > RR73"),
		"the status line has the exchange, theirs and ours in turn");
	expect(logged(rows) == 1 && !strcmp(rows[0].callsign_recv, "G0XYZ")
		&& !strcmp(rows[0].rst_sent, "-12") && !strcmp(rows[0].rst_recv, "-07")
		&& !strcmp(rows[0].exch_recv, "IO91") && !strcmp(rows[0].exch_sent, "MK97")
		&& !strcmp(rows[0].freq, "14074") && !strcmp(rows[0].mode, "FT8"),
		"and the contact is logged then, with the reports, the grids and the frequency");

	hear(T0 + 7 * SLOT, -10, "VU2ESE G0XYZ R-07");
	searched_slot(T0 + 7 * SLOT);
	expect(is_armed("G0XYZ VU2ESE RR73", T0 + 8 * SLOT) && logged(rows) == 1,
		"the R and report again: RR73 is sent again, and it is not logged again");
	expect(!strcmp(qso_line, "G0XYZ > IO91 > -12 x2 > R-07 x2 > RR73 x2"),
		"and the words that passed again are counted, not written again");

	hear(T0 + 9 * SLOT, -10, "VU2ESE G0XYZ 73");
	arms_then = arms;
	searched_slot(T0 + 9 * SLOT);
	expect(arms == arms_then && logged(rows) == 1,
		"the 73 ends it; in NORMAL nothing more is sent");
	expect(qso_line[0] == 0, "and the status line no longer has a contact");
}

static void check_rrr(void) {
	struct logbook_row rows[ROWS];

	printf("calling CQ, FT8 Reply RRR:\n");
	afresh("NORMAL", "RRR");
	call_cq_at(T0 + 0.2);
	hear(T0 + SLOT, -12, "VU2ESE G0XYZ IO91");
	searched_slot(T0 + SLOT);
	hear(T0 + 3 * SLOT, -11, "VU2ESE G0XYZ R-07");
	searched_slot(T0 + 3 * SLOT);
	expect(is_armed("G0XYZ VU2ESE RRR", T0 + 4 * SLOT) && logged(rows) == 0,
		"RRR is sent, and with RRR it is not logged yet");
	hear(T0 + 5 * SLOT, -11, "VU2ESE G0XYZ 73");
	searched_slot(T0 + 5 * SLOT);
	expect(logged(rows) == 1, "it is logged when the 73 comes");

	afresh("NORMAL", "RRR");
	call_cq_at(T0 + 0.2);
	hear(T0 + SLOT, -12, "VU2ESE G0XYZ IO91");
	searched_slot(T0 + SLOT);
	hear(T0 + 3 * SLOT, -11, "VU2ESE G0XYZ R-07");
	searched_slot(T0 + 3 * SLOT);
	searched_slot(T0 + 5 * SLOT);
	expect(logged(rows) == 0, "with no 73, not after one quiet slot");
	searched_slot(T0 + 7 * SLOT);
	expect(logged(rows) == 1, "but after two");
}

static void check_cq_unanswered(void) {
	struct logbook_row rows[ROWS];
	int i;

	printf("CQ with no answer:\n");
	afresh("NORMAL", "RR73");
	call_cq_at(T0 + 3.0);
	expect(is_armed("CQ VU2ESE MK97", T0 + 2 * SLOT),
		"with TX1ST ON, pressed three seconds into the slot at :00, CQ waits for :30");
	call_cq_at(T0 + SLOT + 0.2);
	expect(is_armed("CQ VU2ESE MK97", T0 + 2 * SLOT),
		"and pressed at :15, it waits for :30 too");

	afresh("NORMAL", "RR73");
	set("FT8_TX1ST", "OFF");
	call_cq_at(T0 + 3.0);
	expect(is_armed("CQ VU2ESE MK97", T0 + SLOT),
		"with TX1ST OFF, pressed three seconds into the slot at :00, CQ goes at :15");
	for (i = 1; i <= 3; i++)
		searched_slot(T0 + (2 * i) * SLOT);
	expect(arms == 3 && logged(rows) == 0 && strstr(said, "no answer"),
		"in NORMAL, CQ is called three times, and then no more");

	afresh("RUNNING", "RR73");
	call_cq_at(T0 + 0.2);
	for (i = 1; i <= 5; i++)
		searched_slot(T0 + (2 * i - 1) * SLOT);
	expect(arms == 6 && is_armed("CQ VU2ESE MK97", T0 + 10 * SLOT),
		"in RUNNING, CQ goes on being called");
}

static void check_running(void) {
	struct logbook_row rows[ROWS];

	printf("RUNNING:\n");
	afresh("RUNNING", "RR73");
	call_cq_at(T0 + 0.2);
	hear(T0 + SLOT, -12, "VU2ESE G0XYZ IO91");
	searched_slot(T0 + SLOT);
	hear(T0 + 3 * SLOT, -11, "VU2ESE G0XYZ R-07");
	searched_slot(T0 + 3 * SLOT);
	hear(T0 + 5 * SLOT, -11, "VU2ESE G0XYZ 73");
	searched_slot(T0 + 5 * SLOT);
	expect(logged(rows) == 1 && is_armed("CQ VU2ESE MK97", T0 + 6 * SLOT),
		"after a contact, CQ is called again");

	hear(T0 + 7 * SLOT, -20, "VU2ESE W9XYZ -05");
	searched_slot(T0 + 7 * SLOT);
	expect(is_armed("W9XYZ VU2ESE R-20", T0 + 8 * SLOT),
		"a station that answers with a report at once is sent the R and ours");
	searched_slot(T0 + 9 * SLOT);
	searched_slot(T0 + 11 * SLOT);
	searched_slot(T0 + 13 * SLOT);
	expect(is_armed("W9XYZ VU2ESE R-20", T0 + 14 * SLOT),
		"it is sent four times");
	searched_slot(T0 + 15 * SLOT);
	expect(is_armed("CQ VU2ESE MK97", T0 + 16 * SLOT) && logged(rows) == 1,
		"one that goes quiet is given up after four sends, unlogged, and CQ is called");
}

static void check_answering(void) {
	struct logbook_row rows[ROWS];
	int arms_then;

	printf("answering a CQ:\n");
	afresh("POUNCING", "RR73");
	now = T0 + 2 * SLOT + 14.8;
	expect(ft8_qso_answer("043030|-15|1200|CQ G0XYZ IO91") == 0
		&& is_armed("G0XYZ VU2ESE MK97", T0 + 3 * SLOT),
		"a CQ heard in an even slot is answered in the next, odd, slot");

	hear(T0 + 4 * SLOT, -9, "VU2ESE G0XYZ -09");
	searched_slot(T0 + 4 * SLOT);
	expect(is_armed("G0XYZ VU2ESE R-15", T0 + 5 * SLOT),
		"the report comes: the R and ours go, ours from the CQ's strength");

	hear(T0 + 6 * SLOT, -8, "VU2ESE G0XYZ -09");
	searched_slot(T0 + 6 * SLOT);
	expect(is_armed("G0XYZ VU2ESE R-15", T0 + 7 * SLOT) && logged(rows) == 0,
		"the report again, ours was missed: the R and report again, not logged");

	hear(T0 + 8 * SLOT, -8, "VU2ESE G0XYZ RR73");
	searched_slot(T0 + 8 * SLOT);
	expect(logged(rows) == 1 && !strcmp(rows[0].rst_sent, "-15")
		&& !strcmp(rows[0].rst_recv, "-09") && !strcmp(rows[0].exch_recv, "IO91"),
		"RR73 comes: it is logged then");
	expect(is_armed("G0XYZ VU2ESE 73", T0 + 9 * SLOT), "and 73 is sent");
	expect(!strcmp(qso_line, "G0XYZ > MK97 > -09 x2 > R-15 x2 > RR73 > 73"),
		"the exchange, answering: our grid, the reports, RR73 and 73");

	arms_then = arms;
	searched_slot(T0 + 10 * SLOT);
	expect(arms == arms_then && logged(rows) == 1, "and the contact is over");

	afresh("POUNCING", "RR73");
	now = T0 + 2 * SLOT + 14.8;
	ft8_qso_answer("043030|-15|1200|CQ G0XYZ IO91");
	hear(T0 + 4 * SLOT, -9, "VU2ESE G0XYZ -09");
	searched_slot(T0 + 4 * SLOT);
	hear(T0 + 6 * SLOT, -9, "VU2ESE G0XYZ 73");
	searched_slot(T0 + 6 * SLOT);
	expect(logged(rows) == 1, "a 73 without the RR73 is logged too");

	afresh("POUNCING", "RR73");
	now = T0 + 2 * SLOT + 14.8;
	ft8_qso_answer("043030|-15|1200|CQ G0XYZ IO91");
	hear(T0 + 4 * SLOT, -9, "VU2ESE K1ABC RR73");
	searched_slot(T0 + 4 * SLOT);
	expect(logged(rows) == 0 && is_armed("G0XYZ VU2ESE MK97", T0 + 5 * SLOT),
		"another station's RR73 to us is not logged");
	searched_slot(T0 + 6 * SLOT);
	searched_slot(T0 + 8 * SLOT);
	searched_slot(T0 + 10 * SLOT);
	expect(logged(rows) == 0 && strstr(said, "given up"),
		"no answer after four calls: given up, not logged");
}

static void check_pouncing_rules(void) {
	printf("what may be answered:\n");
	afresh("POUNCING", "RR73");
	call_cq_at(T0 + 0.2);
	expect(arms == 0, "pouncing, CQ is not called");
	now = T0 + 2 * SLOT + 14.8;
	expect(ft8_qso_answer("043030|-15|1200|K1ABC G0XYZ RR73") < 0 && arms == 0,
		"pouncing, the end of another's contact is not answered");
	afresh("NORMAL", "RR73");
	now = T0 + 2 * SLOT + 14.8;
	expect(ft8_qso_answer("043030|-15|1200|K1ABC G0XYZ RR73") == 0
		&& is_armed("G0XYZ VU2ESE MK97", T0 + 3 * SLOT),
		"in NORMAL it is: G0XYZ is called as it ends with K1ABC");
	expect(ft8_qso_answer("043030|TX|1200|CQ VU2ESE MK97") < 0,
		"our own message is not answered");
}

static void check_waiting(void) {
	printf("the station answers another:\n");
	afresh("NORMAL", "RR73");
	now = T0 + 2 * SLOT + 14.8;
	ft8_qso_answer("043030|-15|1200|CQ G0XYZ IO91");
	hear(T0 + 4 * SLOT, -9, "K1ABC G0XYZ -11");
	searched_slot(T0 + 4 * SLOT);
	expect(is_cancelled, "G0XYZ answered K1ABC: we stop sending");
	hear(T0 + 8 * SLOT, -9, "VU2ESE G0XYZ -09");
	searched_slot(T0 + 6 * SLOT);
	searched_slot(T0 + 8 * SLOT);
	expect(is_armed("G0XYZ VU2ESE R-15", T0 + 9 * SLOT),
		"and when G0XYZ comes back to us, we go on");
}

static void check_repeat(void) {
	struct logbook_row rows[ROWS];

	printf("REPEAT:\n");
	afresh("POUNCING", "RR73");
	set("FT8_REPEAT", "2");
	now = T0 + 2 * SLOT + 14.8;
	ft8_qso_answer("043030|-15|1200|CQ G0XYZ IO91");
	searched_slot(T0 + 4 * SLOT);
	expect(arms == 2 && is_armed("G0XYZ VU2ESE MK97", T0 + 5 * SLOT),
		"with REPEAT at 2, the call is sent a second time");
	searched_slot(T0 + 6 * SLOT);
	expect(arms == 2 && logged(rows) == 0 && strstr(said, "given up"),
		"and then given up");
}

static void check_auto(void) {
	struct logbook_row rows[ROWS];

	printf("AUTO:\n");
	afresh("NORMAL", "RR73");
	hear(T0 + SLOT, -11, "VU2ESE W9XYZ EN37");
	searched_slot(T0 + SLOT);
	expect(arms == 0, "with AUTO off, a station that calls us is only shown");

	afresh("POUNCING", "RR73");
	set("FT8_AUTO", "ON");
	hear(T0 + SLOT, -11, "VU2ESE W9XYZ EN37");
	searched_slot(T0 + SLOT);
	expect(is_armed("W9XYZ VU2ESE -11", T0 + 2 * SLOT),
		"with AUTO on, it is answered with its report, even when pouncing");
	hear(T0 + 3 * SLOT, -10, "VU2ESE W9XYZ R-04");
	searched_slot(T0 + 3 * SLOT);
	expect(is_armed("W9XYZ VU2ESE RR73", T0 + 4 * SLOT) && logged(rows) == 1,
		"and the contact goes on as any other, and is logged");
	hear(T0 + 5 * SLOT, -10, "VU2ESE W9XYZ 73");
	searched_slot(T0 + 5 * SLOT);

	hear(T0 + 7 * SLOT, -10, "VU2ESE W9XYZ R-04");
	hear(T0 + 7 * SLOT, -10, "VU2ESE K1ABC RR73");
	searched_slot(T0 + 7 * SLOT);
	expect(logged(rows) == 1 && !strcmp(qso_line, ""),
		"the last station's R and report again, and a late RR73, start nothing");

	hear(T0 + 9 * SLOT, -16, "VU2ESE K1ABC -08");
	searched_slot(T0 + 9 * SLOT);
	expect(is_armed("K1ABC VU2ESE R-16", T0 + 10 * SLOT),
		"another station that calls with a report is answered with the R and ours");
}

static void check_stops(void) {
	printf("what stops a contact:\n");
	afresh("NORMAL", "RR73");
	call_cq_at(T0 + 0.2);
	set("MODE", "USB");
	ft8_qso_poll();
	expect(is_cancelled && strstr(said, "mode"), "a change of mode stops it");
	set("MODE", "FT8");
	afresh("NORMAL", "RR73");
	call_cq_at(T0 + 0.2);
	set("FT8_DO", "HALT");
	expect(is_cancelled, "HALT stops it");
}

int main(void) {
	control_register(&mycallsign);
	control_register(&mygrid);
	control_register(&mode);
	control_register(&freq);
	ft8_qso_open();

	check_calling_cq();
	check_rrr();
	check_cq_unanswered();
	check_running();
	check_answering();
	check_pouncing_rules();
	check_waiting();
	check_repeat();
	check_auto();
	check_stops();

	logbook_close();
	if (checks_failed) {
		printf("ft8 contacts: %d checks FAILED\n", checks_failed);
		return 1;
	}
	printf("ft8 contacts: all checks passed\n");
	return 0;
}
