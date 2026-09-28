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
#define LINK_VERSION 1

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

#define LINK_NOTHING_NEW 0
#define LINK_TOOK_RECORDS 1
#define LINK_SPOILED (-1)

typedef void (*link_took_fn)(int control, int32_t value);

struct link_control {
	int32_t value;
	uint8_t bytes;
	uint8_t is_waiting;
	uint8_t went_under;
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
int link_has_something_to_say(const struct link_end *end);
int link_make_frame(struct link_end *end, uint8_t *frame);
int link_make_answer(struct link_end *end, uint8_t *answer);
int link_take_frame(struct link_end *end, const uint8_t *frame, int count,
	link_took_fn took);

#ifdef __cplusplus
}
#endif
