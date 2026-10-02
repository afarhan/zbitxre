// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// link.c — what the radio and its front panel say to each other.
//
// WHY IT IS AS SMALL AS IT IS
//
// The panel sits against the receiver, and whatever passes between the panel
// and the radio is heard in the receiver as a hash. The earlier I2C link sent
// some 280 bytes on every poll, changed or not. This one says as little as
// it can: four bytes on the bus when there is nothing to say, nine to bring
// a new frequency.
//
// This file knows nothing of I2C. It makes frames and takes them. The panel's
// i2c_link.cpp and the radio's hw/panel.c carry them.
//
// A FRAME
//
//     numbers    one byte. The high four bits are the number of this frame,
//                the low four the number of the last frame taken from the
//                other side.
//     records    none or more
//     check      one byte, the CRC-8 of all the bytes before it
//
// The radio is the master of the bus. What it writes is a frame as it is;
// the stop says where the frame ends. What it reads is a frame with one byte
// before it, the count of the bytes that follow, because a master has to
// know how much to read. That is an answer.
//
// The panel answers from an interrupt, and its chip holds 16 bytes ready to
// go. So an answer is never more than 16 bytes, and the interrupt never
// waits.
//
// A RECORD
//
// A record is one control and its value.
//
//     first      the high two bits are the size of the value: 0 is one byte,
//                1 is two, 2 is four, 3 is counted. The low six bits are the
//                control.
//     count      only if counted: how many bytes of value follow
//     value      low byte first
//
// The size is in the record, so a control that is not known can be stepped
// over.
//
// A value goes in as few bytes as hold it: one for 0 to 255, two for what
// a signed 16 bits hold, four for anything else. Whoever takes it is told
// the size by the record.
//
// A counted value is for what does not fit in four bytes. The spectrum is
// one: a line of it is 144 bytes. A counted value is said once and not
// again. If its frame is lost, it is lost, and that is right for a
// spectrum: by the time it could be sent again there is a newer one. The
// bytes offered are not copied. They must stay as they are until the frame
// has been made, which is at the next poll.
//
// A counted value that is kept, link_offer_kept_bytes(), is said until the
// other side has taken it, as a plain value is. Its bytes must stay as they
// are until then; link_is_waiting() says when it has gone.
//
// THE RADIO DESCRIBES ITS CONTROLS
//
// The panel is not built knowing what controls the radio has. A modem
// brings its own, and one written next year will bring more. So the radio
// tells the panel: each control's number on this link, its name, the group
// it belongs to, whether it is a number or a choice, and its least value,
// greatest and step, or its choices with a / between them. The panel puts
// the ones it has a place for in their places and lists the rest.
//
// FREQ and VOLUME keep the numbers 1 and 2 that they always had. The rest
// are numbered from LINK_FIRST_DESCRIBED in the order of the radio's table.
// A choice's value on the link is which of its choices it is, counted from
// 0.
//
// The descriptions go as the kept, counted value of LINK_DESCRIBE, as many
// to a frame as fit, one after another:
//
//     number     one byte; LINK_NO_MORE_CONTROLS, and nothing after it,
//                when every control has been described
//     kind       one byte
//     least, greatest, step     four bytes each, low byte first
//     name, group, choices      each ended by a zero byte
//
// The radio describes its controls when the panel says HELLO, and says
// their values only when the last description has been taken, since a value
// means nothing to a panel that has not been told whose it is.
//
// NUMBERS
//
// A side numbers the frames it makes that have records in them, 1 to 15 and
// round again. 0 is never a frame's number; as the number taken it means
// that nothing has been taken yet. A frame with no records carries the
// number of the last frame that had some.
//
// A frame whose number has been taken already is a repeat, and its records
// are thrown away. So a frame can be sent again without harm.
//
// A control that is offered waits until the other side says it took the
// frame the control went in. If it is offered again meanwhile, it goes out
// afresh, in a frame with a new number, and everything else still waiting
// goes with it. So only the newest value of a control is ever sent: ten
// steps of the knob between two polls are one record.
//
// A frame whose check is wrong is dropped whole. Nothing says it was taken,
// so it comes again.
//
// HELLO
//
// A side offers HELLO when it starts, with the version of the protocol as
// its value, and HELLO goes first in its frame. A frame that begins with
// HELLO is always taken, whatever its number, since the side that sent it
// has forgotten what numbers it used before.
//
// The radio is the one that remembers the settings. When it takes HELLO from
// the panel it offers every control again.
//
// THE CHECK
//
// CRC-8 with the polynomial x^8 + x^2 + x + 1, starting from 0. It is the
// check SMBus uses.

