// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_logger.c — an entry written by hand and saved to the logbook, as
// logger.c says: refused without CALL, SENT and RECV or without the time,
// kept as typed when refused, and logged in the radio's mode, on its
// frequency, at its time, then wiped. It makes a logbook of its own in
// build/, with zbitxv2's table, and needs no radio.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "control.h"
#include "logbook.h"
#include "logger.h"
#include "radio_clock.h"

#define LOGBOOK_PATH "build/test_logger.db"
#define ROWS 4
#define WHY_ROOM 80
#define SAID_ROOM 96
// 2026-10-05 14:32:10 UTC
#define A_TIME 1791210730LL

static int checks_failed;
static char said[SAID_ROOM];
static int lists_sent;
static int is_clock_set;

static struct control mode = {
	.group = "RADIO", .name = "MODE", .kind = CONTROL_CHOICE,
	.choices = "USB/LSB/CW", .value = "CW",
};
static struct control freq = {
	.group = "RADIO", .name = "FREQ", .kind = CONTROL_NUMBER,
	.min = 0, .max = 30000000, .step = 1, .value = "7030500",
};
static struct control my_callsign = {
	.group = "STATION", .name = "MYCALLSIGN", .kind = CONTROL_TEXT, .value = "vu2ese",
};

void panel_say(const char *text) {
	snprintf(said, sizeof(said), "%s", text);
}

void panel_log_list(const char *callsign) {
	(void)callsign;
	lists_sent++;
}

static void give(struct radio_clock_readings *readings) {
	readings->is_ntp_synced = is_clock_set;
	readings->rtc_s = 0;
	readings->system_s = A_TIME;
	readings->since_start_s = 0;
}

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static int set(const char *name, const char *value, char *why) {
	return control_set(name, value, ORIGIN_PANEL, why, WHY_ROOM);
}

static void make_a_logbook(void) {
	sqlite3 *db;

	remove(LOGBOOK_PATH);
	if (sqlite3_open(LOGBOOK_PATH, &db) != SQLITE_OK
			|| sqlite3_exec(db,
				"CREATE TABLE logbook (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, "
				"mode TEXT, freq TEXT, qso_date TEXT, qso_time TEXT, callsign_sent TEXT, "
				"rst_sent TEXT, exch_sent TEXT DEFAULT \"\", callsign_recv TEXT, "
				"rst_recv TEXT, exch_recv TEXT DEFAULT \"\", tx_id TEXT DEFAULT \"\", "
				"comments TEXT DEFAULT \"\", deleted default 0);",
				NULL, NULL, NULL) != SQLITE_OK) {
		printf("the test's own logbook could not be made\n");
		exit(EXIT_FAILURE);
	}
	sqlite3_close(db);
}

int main(void) {
	struct logbook_row rows[ROWS];
	char why[WHY_ROOM];
	int count;

	printf("logger:\n");
	make_a_logbook();
	if (logbook_open(LOGBOOK_PATH) < 0) {
		printf("the test's logbook did not open\n");
		return EXIT_FAILURE;
	}
	radio_clock_set_reading(give);
	if (control_register(&mode) < 0 || control_register(&freq) < 0
			|| control_register(&my_callsign) < 0 || logger_open() < 0) {
		printf("the controls could not be registered\n");
		return EXIT_FAILURE;
	}

	set("CALL", "vu2xz", why);
	set("SENT", "599", why);
	expect(set("LOGGER", "SAVE", why) < 0 && strstr(why, "RECV") != NULL,
		"without RECV the entry is refused, and RECV is said to be needed");
	expect(!strcmp(control_get("CALL"), "vu2xz") && !strcmp(control_get("SENT"), "599"),
		"and what was typed is kept");

	set("RECV", "579", why);
	set("EXCH", "MK97", why);
	set("NR", "001", why);
	expect(set("LOGGER", "SAVE", why) < 0 && strstr(why, "clock") != NULL,
		"with the clock not set the entry is refused, since it needs the time");

	is_clock_set = 1;
	expect(set("LOGGER", "SAVE", why) == 0, "with CALL, SENT, RECV and the time it is saved");
	count = logbook_find("", rows, ROWS);
	expect(count == 1, "and the logbook has the entry");
	if (count == 1) {
		printf("          %s %s %s %s %s %s %s %s %s\n", rows[0].qso_date, rows[0].qso_time,
			rows[0].freq, rows[0].mode, rows[0].callsign_recv, rows[0].rst_sent,
			rows[0].exch_sent, rows[0].rst_recv, rows[0].exch_recv);
		expect(!strcmp(rows[0].callsign_recv, "VU2XZ"), "the callsign in capitals");
		expect(!strcmp(rows[0].mode, "CW") && !strcmp(rows[0].freq, "7030"),
			"in the radio's mode, on its frequency in kHz");
		expect(!strcmp(rows[0].qso_date, "2026-10-05") && !strcmp(rows[0].qso_time, "1432"),
			"at the radio's time, in UTC, to the minute");
		expect(!strcmp(rows[0].rst_sent, "599") && !strcmp(rows[0].exch_sent, "001")
			&& !strcmp(rows[0].rst_recv, "579") && !strcmp(rows[0].exch_recv, "MK97"),
			"SENT and NR as what was sent, RECV and EXCH as what was received");
	}
	expect(strstr(said, "VU2XZ logged") != NULL, "the panel is told it is logged");
	expect(lists_sent == 1, "and is sent the logbook afresh");
	expect(control_get("CALL")[0] == 0 && control_get("SENT")[0] == 0
		&& control_get("RECV")[0] == 0 && control_get("EXCH")[0] == 0
		&& control_get("NR")[0] == 0, "and the fields are wiped for the next");

	set("CALL", "G0XYZ", why);
	set("RECV", "559", why);
	expect(set("LOGGER", "WIPE", why) == 0 && control_get("CALL")[0] == 0
		&& control_get("RECV")[0] == 0, "WIPE clears the fields");
	expect(logbook_find("", rows, ROWS) == 1, "and writes nothing");

	logbook_close();
	if (checks_failed) {
		printf("logger: %d check(s) FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("logger: all checks passed\n");
	return EXIT_SUCCESS;
}
