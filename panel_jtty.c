// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_jtty.c — JTTY's bins, sent to the front panel.
//
// In JTTY the panel shows the bins where the spectrum and the waterfall are
// in the other modes, as the window and the web page do; modems/modem_jtty.c
// says what a bin is. Each bin that has changed is sent as a line of text,
// the bin, counted from 0 at the bottom of the passband, the pitch of what
// was last heard in it, and the newest of its text, with a | between them:
//
//     10|1505|VU2ESE DE SBITX THE BINS ARE ON THE SCREEN NOW 73 |
//
// The whole of the bin's text goes each time, so that the panel shows a
// bin's line as it is told it. The lines go as the kept, counted value of
// LINK_JTTY_BINS, as many to a part as fit, each part when the panel has
// taken the one before; front_panel/link.c says what is in a part. A bin
// that changed while there was no room for it goes in the next part.
//
// When the panel says HELLO, having started, panel_jtty_resend() has every
// bin with something in it sent again.
//
// The chosen bin's text, for the panel's status line, and what is sent
// live, for its keyboard's box, go as CW's do; panel_cw.c says how.
//
// For the thread that owns the controls.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link.h"
#include "panel.h"
#include "panel_jtty.h"
#include "modems/modem_jtty.h"

static unsigned long told[JTTY_BINS];
static uint8_t part[LINK_COUNTED_MAX];
static int used;

// a bin's line into the part, if there is room for it
static int add_a_bin(int bin, int hz, const char *text) {
	char line[LINK_JTTY_LINE_MAX + 1];
	int length = snprintf(line, sizeof(line), "%d|%d|%s", bin, hz, text);

	if (length > LINK_JTTY_LINE_MAX)
		length = LINK_JTTY_LINE_MAX;
	if (used + length + 1 > (int)sizeof(part))
		return 0;
	memcpy(part + used, line, length);
	part[used + length] = 0;
	used += length + 1;
	return 1;
}

void panel_jtty_poll(void) {
	if (panel_is_waiting(LINK_JTTY_BINS))
		return;
	used = 0;
	jtty_poll_bins(told, add_a_bin);
	if (used > 0)
		panel_set_kept_bytes(LINK_JTTY_BINS, part, used);
}

void panel_jtty_resend(void) {
	jtty_forget_told(told);
}
