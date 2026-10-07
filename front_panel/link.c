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
//                control, or 63 when the control is 63 or more, and then
//     control    the control, a byte of its own. The radio has more controls
//                than six bits hold; those that always had their numbers,
//                and the first of the rest, go as they always went
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
// THE ACCESS POINTS
//
// The radio lists the WiFi access points it can hear when the panel asks,
// the strongest first, no more than LINK_WIFI_POINTS_MAX of them. The list
// goes as the kept, counted value of LINK_WIFI_LIST, in as many parts as
// it needs, each sent when the one before has been taken:
//
//     first      one byte, which place in the list this part's first
//                access point has, counted from 0
//     last       one byte, 1 if the list ends with this part, or else 0
//     and for each access point in the part:
//     signal     one byte, its strength in dBm, a number below zero
//     secured    one byte, 1 if it wants a key, or else 0
//     name       its name, ended by a zero byte
//
// A list with nothing in it is one part, marked last, with no access
// points.
//
// The access point the operator chooses comes back from the panel as a
// text, in parts as a text control's does, with LINK_WIFI_CHOSEN where the
// control's number would be: its name, a tab, and its key.
//
// THE LOGBOOK
//
// The radio lists the newest entries in the logbook when the panel asks,
// the newest first, no more than LINK_LOG_LINES_MAX of them. The list goes
// as the kept, counted value of LINK_LOG_LIST, in parts as the access
// points do:
//
//     first      one byte, which place in the list this part's first
//                entry has, counted from 0
//     last       one byte, 1 if the list ends with this part, or else 0
//     and for each entry in the part a line of text, ended by a zero
//     byte, no longer than LINK_LOG_LINE_MAX; panel_log.c on the radio says
//     what is in a line
//
// The panel asks for the list with a text whose control is LINK_LOG_LIST,
// the beginning of a callsign to search for, or nothing for every station.
// It asks for an entry to be deleted with a text whose control is
// LINK_LOG_DELETE: the entry's number, a tab, and the same search, and
// the radio sends the list of that search again when it has deleted it.
//
// THE MESSAGES
//
// What the radio copies off the air, FT8's messages so far, goes to the
// panel as it is copied, as the kept, counted value of LINK_MESSAGES: one
// line of text after another, each ended by a zero byte and no longer than
// LINK_MESSAGE_LINE_MAX, as many to a value as fit. The next value is sent
// when the panel has taken the one before. There is no first or last: the
// panel adds each line to the end of its list. panel_messages.c on the
// radio says what is in a line.
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
// are numbered from LINK_FIRST_DESCRIBED in the order of the radio's table;
// the numbers below it are for what is not a control: the spectrum, the
// lists. A control's number is six bits, so the radio can describe 48
// controls beyond those two.
// A choice's value on the link is which of its choices it is, counted from
// 0. An action is described as a choice is, with the things it can do for
// its choices; it has no value, and the panel sends which of them is to be
// done.
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
// LINK_SAY is a line of text for the panel's status bar, sent the same
// way, kept and counted.
//
// TEXT
//
// A control that holds text, the callsign, is described as the others are,
// and is given a number. The radio says its value as the kept, counted
// value of that number: the characters, with no zero after them.
//
// The panel's answer is never more than 16 bytes, so a text the panel
// sends goes in parts, each the kept, counted value of LINK_TEXT_PART:
//
//     control    one byte, the number of the control whose text this is
//     offset     one byte, where in the text this part begins
//     last       one byte, 1 if the text ends with this part, or else 0
//     the characters, no more than LINK_TEXT_PART_DATA of them
//
// The panel sends a part when the one before has been taken. The radio
// puts each where its offset says, and has the whole text when the part
// marked last has come. An empty text is one part, marked last, with no
// characters.
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
// afresh, in the next frame. So only the newest value of a control is ever
// sent: ten steps of the knob between two polls are one record.
//
// A new frame is not made until the other side has said it took the last
// one; until then the last is said again, under its number. A new frame
// carries everything still waiting, and the other side may have taken the
// last frame and not yet said so: the records in it would be taken twice.
// For a value that is harmless, but a part of a text would be taken whole
// twice, and an FT8 station was answered five times, when the panel's
// battery reading, changing at each look, kept making new frames. It
// costs no time: the radio reads the panel's frame and then writes its own,
// saying what it took, so the panel's next frame is ready by the next poll.
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
#define CONTROL_FOLLOWS 0x3f
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

// whether the frame last made has things in it the other side has not yet
// said it took
static int is_unanswered(const struct link_end *end) {
	int i;

	for (i = 0; i < LINK_CONTROLS; i++) {
		if (end->control[i].is_waiting && end->control[i].went_under != 0
				&& end->control[i].went_under == end->number)
			return 1;
	}
	return 0;
}

static uint8_t number_after(uint8_t number) {
	if (number >= HIGHEST_NUMBER)
		return 1;
	return number + 1;
}

// the record's first byte, and the control's own byte after it if it does
// not fit in the first
static int header_length(int control) {
	if (control >= CONTROL_FOLLOWS)
		return 2;
	return 1;
}

static int length_to_put(int control, const struct link_control *c) {
	if (c->counted != NULL)
		return header_length(control) + 1 + c->bytes;
	return header_length(control) + c->bytes;
}

