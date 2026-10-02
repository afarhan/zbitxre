// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_messages.c — the messages the radio has copied, sent to the front
// panel.
//
// In FT8 the panel shows a list of the messages where the spectrum and the
// waterfall are in the other modes. The messages are in messages.c, added
// there by the modem; this reads them from there as they come and sends
// them on, each a line of text, the fields with a | between them:
//
//     043015|-12|1797|CQ VU2ESE MK97
//
// the time the message was sent at, in UTC, as hours, minutes and seconds,
// which for FT8 is the start of its slot; how strong it was, in dB over the
// noise; its pitch in the passband, in Hz; and its text. How the panel
// shows them is the panel's business.
//
// The lines go as the kept, counted value of LINK_MESSAGES, as many to a
// part as fit, each part when the panel has taken the one before;
// front_panel/link.c says what is in a part. A busy band has thirty or
// more messages in a slot, which is five or six parts.
//
// Nothing is sent again. A panel that was not there, or was restarted,
// starts with an empty list and has what is copied from then on; of what
// it missed, it is sent what messages.c still has.
//
// For the thread that owns the controls.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "link.h"
#include "messages.h"
#include "panel.h"
#include "panel_messages.h"

static uint8_t part[LINK_COUNTED_MAX];
static unsigned long last_sent;

static int make_a_line(char *line, int room, const struct message *m) {
	struct tm utc;

	gmtime_r(&m->at, &utc);
	return snprintf(line, room, "%02d%02d%02d|%d|%d|%s", utc.tm_hour, utc.tm_min,
		utc.tm_sec, m->snr_db, m->pitch_hz, m->text);
}

void panel_messages_poll(void) {
	char line[LINK_MESSAGE_LINE_MAX + 1];
	struct message m;
	int used = 0;
	int length;

	if (panel_is_waiting(LINK_MESSAGES))
		return;
	while (used + (int)sizeof(line) <= (int)sizeof(part) && message_next(&last_sent, &m)) {
		length = make_a_line(line, sizeof(line), &m);
		if (length > LINK_MESSAGE_LINE_MAX)
			length = LINK_MESSAGE_LINE_MAX;
		memcpy(part + used, line, length);
		part[used + length] = 0;
		used += length + 1;
	}
	if (used > 0)
		panel_set_kept_bytes(LINK_MESSAGES, part, used);
}
