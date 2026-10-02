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
#define LINK_VERSION 2

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
// the radio numbers the rest of its controls from here, in the order of
// its table, and tells the panel which is which by describing them
#define LINK_FIRST_DESCRIBED 8

// one line of the spectrum: a byte for each point, left to right on the
// screen, the lowest frequency first. A byte is the strength in dB above
// the bottom of the display. The panel's graph and waterfall are 288
// pixels wide, two to a point.
#define LINK_SPECTRUM_POINTS 144
#define LINK_COUNTED_MAX 250

// a control as the radio describes it to the panel; link.c says how
#define LINK_KIND_NUMBER 0
#define LINK_KIND_CHOICE 1
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

int link_put_description(uint8_t *at, int room,
	const struct link_description *description);
int link_get_description(const uint8_t *at, int left,
	struct link_description *description);

#ifdef __cplusplus
}
#endif