static int put_header(uint8_t *at, int control, int size_code) {
	if (control < CONTROL_FOLLOWS) {
		at[0] = (uint8_t)((size_code << SIZE_SHIFT) | control);
		return 1;
	}
	at[0] = (uint8_t)((size_code << SIZE_SHIFT) | CONTROL_FOLLOWS);
	at[1] = (uint8_t)control;
	return 2;
}

static int put_record(uint8_t *at, int control, const struct link_control *c) {
	uint32_t value = (uint32_t)c->value;
	int used;
	int i;

	if (c->counted != NULL) {
		used = put_header(at, control, SIZE_COUNTED);
		at[used] = c->bytes;
		memcpy(at + used + 1, c->counted, c->bytes);
		return used + 1 + c->bytes;
	}
	used = put_header(at, control, size_code_of(c->bytes));
	for (i = 0; i < c->bytes; i++)
		at[used + i] = (uint8_t)(value >> (BITS_IN_A_BYTE * i));
	return used + c->bytes;
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

	is_new_frame = something_new_is_waiting(end) && !is_unanswered(end);
	if (is_new_frame)
		end->number = number_after(end->number);

	used = NUMBERS_BYTES;
	for (i = 0; i < LINK_CONTROLS; i++) {
		c = &end->control[i];
		if (!goes_in_this_frame(c, end->number, is_new_frame))
			continue;
		if (used + length_to_put(i, c) + CHECK_BYTES > end->room)
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

// how many bytes the record's first byte and its control take
static int header_taken(const uint8_t *at) {
	if ((at[0] & CONTROL_MASK) == CONTROL_FOLLOWS)
		return 2;
	return 1;
}

static int control_of_record(const uint8_t *at) {
	if ((at[0] & CONTROL_MASK) == CONTROL_FOLLOWS)
		return at[1];
	return at[0] & CONTROL_MASK;
}

// a record that runs past what is left says it is longer than that, so
// that the frame is not taken
static int length_of_record(const uint8_t *at, int left) {
	int code = at[0] >> SIZE_SHIFT;
	int header = header_taken(at);

	if (left < header + 1 && (code == SIZE_COUNTED || header > 1))
		return left + 1;
	if (code != SIZE_COUNTED)
		return header + bytes_of_size_code(code);
	return header + 1 + at[header];
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
	int header;

	while (at < count) {
		length = length_of_record(records + at, count - at);
		control = control_of_record(records + at);
		header = header_taken(records + at);
		is_counted = (records[at] >> SIZE_SHIFT) == SIZE_COUNTED;
		if (is_counted && took_bytes != NULL)
			took_bytes(control, records + at + header + 1, length - header - 1);
		if (!is_counted && took != NULL)
			took(control, value_at(records + at + header, length - header));
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

// makes the part of a text that begins at offset, and says how many bytes
// it is; the part is marked last if the text ends with it
int link_put_text_part(uint8_t *at, int control, const char *text, int offset) {
	int left = (int)strlen(text) - offset;
	int count = left;

	if (left < 0)
		return 0;
	if (count > LINK_TEXT_PART_DATA)
		count = LINK_TEXT_PART_DATA;
	at[0] = (uint8_t)control;
	at[1] = (uint8_t)offset;
	at[2] = count == left;
	memcpy(at + LINK_TEXT_PART_HEADER, text + offset, count);
	return LINK_TEXT_PART_HEADER + count;
}

// puts a part where it belongs in text, which has "room" bytes. Says 1 if
// the text is now whole, with its zero, 0 if more is to come, and -1 if the
// part is not one or does not fit
int link_get_text_part(const uint8_t *bytes, int count, int *control,
		char *text, int room) {
	int offset;
	int length = count - LINK_TEXT_PART_HEADER;

	if (count < LINK_TEXT_PART_HEADER)
		return -1;
	offset = bytes[1];
	if (offset + length >= room)
		return -1;
	*control = bytes[0];
	memcpy(text + offset, bytes + LINK_TEXT_PART_HEADER, length);
	if (!bytes[2])
		return 0;
	text[offset + length] = 0;
	return 1;
}

#define WIFI_POINT_FIXED_BYTES 2

// puts an access point at "at" if there is room, and says how many bytes
// it took, or 0 if there is no room
int link_put_wifi_point(uint8_t *at, int room, const struct link_wifi_point *point) {
	int name = (int)strlen(point->name) + 1;

	if (room < WIFI_POINT_FIXED_BYTES + name)
		return 0;
	at[0] = (uint8_t)point->signal_dbm;
	at[1] = point->is_secured;
	memcpy(at + WIFI_POINT_FIXED_BYTES, point->name, name);
	return WIFI_POINT_FIXED_BYTES + name;
}

// takes the access point at "at" and says how many bytes it was, or -1 if
// it is not a whole one
int link_get_wifi_point(const uint8_t *at, int left, struct link_wifi_point *point) {
	int taken;

	memset(point, 0, sizeof(*point));
	if (left < WIFI_POINT_FIXED_BYTES + 1)
		return -1;
	point->signal_dbm = (int8_t)at[0];
	point->is_secured = at[1];
	taken = get_text(at + WIFI_POINT_FIXED_BYTES, left - WIFI_POINT_FIXED_BYTES,
		point->name, sizeof(point->name));
	if (taken < 0)
		return -1;
	return WIFI_POINT_FIXED_BYTES + taken;
}
