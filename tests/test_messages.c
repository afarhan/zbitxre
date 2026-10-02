// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_messages.c — what the radio copies off the air is kept, read by each
// reader at its own pace, and sent to the panel in parts. It needs no radio.
//
// The panel is stood in for: what panel_messages.c sets as LINK_MESSAGES is
// kept here, and the test says when the panel has taken it.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link.h"
#include "messages.h"
#include "panel.h"
#include "panel_messages.h"

#define LINES_MAX 400
// 2026-10-02 04:30:15 UTC
#define A_SLOT 1790915415

static char lines[LINES_MAX][LINK_MESSAGE_LINE_MAX + 1];
static int line_count;
static int parts_set;
static int longest_part;
static int is_part_waiting;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

void panel_set_kept_bytes(int control, const uint8_t *bytes, int count) {
	int at = 0;

	if (control != LINK_MESSAGES)
		return;
	parts_set++;
	is_part_waiting = 1;
	if (count > longest_part)
		longest_part = count;
	while (at < count && line_count < LINES_MAX) {
		snprintf(lines[line_count], sizeof(lines[line_count]), "%s", (const char *)bytes + at);
		at += (int)strlen((const char *)bytes + at) + 1;
		line_count++;
	}
}

int panel_is_waiting(int control) {
	return control == LINK_MESSAGES && is_part_waiting;
}

// the panel takes each part as it is set, until nothing more is sent
static void send_everything(void) {
	int before;

	do {
		before = parts_set;
		is_part_waiting = 0;
		panel_messages_poll();
	} while (parts_set != before);
}

int main(void) {
	unsigned long reader = 0;
	unsigned long late_reader = 0;
	struct message m;
	char text[MESSAGE_TEXT_ROOM];
	int read = 0;
	int i;

	printf("messages:\n");

	expect(!message_next(&reader, &m), "with nothing copied there is nothing to read");
	message_add("FT8", A_SLOT, -12, 1797, "CQ VU2ESE MK97");
	message_add("FT8", A_SLOT, 3, 512, "VU2XZ VU2ESE -07");
	expect(message_next(&reader, &m) && !strcmp(m.text, "CQ VU2ESE MK97")
		&& m.snr_db == -12 && m.pitch_hz == 1797 && !strcmp(m.mode, "FT8")
		&& m.at == A_SLOT, "a message is read as it was added");
	expect(message_next(&reader, &m) && !strcmp(m.text, "VU2XZ VU2ESE -07"),
		"and the next after it");
	expect(!message_next(&reader, &m), "and then there is no more");

	panel_messages_poll();
	expect(parts_set == 1 && line_count == 2
		&& !strcmp(lines[0], "043015|-12|1797|CQ VU2ESE MK97")
		&& !strcmp(lines[1], "043015|3|512|VU2XZ VU2ESE -07"),
		"the panel is sent both, a line to each: the time, the dB, the pitch, the text");
	message_add("FT8", A_SLOT + 15, -3, 900, "VU2ESE VU2XZ R-09");
	panel_messages_poll();
	expect(parts_set == 1, "nothing more goes while the panel has not taken the part");
	send_everything();
	expect(parts_set == 2 && line_count == 3
		&& !strcmp(lines[2], "043030|-3|900|VU2ESE VU2XZ R-09"),
		"and then the one that came meanwhile goes, once");

	for (i = 0; i < 40; i++) {
		snprintf(text, sizeof(text), "CQ K%dABC FN%02d", i % 10, i);
		message_add("FT8", A_SLOT + 30, -i, 300 + 50 * i, text);
	}
	send_everything();
	expect(line_count == 43 && !strcmp(lines[42], "043045|-39|2250|CQ K9ABC FN39"),
		"a busy slot of forty goes in parts, every message in its order");
	expect(longest_part <= LINK_COUNTED_MAX && parts_set > 3,
		"and no part is longer than the link carries");

	for (i = 0; i < 3 * MESSAGES_KEPT; i++)
		message_add("FT8", A_SLOT, 0, i, "TEST");
	while (message_next(&late_reader, &m)) {
		if (read == 0)
			expect(m.pitch_hz == 2 * MESSAGES_KEPT,
				"a reader that fell far behind starts from the oldest still kept");
		read++;
	}
	expect(read == MESSAGES_KEPT && m.pitch_hz == 3 * MESSAGES_KEPT - 1,
		"and reads what is kept, up to the newest");

	if (checks_failed) {
		printf("messages: %d checks FAILED\n", checks_failed);
		return 1;
	}
	printf("messages: all checks passed\n");
	return 0;
}
