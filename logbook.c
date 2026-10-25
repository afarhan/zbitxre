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
// logbook_line() writes an entry as the line of text it is shown as, by the
// front panel and by the network's clients alike, the fields with a |
// between them in the order zbitxv2's panel showed them:
//
//     972|2026-04-08|04:45|7130|LSB|VU2XZ|59|59
//
// the entry's number in the logbook, the date and the time in UTC, the
// frequency in kHz, the mode, the other station's callsign, what was sent
// and what was received. The last two are the report, and the exchange
// after it if there was one: "-14 MK97".
//
// logbook_changes() counts the entries added and deleted since the program
// began, so that whoever shows the logbook can tell that what it shows is
// no longer what is there.
//
// The whole logbook can be taken away as an ADIF file, the format logging
// programs, LoTW and QRZ read: logbook_adif_header() is the file's first
// line, and logbook_adif_after() each entry's record, a line to an entry,
// the oldest first:
//
//     <CALL:5>VU2XZ <QSO_DATE:8>20260408 <TIME_ON:4>0445 <FREQ:5>7.130
//     <BAND:3>40m <MODE:3>SSB <SUBMODE:3>LSB <RST_SENT:2>59 <RST_RCVD:2>59
//     <STATION_CALLSIGN:6>VU2ESE <EOR>
//
// (on one line). The radio's modes are ADIF's: USB and LSB are SSB, CWR is
// CW, RADE1 and RADE2 are FreeDV's digital voice, and JTTY, which ADIF
// does not yet have, is MFSK as FT4 is. An exchange that is a grid, as FT8
// sends, is GRIDSQUARE (ours MY_GRIDSQUARE); any other is SRX_STRING (ours
// STX_STRING). logbook_count() says how many entries there are.
//
// It needs the SQLite library, as zbitxv2 did.
//
// For the thread that owns the controls.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "logbook.h"

#define TIME_DIGITS 4
#define HOUR_DIGITS 2
// room for every field at its longest, before the line is cut
#define WHOLE_LINE_MAX (12 * LOGBOOK_FIELD_MAX)

static unsigned long changes;

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

// the entry as a line of text, cut to the room there is
void logbook_line(const struct logbook_row *row, char *line, int room) {
	char sent[2 * LOGBOOK_FIELD_MAX];
	char received[2 * LOGBOOK_FIELD_MAX];
	char at[LOGBOOK_FIELD_MAX];
	char whole[WHOLE_LINE_MAX];

	join(sent, sizeof(sent), row->rst_sent, row->exch_sent);
	join(received, sizeof(received), row->rst_recv, row->exch_recv);
	show_time(at, sizeof(at), row->qso_time);
	snprintf(whole, sizeof(whole), "%d|%s|%s|%s|%s|%s|%s|%s", row->id,
		row->qso_date, at, row->freq, row->mode, row->callsign_recv, sent, received);
	snprintf(line, room, "%.*s", room - 1, whole);
}

unsigned long logbook_changes(void) {
	return changes;
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
	changes++;
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
	changes++;
	return (int)sqlite3_last_insert_rowid(db);
}

// how many entries the logbook has, those marked deleted left out; -1 if
// it is not open or cannot be read
int logbook_count(void) {
	sqlite3_stmt *counted = NULL;
	int count = -1;

	if (db == NULL)
		return -1;
	if (sqlite3_prepare_v2(db, "select count(*) from logbook where deleted = 0;", -1,
			&counted, NULL) != SQLITE_OK
			&& sqlite3_prepare_v2(db, "select count(*) from logbook;", -1, &counted, NULL)
			!= SQLITE_OK)
		return -1;
	if (sqlite3_step(counted) == SQLITE_ROW)
		count = sqlite3_column_int(counted, 0);
	sqlite3_finalize(counted);
	return count;
}

const char *logbook_adif_header(void) {
	return "zbitxre logbook <ADIF_VER:5>3.1.4 <PROGRAMID:7>zbitxre <EOH>";
}

struct adif_band {
	int low_khz;
	int high_khz;
	const char *name;
};

static const struct adif_band adif_bands[] = {
	{ 1800, 2000, "160m" }, { 3500, 4000, "80m" }, { 5060, 5450, "60m" },
	{ 7000, 7300, "40m" }, { 10100, 10150, "30m" }, { 14000, 14350, "20m" },
	{ 18068, 18168, "17m" }, { 21000, 21450, "15m" }, { 24890, 24990, "12m" },
	{ 28000, 29700, "10m" }, { 50000, 54000, "6m" },
};

struct adif_mode {
	const char *ours;
	const char *mode;
	const char *submode;
};

static const struct adif_mode adif_modes[] = {
	{ "USB", "SSB", "USB" }, { "LSB", "SSB", "LSB" }, { "CWR", "CW", "" },
	{ "RADE1", "DIGITALVOICE", "FREEDV" }, { "RADE2", "DIGITALVOICE", "FREEDV" },
	{ "FT4", "MFSK", "FT4" }, { "JTTY", "MFSK", "JTTY" },
};

