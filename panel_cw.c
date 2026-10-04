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
//   the Goertzel filter's, cw_rx_take(), to the panel, as the kept, counted
//   value of LINK_CW_TEXT: the characters read since the last value, no
//   more than a value holds. The panel runs
//   them through its status line from right to left, as the author asked;
//   front_panel/main.cpp says how. The next value goes when the panel has
//   taken the one before, and what was read meanwhile waits for it.
//
//   the spectrum's, cw_rx_take_spectrum(), to the terminal, a line at a
//   time: "cw spectrum: CQ CQ DE VU2ESE K", when the line is LINE_MAX long
//   or nothing more has come for IDLE_MS.
//
// For the thread that owns the controls.

#include <stdio.h>
#include <string.h>

#include "link.h"
#include "panel.h"
#include "panel_cw.h"
#include "modems/modem_cw.h"

#define LINE_MAX 60
#define IDLE_MS 3000
#define WAITING_MAX LINK_SAY_MAX

static uint8_t part[WAITING_MAX];
static char waiting[WAITING_MAX + 1];
static int waiting_count;
static char line[LINE_MAX + 1];
static int line_count;
static long last_read_ms;

static void print_the_line(void) {
	if (line_count == 0)
		return;
	line[line_count] = 0;
	printf("cw spectrum: %s\n", line);
	fflush(stdout);
	line_count = 0;
}

void panel_cw_poll(long now_ms) {
	char read[WAITING_MAX + 1];
	int count = cw_rx_take(read, sizeof(read));
	int i;

	for (i = 0; i < count && waiting_count < WAITING_MAX; i++)
		waiting[waiting_count++] = read[i];
	count = cw_rx_take_spectrum(read, sizeof(read));
	for (i = 0; i < count; i++) {
		line[line_count++] = read[i];
		if (line_count == LINE_MAX)
			print_the_line();
	}
	if (count > 0)
		last_read_ms = now_ms;
	if (line_count > 0 && now_ms - last_read_ms > IDLE_MS)
		print_the_line();
	if (waiting_count == 0 || panel_is_waiting(LINK_CW_TEXT))
		return;
	memcpy(part, waiting, waiting_count);
	panel_set_kept_bytes(LINK_CW_TEXT, part, waiting_count);
	waiting_count = 0;
}
