// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_logbook.c — the logbook read: the newest entries, the newest
// first, without the deleted ones. It makes a logbook of its own in
// build/, with zbitxv2's table, and needs no radio.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "logbook.h"

#define LOGBOOK_PATH "build/test_logbook.db"
#define OLD_LOGBOOK_PATH "build/test_logbook_old.db"
#define ROWS 8

static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static void make_a_logbook(const char *path, const char *statements) {
	sqlite3 *db;

	remove(path);
	if (sqlite3_open(path, &db) != SQLITE_OK
			|| sqlite3_exec(db, statements, NULL, NULL, NULL) != SQLITE_OK) {
		printf("the test's own logbook could not be made\n");
		exit(EXIT_FAILURE);
	}
	sqlite3_close(db);
}

int main(void) {
	struct logbook_row rows[ROWS];
	int count;

	expect(logbook_find("", rows, ROWS) < 0, "with no logbook open there is nothing to read");
	expect(logbook_open("build/no_such_folder/sbitx.db") < 0, "a logbook that is not there does not open");

	make_a_logbook(LOGBOOK_PATH,
		"CREATE TABLE logbook (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, mode TEXT, "
		"freq TEXT, qso_date TEXT, qso_time TEXT, callsign_sent TEXT, rst_sent TEXT, "
		"exch_sent TEXT DEFAULT \"\", callsign_recv TEXT, rst_recv TEXT, "
		"exch_recv TEXT DEFAULT \"\", tx_id TEXT DEFAULT \"\", comments TEXT DEFAULT \"\", "
		"deleted default 0);"
		"INSERT INTO logbook (mode, freq, qso_date, qso_time, callsign_sent, rst_sent, "
		"exch_sent, callsign_recv, rst_recv, exch_recv) VALUES "
		"('FT8', '7074', '2023-04-04', '1154', 'VU2ESE', '-15', 'MK97', 'YG9DFZ', '-21', 'OI33'),"
		"('CW', '7030', '2023-04-05', '0101', 'VU2ESE', '599', '', 'DELETED', '599', ''),"
		"('LSB', '7130', '2026-04-08', '0445', 'VU2ESE', '59', '', 'VU2XZ', '59', '');"
		"UPDATE logbook SET deleted = 1 WHERE callsign_recv = 'DELETED';"
		"UPDATE logbook SET exch_recv = NULL WHERE callsign_recv = 'VU2XZ';");
	expect(logbook_open(LOGBOOK_PATH) == 0, "zbitxv2's logbook opens");
	count = logbook_find("", rows, ROWS);
	expect(count == 2, "of three entries the deleted one is left out");
	expect(rows[0].id == 3 && strcmp(rows[0].callsign_recv, "VU2XZ") == 0
		&& strcmp(rows[0].mode, "LSB") == 0 && strcmp(rows[0].freq, "7130") == 0
		&& strcmp(rows[0].qso_date, "2026-04-08") == 0 && strcmp(rows[0].qso_time, "0445") == 0
		&& strcmp(rows[0].rst_sent, "59") == 0 && rows[0].exch_recv[0] == 0,
		"the newest is first, with its fields, an empty one as an empty text");
	expect(rows[1].id == 1 && strcmp(rows[1].callsign_recv, "YG9DFZ") == 0
		&& strcmp(rows[1].exch_sent, "MK97") == 0 && strcmp(rows[1].rst_recv, "-21") == 0,
		"and the older one after it");
	expect(logbook_find(NULL, rows, 1) == 1 && rows[0].id == 3,
		"asked for one, it gives the newest");
	expect(logbook_find("YG9", rows, ROWS) == 1 && strcmp(rows[0].callsign_recv, "YG9DFZ") == 0,
		"given the beginning of a callsign, it gives the entries with that station");
	expect(logbook_find("vu2", rows, ROWS) == 1 && rows[0].id == 3,
		"small letters find it as capitals do");
	expect(logbook_find("2XZ", rows, ROWS) == 0, "the middle of a callsign finds nothing");
	expect(logbook_find("DEL", rows, ROWS) == 0, "nor is a deleted entry found");
	expect(logbook_find("x'; drop table logbook; --", rows, ROWS) == 0
		&& logbook_find("", rows, ROWS) == 2,
		"and what is searched for is never taken for anything but a callsign");

	expect(logbook_delete(1) == 0 && logbook_find("", rows, ROWS) == 1 && rows[0].id == 3,
		"an entry is deleted by its number, and is no longer found");
	expect(logbook_delete(1) < 0 && logbook_delete(99) < 0,
		"deleting one that is not there says so");
	logbook_close();
	expect(logbook_open(LOGBOOK_PATH) == 0 && logbook_find("", rows, ROWS) == 1,
		"closed and opened again, the logbook is still without it");

	make_a_logbook(OLD_LOGBOOK_PATH,
		"CREATE TABLE logbook (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, mode TEXT, "
		"freq TEXT, qso_date TEXT, qso_time TEXT, callsign_sent TEXT, rst_sent TEXT, "
		"exch_sent TEXT, callsign_recv TEXT, rst_recv TEXT, exch_recv TEXT);"
		"INSERT INTO logbook (mode, freq, qso_date, qso_time, callsign_sent, rst_sent, "
		"exch_sent, callsign_recv, rst_recv, exch_recv) VALUES "
		"('CW', '14010', '2022-01-01', '0000', 'VU2ESE', '599', '', 'W1AW', '579', '');");
	expect(logbook_open(OLD_LOGBOOK_PATH) == 0 && logbook_find("", rows, ROWS) == 1
		&& strcmp(rows[0].callsign_recv, "W1AW") == 0,
		"a logbook from before the deleted column is read too");
	logbook_close();

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("logbook: all checks passed\n");
	return EXIT_SUCCESS;
}
