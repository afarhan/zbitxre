// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_cw.c — the CW read off the air, to the front panel and the
// terminal.
//
// modem_cw.c reads CW into text, a character at a time, with two decoders
// side by side, which the author is comparing; modem_cw.c says how they
// differ. This sends what they read on as it comes:
//
//   the reader's, cw_rx_take_reader(), to the panel, as the kept, counted
//   value of LINK_CW_TEXT: the characters read since the last value, no
//   more than a value holds. The author is watching it there, against
//   recordings played back with play=. The panel runs
//   them through its status line from right to left, as the author asked;
//   front_panel/main.cpp says how. The next value goes when the panel has
//   taken the one before, and what was read meanwhile waits for it.
//
//   the reader's, too, to whoever else tools/sdr.c names with
//   panel_cw_also_tell(), which is the radio's clients over the network:
//   the characters as they are read, with no waiting for the panel, since
//   a radio may have no panel at all, as the sbitx has none.
//
//   zbitxv2's, with its Goertzel filter, cw_rx_take(), to the terminal, a
//   line at a time: "cw goertzel: CQ CQ DE VU2ESE K", when the line is
//   LINE_MAX long or nothing more has come for IDLE_MS.
//
//   and what the radio sends itself, cw_text_take_sent(): the characters
//   of typed text, of macros and of what is keyed on a client's pads, as
//   each begins to be sent, to the panel and to the clients in the same
//   run of text as what is read. What is keyed on the radio's own paddle
//   is not among it: the keyer makes dots and dashes, not letters. The
//   same goes on its own too, to whoever panel_cw_also_tell_sent() names,
//   the clients over the network, and to the panel as LINK_CW_SENT: KBD keeps what has been
//   typed and not yet sent in a box, and takes each character out of it as
//   the radio says it has begun to send it, the author's asking of 18
//   October 2026.
//
//   and in JTTY, what is read in the bin chosen, JTTY_RX's, which
//   jtty_take_chosen_text() gives as it comes: to the panel only, in the
//   same run of text, which is where the panel shows what it is chatting
//   with; it has no list of the bins. The clients over the network have
//   the bins themselves, from the server. modems/modem_jtty.c says what a
//   bin is.
//
// For the thread that owns the controls.

#include <stdio.h>
#include <string.h>

#include "link.h"
#include "panel.h"
#include "panel_cw.h"
#include "modems/modem_cw.h"
#include "modems/cw_text.h"
#include "modems/modem_jtty.h"

#define LINE_MAX 60
#define IDLE_MS 3000
#define WAITING_MAX LINK_SAY_MAX

static uint8_t part[WAITING_MAX];
// what has been sent, waiting for the panel to take the value before
static uint8_t sent_part[WAITING_MAX];
static char sent_waiting[WAITING_MAX];
static int sent_waiting_count;
static char waiting[WAITING_MAX + 1];
static int waiting_count;
static char line[LINE_MAX + 1];
static int line_count;
static long last_read_ms;

static void (*also_read)(const char *text);
static void (*also_sent)(const char *text);

void panel_cw_also_tell(void (*read)(const char *text)) {
	also_read = read;
}

void panel_cw_also_tell_sent(void (*sent)(const char *text)) {
	also_sent = sent;
}

static void print_the_line(void) {
	if (line_count == 0)
		return;
	line[line_count] = 0;
	printf("cw goertzel: %s\n", line);
	fflush(stdout);
	line_count = 0;
}

void panel_cw_poll(long now_ms) {
	char read[WAITING_MAX + 1];
	int count = cw_rx_take_reader(read, sizeof(read) - 1);
	int i;

	if (count > 0 && also_read != NULL) {
		read[count] = 0;
		also_read(read);
	}
	for (i = 0; i < count && waiting_count < WAITING_MAX; i++)
		waiting[waiting_count++] = read[i];
	// what the radio is itself sending, text, macros and what a client
	// keys, goes where what is read goes, among it: the author asked to
	// see what is sent in the window that shows what is read
	count = cw_text_take_sent(read, sizeof(read) - 1);
	if (count > 0)
		read[count] = 0;
	if (count > 0 && also_read != NULL)
		also_read(read);
	if (count > 0 && also_sent != NULL)
		also_sent(read);
	for (i = 0; i < count && sent_waiting_count < WAITING_MAX; i++)
		sent_waiting[sent_waiting_count++] = read[i];
	if (sent_waiting_count > 0 && !panel_is_waiting(LINK_CW_SENT)) {
		memcpy(sent_part, sent_waiting, sent_waiting_count);
		panel_set_kept_bytes(LINK_CW_SENT, sent_part, sent_waiting_count);
		sent_waiting_count = 0;
	}
	for (i = 0; i < count && waiting_count < WAITING_MAX; i++)
		waiting[waiting_count++] = read[i];
	count = cw_rx_take(read, sizeof(read));
	for (i = 0; i < count; i++) {
		line[line_count++] = read[i];
		if (line_count == LINE_MAX)
			print_the_line();
	}
	if (count > 0)
		last_read_ms = now_ms;
	if (line_count > 0 && now_ms - last_read_ms > IDLE_MS)
		print_the_line();
	count = jtty_take_chosen_text(read, sizeof(read));
	for (i = 0; i < count && waiting_count < WAITING_MAX; i++)
		waiting[waiting_count++] = read[i];
	if (waiting_count == 0 || panel_is_waiting(LINK_CW_TEXT))
		return;
	memcpy(part, waiting, waiting_count);
	panel_set_kept_bytes(LINK_CW_TEXT, part, waiting_count);
	waiting_count = 0;
}
