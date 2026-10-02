// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// logbook.c — the station's logbook, as zbitxv2 kept it.
//
// The logbook is zbitxv2's own file, sbitx.db, an SQLite database, in the
// radio's data folder, so that a station's log goes on from the one
// program to the other. Its table is zbitxv2's, and the names here are
// its columns':
//
//     logbook: id, mode, freq, qso_date, qso_time, callsign_sent, rst_sent,
//              exch_sent, callsign_recv, rst_recv, exch_recv, tx_id,
//              comments, and in later files deleted
//
// freq is in kHz, qso_date is 2026-04-08 and qso_time 0445, both UTC.
//
// For now the logbook is only read: logbook_latest() gives the newest
// contacts, the newest first, leaving out those marked deleted. A file
// from before zbitxv2 had the "deleted" column is read without it.
//
// It needs the SQLite library, as zbitxv2 did.
//
// For the thread that owns the controls.

#include <stdio.h>
#include <string.h>

#include <sqlite3.h>

#include "logbook.h"

#define STATEMENT_MAX 300
#define COLUMNS "id, mode, freq, qso_date, qso_time, rst_sent, exch_sent, " \
	"callsign_recv, rst_recv, exch_recv"

static sqlite3 *db;

// opens the logbook to be read; 0, or -1 if there is no such file
int logbook_open(const char *path) {
	logbook_close();
	if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
		logbook_close();
		return -1;
	}
	return 0;
}

void logbook_close(void) {
	if (db != NULL)
		sqlite3_close(db);
	db = NULL;
}

// a column's text, cut to what a field holds; one that is empty in the file
// is an empty text
static void take_text(sqlite3_stmt *row, int column, char *field) {
	const unsigned char *text = sqlite3_column_text(row, column);

	field[0] = 0;
	if (text != NULL)
		snprintf(field, LOGBOOK_FIELD_MAX, "%s", (const char *)text);
}

static sqlite3_stmt *prepare(const char *where, int room) {
	char statement[STATEMENT_MAX];
	sqlite3_stmt *prepared = NULL;

	snprintf(statement, sizeof(statement),
		"select " COLUMNS " from logbook %s order by id desc limit %d;", where, room);
	if (sqlite3_prepare_v2(db, statement, -1, &prepared, NULL) != SQLITE_OK)
		return NULL;
	return prepared;
}

// the newest contacts, the newest first, no more than there is room for;
// says how many, or -1 if the logbook is not open or cannot be read
int logbook_latest(struct logbook_row *rows, int room) {
	sqlite3_stmt *found;
	int count = 0;

	if (db == NULL)
		return -1;
	found = prepare("where deleted = 0", room);
	if (found == NULL)
		found = prepare("", room);
	if (found == NULL)
		return -1;
	while (count < room && sqlite3_step(found) == SQLITE_ROW) {
		rows[count].id = sqlite3_column_int(found, 0);
		take_text(found, 1, rows[count].mode);
		take_text(found, 2, rows[count].freq);
		take_text(found, 3, rows[count].qso_date);
		take_text(found, 4, rows[count].qso_time);
		take_text(found, 5, rows[count].rst_sent);
		take_text(found, 6, rows[count].exch_sent);
		take_text(found, 7, rows[count].callsign_recv);
		take_text(found, 8, rows[count].rst_recv);
		take_text(found, 9, rows[count].exch_recv);
		count++;
	}
	sqlite3_finalize(found);
	return count;
}