// a field put on the end of the record, if it has a value and there is room
static void adif_field(char *record, int room, const char *name, const char *value) {
	int used = (int)strlen(record);

	if (value[0] == 0 || used >= room)
		return;
	snprintf(record + used, room - used, "<%s:%d>%s ", name, (int)strlen(value), value);
}

// what is left of a text when all but its digits are taken out:
// 2026-04-08 is 20260408
static void only_digits(char *digits, int room, const char *text) {
	int n = 0;

	for (; *text != 0 && n < room - 1; text++)
		if (isdigit((unsigned char)*text))
			digits[n++] = *text;
	digits[n] = 0;
}

// a Maidenhead grid of four or six characters: MK97, MK97fa
static int is_grid(const char *text) {
	int length = (int)strlen(text);

	if (length != 4 && length != 6)
		return 0;
	if (toupper((unsigned char)text[0]) < 'A' || toupper((unsigned char)text[0]) > 'R'
			|| toupper((unsigned char)text[1]) < 'A' || toupper((unsigned char)text[1]) > 'R'
			|| !isdigit((unsigned char)text[2]) || !isdigit((unsigned char)text[3]))
		return 0;
	return length == 4 || (isalpha((unsigned char)text[4]) && isalpha((unsigned char)text[5]));
}

// the next entry after the one numbered so (0 for the first) as an ADIF
// record, a line of text without its end. Says the entry's number, 0 if
// there is none after it, or -1 if the logbook cannot be read
int logbook_adif_after(int after, char *record, int room) {
	static const char *const columns =
		"select id, mode, freq, qso_date, qso_time, callsign_sent, rst_sent, exch_sent, "
		"callsign_recv, rst_recv, exch_recv, %s from logbook where id > ? %s "
		"order by id limit 1;";
	// the files of before zbitxv2 had neither comments nor deleted
	static const char *const kinds[][2] = {
		{ "comments", "and deleted = 0" }, { "comments", "" }, { "''", "" },
	};
	char statement[STATEMENT_MAX];
	sqlite3_stmt *found = NULL;
	char field[12][LOGBOOK_FIELD_MAX * 4];
	char digits[LOGBOOK_FIELD_MAX];
	char mhz[LOGBOOK_FIELD_MAX];
	const char *mode, *submode = "";
	double khz;
	int id, i, step;

	if (db == NULL || room < 1)
		return -1;
	for (i = 0; found == NULL && i < (int)(sizeof(kinds) / sizeof(kinds[0])); i++) {
		snprintf(statement, sizeof(statement), columns, kinds[i][0], kinds[i][1]);
		if (sqlite3_prepare_v2(db, statement, -1, &found, NULL) != SQLITE_OK)
			found = NULL;
	}
	if (found == NULL)
		return -1;
	sqlite3_bind_int(found, 1, after);
	step = sqlite3_step(found);
	if (step != SQLITE_ROW) {
		sqlite3_finalize(found);
		return step == SQLITE_DONE ? 0 : -1;
	}
	id = sqlite3_column_int(found, 0);
	for (i = 1; i < 12; i++) {
		const unsigned char *text = sqlite3_column_text(found, i);

		char *at;

		snprintf(field[i], sizeof(field[i]), "%s", text != NULL ? (const char *)text : "");
		// a record is a line: a comment's line ends would end it
		for (at = field[i]; *at != 0; at++)
			if (iscntrl((unsigned char)*at))
				*at = ' ';
	}
	sqlite3_finalize(found);

	record[0] = 0;
	adif_field(record, room, "CALL", field[8]);
	only_digits(digits, sizeof(digits), field[3]);
	adif_field(record, room, "QSO_DATE", digits);
	only_digits(digits, sizeof(digits), field[4]);
	adif_field(record, room, "TIME_ON", digits);
	khz = atof(field[2]);
	mhz[0] = 0;
	if (khz > 0)
		snprintf(mhz, sizeof(mhz), "%.6g", khz / 1000.0);
	adif_field(record, room, "FREQ", mhz);
	for (i = 0; i < (int)(sizeof(adif_bands) / sizeof(adif_bands[0])); i++)
		if (khz >= adif_bands[i].low_khz && khz <= adif_bands[i].high_khz)
			adif_field(record, room, "BAND", adif_bands[i].name);
	mode = field[1];
	for (i = 0; i < (int)(sizeof(adif_modes) / sizeof(adif_modes[0])); i++)
		if (!strcmp(field[1], adif_modes[i].ours)) {
			mode = adif_modes[i].mode;
			submode = adif_modes[i].submode;
		}
	adif_field(record, room, "MODE", mode);
	adif_field(record, room, "SUBMODE", submode);
	adif_field(record, room, "RST_SENT", field[6]);
	adif_field(record, room, "RST_RCVD", field[9]);
	adif_field(record, room, is_grid(field[7]) ? "MY_GRIDSQUARE" : "STX_STRING", field[7]);
	adif_field(record, room, is_grid(field[10]) ? "GRIDSQUARE" : "SRX_STRING", field[10]);
	adif_field(record, room, "STATION_CALLSIGN", field[5]);
	adif_field(record, room, "COMMENT", field[11]);
	if ((int)strlen(record) + 6 > room)
		return -1;
	strcat(record, "<EOR>");
	return id;
}