#include <stddef.h>
#include <string.h>

#include "link.h"

#define CRC_POLYNOMIAL 0x07
#define CRC_TOP_BIT 0x80
#define BITS_IN_A_BYTE 8

#define NUMBER_SHIFT 4
#define NUMBER_MASK 0x0f
#define HIGHEST_NUMBER 15

#define SIZE_SHIFT 6
#define CONTROL_MASK 0x3f
#define SIZE_ONE 0
#define SIZE_TWO 1
#define SIZE_FOUR 2
#define SIZE_COUNTED 3

#define COUNT_BYTES 1
#define NUMBERS_BYTES 1
#define CHECK_BYTES 1

uint8_t link_crc(const uint8_t *bytes, int count) {
	uint8_t crc = 0;
	int i;
	int bit;

	for (i = 0; i < count; i++) {
		crc ^= bytes[i];
		for (bit = 0; bit < BITS_IN_A_BYTE; bit++) {
			if (crc & CRC_TOP_BIT)
				crc = (uint8_t)((crc << 1) ^ CRC_POLYNOMIAL);
			else
				crc = (uint8_t)(crc << 1);
		}
	}
	return crc;
}

#define ONE_BYTE_MAX 255
#define TWO_BYTES_MIN (-32768)
#define TWO_BYTES_MAX 32767

static uint8_t bytes_for(int32_t value) {
	if (value >= 0 && value <= ONE_BYTE_MAX)
		return 1;
	if (value >= TWO_BYTES_MIN && value <= TWO_BYTES_MAX)
		return 2;
	return 4;
}

static uint8_t size_code_of(uint8_t bytes) {
	if (bytes == 4)
		return SIZE_FOUR;
	if (bytes == 2)
		return SIZE_TWO;
	return SIZE_ONE;
}

static int bytes_of_size_code(int code) {
	if (code == SIZE_FOUR)
		return 4;
	if (code == SIZE_TWO)
		return 2;
	return 1;
}

void link_start(struct link_end *end, int room) {
	memset(end, 0, sizeof(*end));
	end->room = room;
	link_offer(end, LINK_HELLO, LINK_VERSION);
}

void link_offer(struct link_end *end, int control, int32_t value) {
	struct link_control *c;

	if (control < 0 || control >= LINK_CONTROLS)
		return;
	c = &end->control[control];
	c->value = value;
	c->counted = NULL;
	c->bytes = bytes_for(value);
	c->is_waiting = 1;
	c->went_under = 0;
	c->is_kept = 0;
}

void link_offer_bytes(struct link_end *end, int control, const uint8_t *bytes,
		int count) {
	struct link_control *c;

	if (control < 0 || control >= LINK_CONTROLS)
		return;
	if (bytes == NULL || count < 0 || count > LINK_COUNTED_MAX)
		return;
	c = &end->control[control];
	c->counted = bytes;
	c->bytes = (uint8_t)count;
	c->is_waiting = 1;
	c->went_under = 0;
	c->is_kept = 0;
}

void link_offer_kept_bytes(struct link_end *end, int control,
		const uint8_t *bytes, int count) {
	if (control < 0 || control >= LINK_CONTROLS)
		return;
	if (bytes == NULL || count < 0 || count > LINK_COUNTED_MAX)
		return;
	link_offer_bytes(end, control, bytes, count);
	end->control[control].is_kept = 1;
}

int link_is_waiting(const struct link_end *end, int control) {
	if (control < 0 || control >= LINK_CONTROLS)
		return 0;
	return end->control[control].is_waiting;
}

int link_has_something_to_say(const struct link_end *end) {
	int i;

	if (end->owes_a_word)
		return 1;
	for (i = 0; i < LINK_CONTROLS; i++) {
		if (end->control[i].is_waiting)
			return 1;
	}
	return 0;
}

