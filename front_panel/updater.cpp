// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// updater.cpp — the panel takes new firmware from the radio, over I2C.
//
// WHY
//
// The author's plan of 16 October 2026: owners put this release on their
// panel with the USB cable, as they are used to, and from then on the
// radio updates the panel itself, over the bus they already share. So this
// file has to be right in the firmware that goes out first: whatever it
// cannot do, the cable has to do again. link.h says what the radio sends.
//
// HOW
//
// The rp2040 core's Updater keeps the image in a file of the panel's own
// flash, in the filesystem the sketch is built with (1 MB, beside 1 MB for
// the sketch; the Makefile says so). When the image is whole and its MD5
// is right, the Updater leaves a command in flash and the panel restarts;
// the core's small loader, which runs before the sketch, copies the image
// over the sketch and starts it. A power cut during the copy only means it
// is begun again at the next start, the core's documentation says. A
// transfer that is cut off or wrong is thrown away, and the panel goes on
// with the firmware it has. The touch calibration lives in the EEPROM
// sector at the very end of the flash, past the filesystem, and is not
// touched.
//
// Writing flash stops the chip for milliseconds at a time, which an I2C
// interrupt must never do. So the interrupt only copies a message here and
// says BUSY; updater_slice(), from loop(), does the writing and says READY.
// The radio reads the answer after each message and sends the next only on
// READY, with the offset the panel wants. LINK_UPDATE_QUIET_MS without a
// message, and an update begun and not finished is thrown away.
//
// BOOTSEL restarts the panel as the USB drive the rp2040 has in its ROM,
// for owners who flash it with the cable; the author asked for it among
// the radio's settings.

#include <Arduino.h>
#include <Updater.h>
#include "link.h"
#include "ui.h"
#include "updater.h"

#ifndef PANEL_FIRMWARE
#define PANEL_FIRMWARE "unknown"
#endif

static volatile int state = LINK_UPDATE_IDLE;
static volatile uint32_t wanted;     // the offset the panel wants next
static volatile uint32_t heard_ms;   // when the last update message came
static volatile bool is_answering;   // reads get the update's answer

// what the interrupt left for loop(): one message at a time
static uint8_t pending[LINK_UPDATE_MARK_SIZE + 1 + 4 + 1 + LINK_UPDATE_DATA_MAX + 2];
static volatile int pending_count;
static volatile bool is_pending;

static uint32_t size;
static int shown_percent = -1;

static uint32_t number_at(const uint8_t *at) {
	return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16
		| (uint32_t)at[3] << 24;
}

// a message is the mark, a kind, what it carries and a right CRC-16
bool updater_is_message(const uint8_t *bytes, int count) {
	if (count < LINK_UPDATE_MARK_SIZE + 3)
		return false;
	if (memcmp(bytes, LINK_UPDATE_MARK, LINK_UPDATE_MARK_SIZE))
		return false;
	return link_crc16(bytes, count - 2)
		== (uint16_t)(bytes[count - 2] | bytes[count - 1] << 8);
}

// from the interrupt: kept for loop(), unless loop() has one already
void updater_take(const uint8_t *bytes, int count) {
	heard_ms = millis();
	is_answering = true;
	if (is_pending || count > (int)sizeof(pending))
		return;
	memcpy(pending, bytes, count);
	pending_count = count;
	is_pending = true;
	if (bytes[LINK_UPDATE_MARK_SIZE] == LINK_UPDATE_DATA
			|| bytes[LINK_UPDATE_MARK_SIZE] == LINK_UPDATE_BEGIN
			|| bytes[LINK_UPDATE_MARK_SIZE] == LINK_UPDATE_END)
		state = LINK_UPDATE_BUSY;
}

bool updater_is_answering(void) {
	return is_answering;
}

