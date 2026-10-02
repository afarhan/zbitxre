// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// logbook.h — the station's logbook, as zbitxv2 kept it. See logbook.c.

#pragma once

#define LOGBOOK_FIELD_MAX 16

struct logbook_row {
	int id;
	char mode[LOGBOOK_FIELD_MAX];
	char freq[LOGBOOK_FIELD_MAX];
	char qso_date[LOGBOOK_FIELD_MAX];
	char qso_time[LOGBOOK_FIELD_MAX];
	char rst_sent[LOGBOOK_FIELD_MAX];
	char exch_sent[LOGBOOK_FIELD_MAX];
	char callsign_recv[LOGBOOK_FIELD_MAX];
	char rst_recv[LOGBOOK_FIELD_MAX];
	char exch_recv[LOGBOOK_FIELD_MAX];
};

int logbook_open(const char *path);
void logbook_close(void);
int logbook_find(const char *callsign, struct logbook_row *rows, int room);
int logbook_delete(int id);
