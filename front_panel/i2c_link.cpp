// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// i2c_link.cpp — the panel's end of the link to the radio: an I2C slave.
//
// What is said over the link is in link.c. This file only carries it. The
// panel is a slave at LINK_ADDRESS on the Pico's second I2C port, pins 6 and
// 7, which go to the two pins of the radio's bus that the Si5351 is on.
//
// A slave speaks only when it is spoken to, and the Pico is spoken to in an
// interrupt.
//
//   When the radio reads, the interrupt hands over an answer that was made
//   ready beforehand, and does nothing else. The answer is never more than
//   16 bytes, which is what the Pico's chip holds, so the interrupt never
//   waits for room.
//
//   When the radio writes, the interrupt takes the frame there and then and
//   makes the next answer ready. It has to be done in the interrupt. The
//   radio polls every 20 ms when the knob is being turned, and loop() can
//   take longer than that to come round, what with drawing the screen and
//   reading the touch screen. Left to loop(), the panel had not yet heard
//   that its last frame was taken when the radio asked again, and it sent
//   the frame a second time: two frames in three were repeats.
//
//   What the radio said is only noted in the interrupt. It is shown on the
//   screen from loop(), by i2c_link_slice(), because the screen is not for
//   an interrupt to draw on. A line of the spectrum is handed to the
//   waterfall, which keeps it until loop() comes to paint it.
//
// loop() and the interrupt both use the link's book-keeping, so loop() holds
// interrupts off while it does. That is for the time it takes to offer a
// value and make an answer of 16 bytes.

#include <Arduino.h>
#include <Wire.h>
#include "link.h"
#include "waterfall.h"
#include "ui.h"

#define LINK_SDA 6
#define LINK_SCL 7
#define SAID_MAX 16

struct said {
	int control;
	int32_t value;
};

static struct link_end panel_end;

static uint8_t ready[LINK_ANSWER_MAX];
static volatile int ready_count;

static struct said said_list[SAID_MAX];
static volatile int said_count;

static volatile unsigned long times_read;
static volatile unsigned long times_written;
static volatile unsigned long said_and_lost;

static void make_the_answer_ready(){
	ready_count = link_make_answer(&panel_end, ready);
}

static void on_took(int control, int32_t value){
	if (said_count >= SAID_MAX){
		said_and_lost = said_and_lost + 1;
		return;
	}
	said_list[said_count].control = control;
	said_list[said_count].value = value;
	said_count = said_count + 1;
}

static void on_took_bytes(int control, const uint8_t *bytes, int count){
	if (control == LINK_SPECTRUM && count == WATERFALL_POINTS)
		waterfall_update(bytes);
}

static void on_request(){
	times_read = times_read + 1;
	Wire1.write(ready, ready_count);
}

static void on_receive(int count){
	uint8_t frame[LINK_FRAME_MAX];
	int kept = 0;
	int b;

	times_written = times_written + 1;
	while (count > 0){
		b = Wire1.read();
		if (kept < LINK_FRAME_MAX)
			frame[kept++] = (uint8_t)b;
		count--;
	}
	link_take_frame(&panel_end, frame, kept, on_took, on_took_bytes);
	make_the_answer_ready();
}

void i2c_link_init(){
	link_start(&panel_end, LINK_ANSWER_MAX - 1);
	make_the_answer_ready();

	Wire1.setSDA(LINK_SDA);
	Wire1.setSCL(LINK_SCL);
	Wire1.begin(LINK_ADDRESS);
	Wire1.onReceive(on_receive);
	Wire1.onRequest(on_request);
}

void i2c_link_changed(int control, int32_t value){
	noInterrupts();
	link_offer(&panel_end, control, value);
	make_the_answer_ready();
	interrupts();
}

void i2c_link_slice(){
	struct said said[SAID_MAX];
	int count;
	int i;

	if (said_count == 0)
		return;
	noInterrupts();
	count = said_count;
	memcpy(said, said_list, count * sizeof(struct said));
	said_count = 0;
	interrupts();

	for (i = 0; i < count; i++)
		main_radio_says(said[i].control, said[i].value);
}

void i2c_link_report(){
	Serial.printf("link: read %lu times, written to %lu times\n",
		times_read, times_written);
	Serial.printf("link: frames taken %lu, repeated %lu, spoiled %lu, "
		"said and lost %lu\n", panel_end.frames_taken,
		panel_end.frames_repeated, panel_end.frames_spoiled, said_and_lost);
}
