// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// messages.c — what the radio has copied off the air, kept for whoever
// shows it.
//
// A modem that reads text from the signal, as FT8's does, adds each message
// here with message_add(), which was zbitxv2's name for it. It does not
// know who shows them. Whatever shows them, the terminal, the front panel,
// reads them from here with message_next(), each at its own pace:
//
//     static unsigned long last_read;
//     struct message m;
//
//     while (message_next(&last_read, &m))
//         show(&m);
//
// A message has the mode it came in, the time it was sent at (for FT8 the
// start of its slot), how strong it was in dB over the noise, its pitch in
// the passband in Hz, and its text.
//
// The newest MESSAGES_KEPT are kept. Each has a serial number, counted from
// 1, and a reader says the last it has read. One that has fallen further
// behind than what is kept is given the oldest that is still here, and so
// misses those between: a list on a screen is no worse for it.
//
// A modem adds from a thread of its own while another thread reads, so a
// lock is held for each call, for as long as it takes to copy one message.

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "messages.h"

static struct message kept[MESSAGES_KEPT];
static unsigned long newest_serial;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void message_add(const char *mode, time_t at, int snr_db, int pitch_hz,
		const char *text) {
	struct message *m;

	pthread_mutex_lock(&lock);
	newest_serial++;
	m = &kept[newest_serial % MESSAGES_KEPT];
	m->serial = newest_serial;
	m->at = at;
	m->snr_db = snr_db;
	m->pitch_hz = pitch_hz;
	snprintf(m->mode, sizeof(m->mode), "%s", mode);
	snprintf(m->text, sizeof(m->text), "%s", text);
	pthread_mutex_unlock(&lock);
}

// gives the message after the last one read, and says whether there was one
int message_next(unsigned long *last_read, struct message *m) {
	unsigned long wanted = *last_read + 1;
	int is_there = 0;

	pthread_mutex_lock(&lock);
	if (newest_serial >= MESSAGES_KEPT && wanted <= newest_serial - MESSAGES_KEPT)
		wanted = newest_serial - MESSAGES_KEPT + 1;
	if (wanted <= newest_serial) {
		*m = kept[wanted % MESSAGES_KEPT];
		*last_read = wanted;
		is_there = 1;
	}
	pthread_mutex_unlock(&lock);
	return is_there;
}
