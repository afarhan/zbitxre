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
// noise; its pitch in the passband, in Hz; and its text. A message the
// radio sent itself has TX where its strength would be:
//
//     043030|TX|1500|VU2ESE G0XYZ R-07
//
// How the panel shows them is the panel's business.
//
// THE PANEL ANSWERS A MESSAGE
//
// When the operator marks a message on the panel and clicks the knob, the
// panel sends the message back, as a text in parts with LINK_FT8_ANSWER
// for its control: the second of the minute its slot began at, its
// strength, its pitch and its text, "45|-12|1797|CQ VU2ESE MK97". It goes to
// ft8_qso.c, which says what is done with it.
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
#include "ft8_qso.h"

static uint8_t part[LINK_COUNTED_MAX];
static unsigned long last_sent;

void panel_messages_poll(void) {
	char line[LINK_MESSAGE_LINE_MAX + 1];
	struct message m;
	int used = 0;
	int length;

	if (panel_is_waiting(LINK_MESSAGES))
		return;
	while (used + (int)sizeof(line) <= (int)sizeof(part) && message_next(&last_sent, &m)) {
		length = message_line(&m, line, sizeof(line));
		if (length > LINK_MESSAGE_LINE_MAX)
			length = LINK_MESSAGE_LINE_MAX;
		memcpy(part + used, line, length);
		part[used + length] = 0;
		used += length + 1;
	}
	if (used > 0)
		panel_set_kept_bytes(LINK_MESSAGES, part, used);
}

// the panel's text, when it is whole: a message to be answered
void panel_messages_took_bytes(int control, const uint8_t *bytes, int count) {
	static char gathered[LINK_TEXT_MAX + 1];
	int number = 0;

	if (control != LINK_TEXT_PART)
		return;
	if (link_get_text_part(bytes, count, &number, gathered, sizeof(gathered)) != 1)
		return;
	if (number == LINK_FT8_ANSWER)
		ft8_qso_answer(gathered);
}