static int something_new_is_waiting(const struct link_end *end) {
	int i;

	for (i = 0; i < LINK_CONTROLS; i++) {
		if (end->control[i].is_waiting && end->control[i].went_under == 0)
			return 1;
	}
	return 0;
}

static uint8_t number_after(uint8_t number) {
	if (number >= HIGHEST_NUMBER)
		return 1;
	return number + 1;
}

static int length_to_put(const struct link_control *c) {
	if (c->counted != NULL)
		return 2 + c->bytes;
	return 1 + c->bytes;
}

static int put_record(uint8_t *at, int control, const struct link_control *c) {
	uint32_t value = (uint32_t)c->value;
	int i;

	if (c->counted != NULL) {
		at[0] = (uint8_t)((SIZE_COUNTED << SIZE_SHIFT) | control);
		at[1] = c->bytes;
		memcpy(at + 2, c->counted, c->bytes);
		return 2 + c->bytes;
	}
	at[0] = (uint8_t)((size_code_of(c->bytes) << SIZE_SHIFT) | control);
	for (i = 0; i < c->bytes; i++)
		at[1 + i] = (uint8_t)(value >> (BITS_IN_A_BYTE * i));
	return 1 + c->bytes;
}

static int goes_in_this_frame(const struct link_control *c, uint8_t number,
		int is_new_frame) {
	if (!c->is_waiting)
		return 0;
	if (is_new_frame)
		return 1;
	return c->went_under == number;
}

int link_make_frame(struct link_end *end, uint8_t *frame) {
	struct link_control *c;
	int is_new_frame;
	int used;
	int i;

	is_new_frame = something_new_is_waiting(end);
	if (is_new_frame)
		end->number = number_after(end->number);

	used = NUMBERS_BYTES;
	for (i = 0; i < LINK_CONTROLS; i++) {
		c = &end->control[i];
		if (!goes_in_this_frame(c, end->number, is_new_frame))
			continue;
		if (used + length_to_put(c) + CHECK_BYTES > end->room)
			continue;
		used += put_record(frame + used, i, c);
		c->went_under = end->number;
		if (c->counted != NULL && !c->is_kept)
			c->is_waiting = 0;
	}

	frame[0] = (uint8_t)((end->number << NUMBER_SHIFT) | end->taken);
	frame[used] = link_crc(frame, used);
	end->owes_a_word = 0;
	end->frames_made++;
	return used + CHECK_BYTES;
}

int link_make_answer(struct link_end *end, uint8_t *answer) {
	int count;

	count = link_make_frame(end, answer + COUNT_BYTES);
	answer[0] = (uint8_t)count;
	return COUNT_BYTES + count;
}

static void the_other_side_took(struct link_end *end, uint8_t number) {
	struct link_control *c;
	int i;

	if (number == 0 || number != end->number)
		return;
	for (i = 0; i < LINK_CONTROLS; i++) {
		c = &end->control[i];
		if (c->is_waiting && c->went_under == number)
			c->is_waiting = 0;
	}
}

static int32_t value_at(const uint8_t *at, int bytes) {
	uint32_t value = 0;
	int i;

	for (i = 0; i < bytes; i++)
		value |= (uint32_t)at[i] << (BITS_IN_A_BYTE * i);
	if (bytes == 2)
		return (int16_t)value;
	return (int32_t)value;
}

static int length_of_record(const uint8_t *at, int left) {
	int code = at[0] >> SIZE_SHIFT;

	if (code != SIZE_COUNTED)
		return 1 + bytes_of_size_code(code);
	if (left < 2)
		return left + 1;
	return 2 + at[1];
}

static int records_are_whole(const uint8_t *records, int count) {
	int at = 0;

	while (at < count)
		at += length_of_record(records + at, count - at);
	return at == count;
}

static void take_records(const uint8_t *records, int count, link_took_fn took,
		link_took_bytes_fn took_bytes) {
	int at = 0;
	int length;
	int control;
	int is_counted;

	while (at < count) {
		length = length_of_record(records + at, count - at);
		control = records[at] & CONTROL_MASK;
		is_counted = (records[at] >> SIZE_SHIFT) == SIZE_COUNTED;
		if (is_counted && took_bytes != NULL)
			took_bytes(control, records + at + 2, length - 2);
		if (!is_counted && took != NULL)
			took(control, value_at(records + at + 1, length - 1));
		at += length;
	}
}