int updater_answer(uint8_t *answer) {
	uint32_t at = wanted;

	answer[0] = LINK_UPDATE_ANSWER - 1;
	answer[1] = (uint8_t)LINK_UPDATE_MARK[0];
	answer[2] = (uint8_t)state;
	answer[3] = (uint8_t)at;
	answer[4] = (uint8_t)(at >> 8);
	answer[5] = (uint8_t)(at >> 16);
	answer[6] = (uint8_t)(at >> 24);
	memset(answer + 7, 0, LINK_VERSION_ROOM);
	memcpy(answer + 7, PANEL_FIRMWARE,
		strnlen(PANEL_FIRMWARE, LINK_VERSION_ROOM));
	return LINK_UPDATE_ANSWER;
}

static void show_progress(void) {
	char text[32];
	int percent = size ? (int)((uint64_t)wanted * 100 / size) : 0;

	if (percent == shown_percent)
		return;
	shown_percent = percent;
	snprintf(text, sizeof(text), "PANEL UPDATE %d%%", percent);
	main_note_large(text);
}

static void fail(void) {
	if (Update.isRunning())
		Update.end(false);
	state = LINK_UPDATE_FAILED;
	main_note_large("");
	shown_percent = -1;
}

static void begin(const uint8_t *carried, int count) {
	char md5[LINK_UPDATE_MD5_HEX + 1];

	if (count != 4 + LINK_UPDATE_MD5_HEX) {
		fail();
		return;
	}
	if (Update.isRunning())
		Update.end(false);
	size = number_at(carried);
	memcpy(md5, carried + 4, LINK_UPDATE_MD5_HEX);
	md5[LINK_UPDATE_MD5_HEX] = 0;
	if (!Update.begin(size) || !Update.setMD5(md5)) {
		fail();
		return;
	}
	wanted = 0;
	shown_percent = -1;
	show_progress();
	state = LINK_UPDATE_READY;
}

static void take_data(const uint8_t *carried, int count) {
	uint32_t offset;
	int length;

	if (!Update.isRunning() || count < 5) {
		fail();
		return;
	}
	offset = number_at(carried);
	length = carried[4];
	if (length != count - 5) {
		fail();
		return;
	}
	// a part sent again, because its answer was lost, is taken once
	if (offset + length <= wanted) {
		state = LINK_UPDATE_READY;
		return;
	}
	if (offset != wanted
			|| Update.write((uint8_t *)carried + 5, length) != (size_t)length) {
		fail();
		return;
	}
	wanted = offset + length;
	show_progress();
	state = LINK_UPDATE_READY;
}

static void finish(void) {
	if (!Update.isRunning() || wanted != size || !Update.end()) {
		fail();
		return;
	}
	state = LINK_UPDATE_DONE;
	main_note_large("PANEL RESTARTING");
}

void updater_slice(void) {
	uint8_t message[sizeof(pending)];
	int count;
	static uint32_t done_ms;

	if (state == LINK_UPDATE_DONE) {
		// time for the radio to read DONE, then the loader puts the
		// image in place
		if (done_ms == 0)
			done_ms = millis();
		if (millis() - done_ms > 1000)
			rp2040.reboot();
		return;
	}
	if (is_answering && millis() - heard_ms > LINK_UPDATE_QUIET_MS) {
		if (Update.isRunning())
			fail();
		is_answering = false;
		state = LINK_UPDATE_IDLE;
		main_note_large("");
	}
	if (!is_pending)
		return;
	noInterrupts();
	count = pending_count;
	memcpy(message, pending, count);
	is_pending = false;
	interrupts();

	count -= LINK_UPDATE_MARK_SIZE + 1 + 2;
	switch (message[LINK_UPDATE_MARK_SIZE]) {
	case LINK_UPDATE_BEGIN:
		begin(message + LINK_UPDATE_MARK_SIZE + 1, count);
		break;
	case LINK_UPDATE_DATA:
		take_data(message + LINK_UPDATE_MARK_SIZE + 1, count);
		break;
	case LINK_UPDATE_END:
		finish();
		break;
	case LINK_UPDATE_ABORT:
		fail();
		state = LINK_UPDATE_IDLE;
		break;
	case LINK_UPDATE_BOOTSEL:
		main_note_large("USB DRIVE");
		delay(200);
		rp2040.rebootToBootloader();
		break;
	default:
		break;
	}
}
