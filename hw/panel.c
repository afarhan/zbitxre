// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel.c — the radio's end of the link to the front panel.
//
// What is said over the link is in front_panel/link.c, which the panel's
// firmware is built with too. This file carries it over the I2C bus, where
// the radio is the master and the panel a slave, and it decides when.
//
// The panel cannot speak until it is spoken to, so the radio polls it: it
// reads the panel's answer, and then, if it has anything to say, it writes.
// What it has to say is a control's value, or only that it took what the
// panel sent.
//
// HOW OFTEN
//
// Every transfer on this bus is heard in the receiver, the panel and its
// cable being where they are. So the radio polls no more than it must.
//
//   While the panel has sent something in the last two seconds, every
//   20 ms. The knob is being turned, and the radio has to follow it. What
//   the radio sends does not count: the spectrum goes out all the time,
//   and it must not keep the radio polling fast.
//
//   Otherwise every 100 ms. A poll with nothing to say is four bytes on
//   the bus, so an idle link is on the bus for 4 ms in every second.
//
//   When the panel has not answered ten times running it is taken to be
//   absent, and it is looked for once a second.
//
// What the radio has to say goes out at the next poll, so a line of the
// spectrum may wait 100 ms. The bytes given to panel_set_bytes() are not
// copied and must be left alone until then.
//
// panel_set_polling(0) stops the polling altogether. It is there so that
// the hash of the link can be told from everything else, by ear.
//
// WHO CALLS
//
// One thread, the one that owns the controls and retunes the Si5351. The
// kernel holds a processor for the length of a transfer, 0.4 to 1 ms here,
// so none of this is for the audio thread.
//
// panel_open() expects the bus to be open already. The radio opens it when
// it looks for the Si5351, and the panel is on the same bus.

#include <string.h>

#include "i2c.h"
#include "link.h"
#include "panel.h"

#define BUSY_POLL_EVERY_MS 20
#define IDLE_POLL_EVERY_MS 100
#define ABSENT_POLL_EVERY_MS 1000
#define BUSY_LASTS_MS 2000
#define ABSENT_AFTER_MISSES 10

static struct link_end radio_end;
static struct panel_report counted;

static long next_poll_ms;
static long busy_until_ms;
static int misses;
static int is_polling = 1;

void panel_open(void) {
	link_start(&radio_end, LINK_FRAME_MAX);
	memset(&counted, 0, sizeof(counted));
	next_poll_ms = 0;
	busy_until_ms = 0;
	misses = 0;
}

void panel_set(int control, int32_t value) {
	link_offer(&radio_end, control, value);
}

void panel_set_bytes(int control, const uint8_t *bytes, int count) {
	link_offer_bytes(&radio_end, control, bytes, count);
}

// bytes that are said until the panel has taken them; they must be left
// alone until panel_is_waiting() says they have gone
void panel_set_kept_bytes(int control, const uint8_t *bytes, int count) {
	link_offer_kept_bytes(&radio_end, control, bytes, count);
}

int panel_is_waiting(int control) {
	return link_is_waiting(&radio_end, control);
}

void panel_set_polling(int on) {
	is_polling = on;
}

static long poll_interval(long now_ms) {
	if (misses >= ABSENT_AFTER_MISSES)
		return ABSENT_POLL_EVERY_MS;
	if (now_ms < busy_until_ms)
		return BUSY_POLL_EVERY_MS;
	return IDLE_POLL_EVERY_MS;
}

static int read_reply(link_took_fn took) {
	uint8_t answer[I2C_COUNTED_ROOM];
	int count;

	count = i2c_receive_counted(LINK_ADDRESS, answer, sizeof(answer));
	if (count < 0) {
		counted.no_answers++;
		if (misses < ABSENT_AFTER_MISSES)
			misses++;
		return LINK_SPOILED;
	}
	counted.answers++;
	misses = 0;
	return link_take_frame(&radio_end, answer + 1, answer[0], took, NULL);
}

static void send_frame(void) {
	uint8_t word[LINK_FRAME_MAX];
	int count;

	count = link_make_frame(&radio_end, word);
	counted.words++;
	if (i2c_send(LINK_ADDRESS, word, count) < 0)
		counted.words_not_taken++;
}

int panel_poll(long now_ms, link_took_fn took) {
	int took_records = 0;

	if (!is_polling || now_ms < next_poll_ms)
		return 0;

	counted.polls++;
	if (read_reply(took) == LINK_TOOK_RECORDS)
		took_records = 1;
	if (misses == 0 && link_has_something_to_say(&radio_end))
		send_frame();
	if (took_records)
		busy_until_ms = now_ms + BUSY_LASTS_MS;

	next_poll_ms = now_ms + poll_interval(now_ms);
	return took_records;
}

void panel_get_report(struct panel_report *report) {
	*report = counted;
	report->frames_taken = radio_end.frames_taken;
	report->frames_repeated = radio_end.frames_repeated;
	report->frames_spoiled = radio_end.frames_spoiled;
	report->is_present = misses < ABSENT_AFTER_MISSES;
	report->is_polling = is_polling;
}
