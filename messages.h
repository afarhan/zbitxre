// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// messages.h — what the radio has copied off the air, kept for whoever
// shows it. See messages.c.

#pragma once

#include <time.h>

#define MESSAGE_MODE_ROOM 8
#define MESSAGE_TEXT_ROOM 32
#define MESSAGES_KEPT 128

struct message {
	unsigned long serial;
	time_t at;
	char mode[MESSAGE_MODE_ROOM];
	int snr_db;
	int pitch_hz;
	int is_sent;
	char text[MESSAGE_TEXT_ROOM];
};

void message_add(const char *mode, time_t at, int snr_db, int pitch_hz,
	const char *text);
void message_add_sent(const char *mode, time_t at, int pitch_hz, const char *text);
int message_next(unsigned long *last_read, struct message *m);
