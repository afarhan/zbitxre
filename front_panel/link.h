// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// link.h — what the radio and its front panel say to each other. See link.c.
//
// This file and link.c are used by both: the panel's firmware, which is in
// this folder, and the radio, which takes them from here.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_ADDRESS 0x0a
#define LINK_VERSION 3

// the most the panel answers with, its count byte included, and the most
// the radio writes
#define LINK_ANSWER_MAX 16
#define LINK_FRAME_MAX 255

// a frame is at least its numbers and its check
#define LINK_FRAME_MIN 2

#define LINK_CONTROLS 64

#define LINK_HELLO 0
#define LINK_FREQ 1
#define LINK_VOLUME 2
#define LINK_SPECTRUM 3
#define LINK_DESCRIBE 4
// a line of text for the panel's status bar, as a kept, counted value: no
// more than LINK_SAY_MAX characters, with no zero at its end
#define LINK_SAY 5
#define LINK_SAY_MAX 48
// a part of the new text of a control, from the panel; link.c says how a
// text is sent in parts
#define LINK_TEXT_PART 6
#define LINK_TEXT_PART_HEADER 3
#define LINK_TEXT_PART_DATA 8
#define LINK_TEXT_MAX 100
// the access points the radio can hear, for the panel's WiFi screen: a part
// of the list, as a kept, counted value; link.c says what is in a part
#define LINK_WIFI_LIST 7
#define LINK_WIFI_PART_HEADER 2
#define LINK_WIFI_NAME_MAX 32
#define LINK_WIFI_POINTS_MAX 16
// the access point the operator chose from the list, and its key: a text
// from the panel, in parts as any text is, with this for its control: the
// name, LINK_WIFI_BETWEEN, and the key, which is empty for an open one
#define LINK_WIFI_CHOSEN LINK_WIFI_LIST
#define LINK_WIFI_BETWEEN '\t'
#define LINK_WIFI_KEY_MIN 8
#define LINK_WIFI_KEY_MAX 63

// the newest entries in the logbook, for the panel's LOG screen: a part of
// the list, as a kept, counted value; link.c says what is in a part
#define LINK_LOG_LIST 8
#define LINK_LOG_PART_HEADER 2
#define LINK_LOG_LINE_MAX 63
#define LINK_LOG_LINES_MAX 100
// an entry to be deleted from the logbook: a text from the panel, in parts
// as any text is, with this for its control: the entry's number in the
// logbook, LINK_LOG_BETWEEN, and what the list is being searched for, so
// that the list sent back after it is of the same search
#define LINK_LOG_DELETE 9
#define LINK_LOG_BETWEEN '\t'
// the messages the radio has copied off the air, FT8's, for the panel's
// list of them: a kept, counted value, as many lines as fit; link.c says
// what is in it
#define LINK_MESSAGES 10
#define LINK_MESSAGE_LINE_MAX 47
// a message the operator chose to answer: a text from the panel, in parts
// as any text is, with this for its control; panel_messages.c on the radio
// says what is in it
#define LINK_FT8_ANSWER 11
// the contact under way, for the panel's status line, as a kept, counted
// value as LINK_SAY is: the other station and what has passed between us,
// "VU2XZ > -19 > R-14 > RR73"; empty when there is no contact
#define LINK_QSO 12
// the time by the radio's clock, as the seconds of the day in UTC, sent
// each second; LINK_UTC_NOT_SET when the radio does not know the time
#define LINK_UTC 13
#define LINK_UTC_NOT_SET (-1)

struct link_wifi_point {
	int8_t signal_dbm;
	uint8_t is_secured;
	char name[LINK_WIFI_NAME_MAX + 1];
};
// the radio numbers the rest of its controls from here, in the order of
// its table, and tells the panel which is which by describing them
#define LINK_FIRST_DESCRIBED 16

// one line of the spectrum: a byte for each point, left to right on the
// screen, the lowest frequency first. A byte is the strength in dB above
// the bottom of the display. The panel's graph and waterfall are 288
// pixels wide, two to a point.
#define LINK_SPECTRUM_POINTS 144
#define LINK_COUNTED_MAX 250

// a control as the radio describes it to the panel; link.c says how
#define LINK_KIND_NUMBER 0
#define LINK_KIND_CHOICE 1
#define LINK_KIND_ACTION 2
#define LINK_KIND_TEXT 3
#define LINK_NAME_MAX 16
#define LINK_GROUP_MAX 12
#define LINK_CHOICES_MAX 64
#define LINK_NO_MORE_CONTROLS 0

struct link_description {
	uint8_t number;
	uint8_t kind;
	int32_t min;
	int32_t max;
	int32_t step;
	char name[LINK_NAME_MAX];
	char group[LINK_GROUP_MAX];
	char choices[LINK_CHOICES_MAX];
};

#define LINK_NOTHING_NEW 0
#define LINK_TOOK_RECORDS 1
#define LINK_SPOILED (-1)

typedef void (*link_took_fn)(int control, int32_t value);
typedef void (*link_took_bytes_fn)(int control, const uint8_t *bytes, int count);

struct link_control {
	int32_t value;
	const uint8_t *counted;
	uint8_t bytes;
	uint8_t is_waiting;
	uint8_t went_under;
	uint8_t is_kept;
};

struct link_end {
	int room;
	uint8_t number;
	uint8_t taken;
	uint8_t owes_a_word;
	unsigned long frames_made;
	unsigned long frames_taken;
	unsigned long frames_repeated;
	unsigned long frames_spoiled;
	struct link_control control[LINK_CONTROLS];
};

uint8_t link_crc(const uint8_t *bytes, int count);

void link_start(struct link_end *end, int room);
void link_offer(struct link_end *end, int control, int32_t value);
void link_offer_bytes(struct link_end *end, int control, const uint8_t *bytes,
	int count);
void link_offer_kept_bytes(struct link_end *end, int control,
	const uint8_t *bytes, int count);
int link_is_waiting(const struct link_end *end, int control);
int link_has_something_to_say(const struct link_end *end);
int link_make_frame(struct link_end *end, uint8_t *frame);
int link_make_answer(struct link_end *end, uint8_t *answer);
int link_take_frame(struct link_end *end, const uint8_t *frame, int count,
	link_took_fn took, link_took_bytes_fn took_bytes);

int link_put_text_part(uint8_t *at, int control, const char *text, int offset);
int link_get_text_part(const uint8_t *bytes, int count, int *control,
	char *text, int room);

int link_put_wifi_point(uint8_t *at, int room, const struct link_wifi_point *point);
int link_get_wifi_point(const uint8_t *at, int left, struct link_wifi_point *point);

int link_put_description(uint8_t *at, int room,
	const struct link_description *description);
int link_get_description(const uint8_t *at, int left,
	struct link_description *description);

#ifdef __cplusplus
}
#endif