int link_take_frame(struct link_end *end, const uint8_t *frame, int count,
		link_took_fn took, link_took_bytes_fn took_bytes) {
	const uint8_t *records = frame + NUMBERS_BYTES;
	int records_count = count - NUMBERS_BYTES - CHECK_BYTES;
	uint8_t number;
	int begins_with_hello;

	if (count < LINK_FRAME_MIN || count > LINK_FRAME_MAX
			|| link_crc(frame, count - CHECK_BYTES) != frame[count - CHECK_BYTES]
			|| !records_are_whole(records, records_count)) {
		end->frames_spoiled++;
		return LINK_SPOILED;
	}

	number = frame[0] >> NUMBER_SHIFT;
	the_other_side_took(end, frame[0] & NUMBER_MASK);
	if (records_count == 0 || number == 0)
		return LINK_NOTHING_NEW;

	begins_with_hello = (records[0] & CONTROL_MASK) == LINK_HELLO;
	if (number == end->taken && !begins_with_hello) {
		end->frames_repeated++;
		end->owes_a_word = 1;
		return LINK_NOTHING_NEW;
	}

	end->taken = number;
	end->owes_a_word = 1;
	end->frames_taken++;
	take_records(records, records_count, took, took_bytes);
	return LINK_TOOK_RECORDS;
}

#define DESCRIPTION_NUMBERS 3
#define DESCRIPTION_FIXED_BYTES (2 + DESCRIPTION_NUMBERS * 4)

static void put_four(uint8_t *at, int32_t value) {
	int i;

	for (i = 0; i < 4; i++)
		at[i] = (uint8_t)((uint32_t)value >> (BITS_IN_A_BYTE * i));
}

// puts a description at "at" if there is room for it, and says how many
// bytes it took, or 0 if there is no room
int link_put_description(uint8_t *at, int room,
		const struct link_description *d) {
	int name = (int)strlen(d->name) + 1;
	int group = (int)strlen(d->group) + 1;
	int choices = (int)strlen(d->choices) + 1;
	int used = DESCRIPTION_FIXED_BYTES;

	if (d->number == LINK_NO_MORE_CONTROLS) {
		if (room < 1)
			return 0;
		at[0] = LINK_NO_MORE_CONTROLS;
		return 1;
	}
	if (room < used + name + group + choices)
		return 0;
	at[0] = d->number;
	at[1] = d->kind;
	put_four(at + 2, d->min);
	put_four(at + 6, d->max);
	put_four(at + 10, d->step);
	memcpy(at + used, d->name, name);
	used += name;
	memcpy(at + used, d->group, group);
	used += group;
	memcpy(at + used, d->choices, choices);
	return used + choices;
}

// copies the text that begins at "at" and says how many bytes it took with
// its zero, or -1 if it has no zero within "left" or is too long to keep
static int get_text(const uint8_t *at, int left, char *text, int room) {
	int length = 0;

	while (length < left && at[length] != 0)
		length++;
	if (length == left || length >= room)
		return -1;
	memcpy(text, at, length + 1);
	return length + 1;
}

// takes the description at "at" and says how many bytes it was, or -1 if
// it is not a whole one
int link_get_description(const uint8_t *at, int left,
		struct link_description *d) {
	int used = DESCRIPTION_FIXED_BYTES;
	int taken;

	memset(d, 0, sizeof(*d));
	if (left < 1)
		return -1;
	d->number = at[0];
	if (d->number == LINK_NO_MORE_CONTROLS)
		return 1;
	if (left < used)
		return -1;
	d->kind = at[1];
	d->min = value_at(at + 2, 4);
	d->max = value_at(at + 6, 4);
	d->step = value_at(at + 10, 4);
	taken = get_text(at + used, left - used, d->name, sizeof(d->name));
	if (taken < 0)
		return -1;
	used += taken;
	taken = get_text(at + used, left - used, d->group, sizeof(d->group));
	if (taken < 0)
		return -1;
	used += taken;
	taken = get_text(at + used, left - used, d->choices, sizeof(d->choices));
	if (taken < 0)
		return -1;
	return used + taken;
}
