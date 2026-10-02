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
//   waterfall, which keeps it until loop() comes to paint it. The radio's
//   descriptions of its controls are handed to controls.cpp, which stages
//   them until the last has come.
//
// loop() and the interrupt both use the link's book-keeping, so loop() holds
// interrupts off while it does. That is for the time it takes to offer a
// value and make an answer of 16 bytes.

#include <Arduino.h>
#include <Wire.h>
#include "link.h"
#include "waterfall.h"
#include "controls.h"
#include "ui.h"

#define LINK_SDA 6
#define LINK_SCL 7
//when the radio has described its controls it says every one's value, and
//they may all come in one frame
#define SAID_MAX 64

struct said {
	int control;
	int32_t value;
};

static struct link_end panel_end;

static uint8_t ready[LINK_ANSWER_MAX];
static volatile int ready_count;

static struct said said_list[SAID_MAX];
static volatile int said_count;

//the values of text controls that the radio has said, kept until loop()
//takes them
#define TEXTS_MAX 4
struct said_text {
	int number;
	char text[CONTROL_TEXT_ROOM];
};
static struct said_text texts[TEXTS_MAX];
static volatile int texts_count;

//the access points the radio listed, staged until the list is whole
static struct link_wifi_point wifi_staged[LINK_WIFI_POINTS_MAX];
static volatile int wifi_staged_count;
static volatile bool wifi_is_new;

//a text on its way to the radio, a part at a time
static char sending[LINK_TEXT_MAX + 1];
static uint8_t sending_part[LINK_TEXT_PART_HEADER + LINK_TEXT_PART_DATA];
static int sending_number;
static int sending_offset;
static bool is_sending;

//a line for the status bar, kept until loop() shows it
static char said_text[LINK_SAY_MAX + 1];
static volatile bool is_text_said;

static volatile unsigned long times_read;
static volatile unsigned long times_written;
static volatile unsigned long said_and_lost;

static void make_the_answer_ready(){
	ready_count = link_make_answer(&panel_end, ready);
}

static void on_took(int control, int32_t value){
	if (control == LINK_HELLO){
		controls_forget_staged();
		return;
	}
	if (said_count >= SAID_MAX){
		said_and_lost = said_and_lost + 1;
		return;
	}
	said_list[said_count].control = control;
	said_list[said_count].value = value;
	said_count = said_count + 1;
}

//a part of the radio's list of access points: the list is whole when the
//part marked last has come, and loop() then takes it
static void stage_wifi_points(const uint8_t *bytes, int count){
	struct link_wifi_point point;
	int at = LINK_WIFI_PART_HEADER;
	int used;

	if (count < LINK_WIFI_PART_HEADER)
		return;
	if (bytes[0] == 0)
		wifi_staged_count = 0;
	while (at < count){
		used = link_get_wifi_point(bytes + at, count - at, &point);
		if (used < 0)
			return;
		at += used;
		if (wifi_staged_count < LINK_WIFI_POINTS_MAX){
			wifi_staged[wifi_staged_count] = point;
			wifi_staged_count = wifi_staged_count + 1;
		}
	}
	if (bytes[1])
		wifi_is_new = true;
}

static void on_took_bytes(int control, const uint8_t *bytes, int count){
	if (control == LINK_SPECTRUM && count == WATERFALL_POINTS)
		waterfall_update(bytes);
	if (control == LINK_DESCRIBE)
		controls_stage(bytes, count);
	if (control >= LINK_FIRST_DESCRIBED && texts_count < TEXTS_MAX
			&& count < CONTROL_TEXT_ROOM){
		texts[texts_count].number = control;
		memcpy(texts[texts_count].text, bytes, count);
		texts[texts_count].text[count] = 0;
		texts_count = texts_count + 1;
	}
	if (control == LINK_WIFI_LIST)
		stage_wifi_points(bytes, count);
	if (control == LINK_LOG_LIST)
		screens_log_part(bytes, count);
	if (control == LINK_SAY && count <= LINK_SAY_MAX){
		memcpy(said_text, bytes, count);
		said_text[count] = 0;
		is_text_said = true;
	}
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

//a text is sent to the radio as the new value of a control; link.c says
//how it goes in parts
void i2c_link_send_text(int number, const char *text){
	snprintf(sending, sizeof(sending), "%s", text);
	sending_number = number;
	sending_offset = 0;
	is_sending = true;
}

//the next part goes when the radio has taken the one before
static void send_the_next_part(){
	int count;

	if (!is_sending)
		return;
	noInterrupts();
	if (!link_is_waiting(&panel_end, LINK_TEXT_PART)){
		count = link_put_text_part(sending_part, sending_number, sending, sending_offset);
		if (count == 0)
			is_sending = false;
		else {
			link_offer_kept_bytes(&panel_end, LINK_TEXT_PART, sending_part, count);
			make_the_answer_ready();
			sending_offset += count - LINK_TEXT_PART_HEADER;
			if (sending_part[2])
				sending_offset = LINK_TEXT_MAX + 1;
		}
	}
	interrupts();
}

void i2c_link_slice(){
	static struct said said[SAID_MAX];
	struct said_text text_said;
	char text[LINK_SAY_MAX + 1];
	int count;
	int i;

	//the controls the radio described come into use before the values it
	//said after them are shown
	if (controls_slice())
		main_controls_described();
	send_the_next_part();
	if (wifi_is_new){
		noInterrupts();
		wifi_is_new = false;
		screens_wifi_listed(wifi_staged, wifi_staged_count);
		interrupts();
	}
	while (texts_count > 0){
		noInterrupts();
		text_said = texts[0];
		texts_count = texts_count - 1;
		memmove(texts, texts + 1, texts_count * sizeof(texts[0]));
		interrupts();
		main_radio_says_text(text_said.number, text_said.text);
	}
	if (is_text_said){
		noInterrupts();
		memcpy(text, said_text, sizeof(text));
		is_text_said = false;
		interrupts();
		main_status(text);
		screens_status(text);
	}
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
