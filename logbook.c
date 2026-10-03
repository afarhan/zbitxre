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
// logbook_add() puts in a new entry, as zbitxv2's logbook_add() did, with
// the callsign of the station that made it, ours, in callsign_sent. It is
// how a contact made in FT8 is logged; ft8_qso.c says when.
//
// The author's rule for a logbook: an entry in it is never changed, but a
// bad one can be deleted. logbook_delete() takes one out for good, by its
// number, as zbitxv2 did; nothing here changes an entry.
//
// logbook_find() gives the newest entries, the newest first, leaving out
// those marked deleted. Given a callsign, or the beginning of one, it gives
// only the entries with stations whose callsign begins so, as zbitxv2's
// search did; capitals and small letters are the same to it. A file from
// before zbitxv2 had the "deleted" column is read without it.
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

// opens the logbook; 0, or -1 if there is no such file. It is not made if
// it is not there
int logbook_open(const char *path) {
	logbook_close();
	if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
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

// the statement that finds the entries, with the callsign's beginning put
// in where the ? is, so that no character in it can be taken for SQL
static sqlite3_stmt *prepare(const char *also, const char *callsign, int room) {
	char statement[STATEMENT_MAX];
	sqlite3_stmt *prepared = NULL;

	snprintf(statement, sizeof(statement),
		"select " COLUMNS " from logbook where callsign_recv like ? || '%%' %s "
		"order by id desc limit %d;", also, room);
	if (sqlite3_prepare_v2(db, statement, -1, &prepared, NULL) != SQLITE_OK)
		return NULL;
	sqlite3_bind_text(prepared, 1, callsign, -1, SQLITE_TRANSIENT);
	return prepared;
}

// the newest entries, the newest first, no more than there is room for,
// with stations whose callsign begins as given; an empty beginning, or
// NULL, is every station. Says how many, or -1 if the logbook is not open
// or cannot be read
int logbook_find(const char *callsign, struct logbook_row *rows, int room) {
	sqlite3_stmt *found;
	int count = 0;

	if (db == NULL)
		return -1;
	if (callsign == NULL)
		callsign = "";
	found = prepare("and deleted = 0", callsign, room);
	if (found == NULL)
		found = prepare("", callsign, room);
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

// takes the entry of that number out of the logbook; 0, or -1 if there
// was no such entry or the logbook could not be written
int logbook_delete(int id) {
	sqlite3_stmt *prepared = NULL;
	int is_done;

	if (db == NULL)
		return -1;
	if (sqlite3_prepare_v2(db, "delete from logbook where id = ?;", -1, &prepared, NULL)
			!= SQLITE_OK)
		return -1;
	sqlite3_bind_int(prepared, 1, id);
	is_done = sqlite3_step(prepared) == SQLITE_DONE;
	sqlite3_finalize(prepared);
	if (!is_done || sqlite3_changes(db) != 1)
		return -1;
	return 0;
}

// a new entry, with our callsign; the row's number is not used, the
// logbook gives the entry its own. Says the entry's number, or -1 if the
// logbook is not open or cannot be written
int logbook_add(const struct logbook_row *row, const char *callsign_sent) {
	sqlite3_stmt *prepared = NULL;
	int is_done;

	if (db == NULL)
		return -1;
	if (sqlite3_prepare_v2(db, "insert into logbook (mode, freq, qso_date, qso_time, "
			"callsign_sent, rst_sent, exch_sent, callsign_recv, rst_recv, exch_recv) "
			"values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);", -1, &prepared, NULL) != SQLITE_OK)
		return -1;
	sqlite3_bind_text(prepared, 1, row->mode, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(prepared, 2, row->freq, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(prepared, 3, row->qso_date, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(prepared, 4, row->qso_time, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(prepared, 5, callsign_sent, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(prepared, 6, row->rst_sent, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(prepared, 7, row->exch_sent, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(prepared, 8, row->callsign_recv, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(prepared, 9, row->rst_recv, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(prepared, 10, row->exch_recv, -1, SQLITE_TRANSIENT);
	is_done = sqlite3_step(prepared) == SQLITE_DONE;
	sqlite3_finalize(prepared);
	if (!is_done)
		return -1;
	return (int)sqlite3_last_insert_rowid(db);
}
