// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_link.c — the link between the radio and its panel, with no radio and
// no panel.
//
// The two ends of front_panel/link.c are set talking to each other here
// through a bus that is only a pair of arrays. The bus can be told to lose
// what the panel answers, to lose what the radio writes, or to spoil a byte
// on the way, which is what a real bus does next to a transmitter.
//
// One turn of the bus is what the radio does each time it polls: it reads
// the panel's answer, and then, if it has anything to say, it writes.
//
// The checks, in order: the CRC against a value known from elsewhere; the
// two ends starting up; a change going from the panel to the radio and what
// it costs in bytes; many changes between two polls going as one; an answer
// lost; an answer spoiled; the radio's word lost; each end restarted while
// the other carries on; a value going from the radio to the panel; more
// waiting than one answer holds; a line of the spectrum; and frames made by
// hand that are wrong or hold what this version does not know.
//
// Exits 0 when every check passes.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link.h"

#define PANEL_ROOM (LINK_ANSWER_MAX - 1)
#define IDLE_ANSWER_BYTES 3
#define FREQ_ANSWER_BYTES 8
#define BARE_FRAME_BYTES 2
#define CRC_OF_DIGITS 0xF4
#define TOOK_MAX 64
#define TURNS_TO_SETTLE 8
#define KNOB_STEPS 10
#define MANY_CONTROLS 20
#define FIRST_OF_MANY 10

struct took {
	int control;
	int32_t value;
};

static struct link_end panel;
static struct link_end radio;

static struct took radio_took[TOOK_MAX];
static struct took panel_took[TOOK_MAX];
static int radio_took_count;
static int panel_took_count;

static uint8_t line_taken[LINK_COUNTED_MAX];
static int lines_taken;
static int line_control;
static int line_count;

static int lose_the_answer;
static int lose_the_word;
static int spoil_the_answer;
static int longest_answer;
static int last_answer_bytes;
static int last_word_bytes;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static void on_radio_took(int control, int32_t value) {
	if (radio_took_count < TOOK_MAX) {
		radio_took[radio_took_count].control = control;
		radio_took[radio_took_count].value = value;
	}
	radio_took_count++;
	if (control == LINK_HELLO) {
		link_offer(&radio, LINK_FREQ, 7040000);
		link_offer(&radio, LINK_VOLUME, 75);
	}
}

static void on_panel_took(int control, int32_t value) {
	if (panel_took_count < TOOK_MAX) {
		panel_took[panel_took_count].control = control;
		panel_took[panel_took_count].value = value;
	}
	panel_took_count++;
}

static void on_panel_took_bytes(int control, const uint8_t *bytes, int count) {
	lines_taken++;
	line_control = control;
	line_count = count;
	if (count <= LINK_COUNTED_MAX)
		memcpy(line_taken, bytes, count);
}

static void forget_what_was_taken(void) {
	radio_took_count = 0;
	panel_took_count = 0;
	lines_taken = 0;
}

static void one_turn(void) {
	uint8_t answer[LINK_ANSWER_MAX + LINK_FRAME_MAX];
	uint8_t word[LINK_FRAME_MAX];
	int count;

	count = link_make_answer(&panel, answer);
	last_answer_bytes = count;
	if (count > longest_answer)
		longest_answer = count;
	if (spoil_the_answer)
		answer[count / 2] ^= 0x10;
	if (!lose_the_answer)
		link_take_frame(&radio, answer + 1, answer[0], on_radio_took, NULL);

	last_word_bytes = 0;
	if (!link_has_something_to_say(&radio))
		return;
	count = link_make_frame(&radio, word);
	last_word_bytes = count;
	if (!lose_the_word)
		link_take_frame(&panel, word, count, on_panel_took, on_panel_took_bytes);
}

static void turns(int count) {
	int i;

	for (i = 0; i < count; i++)
		one_turn();
}

static void start_both(void) {
	link_start(&panel, PANEL_ROOM);
	link_start(&radio, LINK_FRAME_MAX);
	forget_what_was_taken();
	turns(TURNS_TO_SETTLE);
}

static int radio_took_only(int control, int32_t value) {
	if (radio_took_count != 1)
		return 0;
	return radio_took[0].control == control && radio_took[0].value == value;
}

static void check_the_crc(void) {
	expect(link_crc((const uint8_t *)"123456789", 9) == CRC_OF_DIGITS,
		"the CRC of the digits 1 to 9 is F4, as it is for SMBus");
}

static void check_starting(void) {
	link_start(&panel, PANEL_ROOM);
	link_start(&radio, LINK_FRAME_MAX);
	forget_what_was_taken();
	turns(TURNS_TO_SETTLE);

	expect(radio_took_count == 1 && radio_took[0].control == LINK_HELLO
		&& radio_took[0].value == LINK_VERSION,
		"the radio takes HELLO from the panel, once");
	expect(panel_took_count == 3 && panel_took[0].control == LINK_HELLO
		&& panel_took[1].control == LINK_FREQ && panel_took[1].value == 7040000
		&& panel_took[2].control == LINK_VOLUME && panel_took[2].value == 75,
		"the panel takes HELLO and the radio's frequency and volume, once");
	expect(!link_has_something_to_say(&panel) && !link_has_something_to_say(&radio),
		"after that neither has anything to say");
	expect(last_answer_bytes == IDLE_ANSWER_BYTES && last_word_bytes == 0,
		"with nothing to say the answer is 3 bytes and the radio writes nothing");
}

static void check_a_change(void) {
	start_both();
	forget_what_was_taken();
	link_offer(&panel, LINK_FREQ, 7040100);
	one_turn();
	expect(radio_took_only(LINK_FREQ, 7040100),
		"a frequency from the panel reaches the radio on the next poll");
	expect(last_answer_bytes == FREQ_ANSWER_BYTES,
		"the answer that brings a frequency is 8 bytes");
	expect(last_word_bytes == BARE_FRAME_BYTES,
		"the radio says it took it in 2 bytes");
	expect(!link_has_something_to_say(&panel),
		"the panel stops offering it");
	one_turn();
	expect(radio_took_count == 1 && last_answer_bytes == IDLE_ANSWER_BYTES
		&& last_word_bytes == 0, "and the bus goes back to 3 bytes a poll");
}

static void check_many_changes(void) {
	int i;

	start_both();
	forget_what_was_taken();
	for (i = 1; i <= KNOB_STEPS; i++)
		link_offer(&panel, LINK_FREQ, 7040000 + i * 100);
	turns(2);
	expect(radio_took_only(LINK_FREQ, 7040000 + KNOB_STEPS * 100),
		"ten steps of the knob between two polls are one record, the last");

	forget_what_was_taken();
	link_offer(&panel, LINK_FREQ, 7050000);
	lose_the_word = 1;
	one_turn();
	link_offer(&panel, LINK_FREQ, 7050100);
	lose_the_word = 0;
	turns(2);
	expect(radio_took_count == 2 && radio_took[1].value == 7050100
		&& !link_has_something_to_say(&panel),
		"a value changed before the first was taken goes out again, and is taken");
}

static void check_an_answer_lost(void) {
	start_both();
	forget_what_was_taken();
	link_offer(&panel, LINK_VOLUME, 40);
	lose_the_answer = 1;
	one_turn();
	expect(radio_took_count == 0 && link_has_something_to_say(&panel),
		"an answer lost is not taken, and the panel goes on offering");
	lose_the_answer = 0;
	turns(2);
	expect(radio_took_only(LINK_VOLUME, 40), "the next poll brings it, once");
}

static void check_an_answer_spoiled(void) {
	unsigned long spoiled_before;

	start_both();
	forget_what_was_taken();
	spoiled_before = radio.frames_spoiled;
	link_offer(&panel, LINK_FREQ, 14074000);
	spoil_the_answer = 1;
	one_turn();
	expect(radio_took_count == 0 && radio.frames_spoiled == spoiled_before + 1,
		"an answer with a byte spoiled is dropped whole and counted");
	spoil_the_answer = 0;
	turns(2);
	expect(radio_took_only(LINK_FREQ, 14074000), "it comes again, and is taken once");
}

static void check_a_word_lost(void) {
	start_both();
	forget_what_was_taken();
	link_offer(&panel, LINK_FREQ, 7030000);
	lose_the_word = 1;
	one_turn();
	expect(radio_took_count == 1 && link_has_something_to_say(&panel),
		"when the radio's word is lost the panel goes on offering");
	lose_the_word = 0;
	one_turn();
	expect(radio_took_count == 1 && radio.frames_repeated == 1,
		"the radio knows the repeat and does not take it twice");
	expect(!link_has_something_to_say(&panel),
		"and its word gets through the second time");
}

static void check_the_panel_restarting(void) {
	start_both();
	link_offer(&panel, LINK_VOLUME, 10);
	turns(2);
	link_start(&panel, PANEL_ROOM);
	radio.taken = 1;
	forget_what_was_taken();
	turns(TURNS_TO_SETTLE);
	expect(radio_took_count == 1 && radio_took[0].control == LINK_HELLO,
		"a panel that restarts is heard, though its numbers begin again");
	expect(panel_took_count == 2 && panel_took[0].control == LINK_FREQ
		&& panel_took[1].control == LINK_VOLUME,
		"and the radio gives it the frequency and the volume again");
}

static void check_the_radio_restarting(void) {
	start_both();
	link_offer(&panel, LINK_FREQ, 7100000);
	turns(2);
	link_start(&radio, LINK_FRAME_MAX);
	panel.taken = 1;
	forget_what_was_taken();
	turns(TURNS_TO_SETTLE);
	expect(panel_took_count == 1 && panel_took[0].control == LINK_HELLO,
		"a radio that restarts is heard by the panel");
	link_offer(&panel, LINK_FREQ, 7100100);
	turns(2);
	expect(radio_took_only(LINK_FREQ, 7100100),
		"and takes the panel's next change");
}

static void check_the_radio_setting(void) {
	start_both();
	forget_what_was_taken();
	link_offer(&radio, LINK_FREQ, 21074000);
	turns(2);
	expect(panel_took_count == 1 && panel_took[0].control == LINK_FREQ
		&& panel_took[0].value == 21074000 && !link_has_something_to_say(&radio),
		"a frequency from the radio reaches the panel, once, and is known taken");
}

static void check_more_than_fits(void) {
	int all_arrived;
	int seen;
	int i;
	int j;

	start_both();
	forget_what_was_taken();
	longest_answer = 0;
	for (i = 0; i < MANY_CONTROLS; i++)
		link_offer(&panel, FIRST_OF_MANY + i, i + 1);
	turns(TURNS_TO_SETTLE);

	all_arrived = 1;
	for (i = 0; i < MANY_CONTROLS; i++) {
		seen = 0;
		for (j = 0; j < radio_took_count && j < TOOK_MAX; j++) {
			if (radio_took[j].control == FIRST_OF_MANY + i
					&& radio_took[j].value == i + 1)
				seen = 1;
		}
		if (!seen)
			all_arrived = 0;
	}
	expect(longest_answer <= LINK_ANSWER_MAX,
		"no answer is longer than 16 bytes, however much is waiting");
	expect(all_arrived && !link_has_something_to_say(&panel),
		"twenty controls changed at once all arrive, over several polls");
}

static void check_a_spectrum(void) {
	static uint8_t line[LINK_SPECTRUM_POINTS];
	int i;

	for (i = 0; i < LINK_SPECTRUM_POINTS; i++)
		line[i] = (uint8_t)(i / 3);

	start_both();
	forget_what_was_taken();
	link_offer_bytes(&radio, LINK_SPECTRUM, line, sizeof(line));
	link_offer(&radio, LINK_FREQ, 7088000);
	turns(3);
	expect(lines_taken == 1 && line_control == LINK_SPECTRUM
		&& line_count == LINK_SPECTRUM_POINTS
		&& memcmp(line, line_taken, sizeof(line)) == 0,
		"a line of the spectrum reaches the panel whole, once");
	expect(panel_took_count == 1 && panel_took[0].value == 7088000,
		"a frequency in the same frame arrives with it");
	expect(last_word_bytes == 0, "and then the radio has no more to say");

	forget_what_was_taken();
	link_offer_bytes(&radio, LINK_SPECTRUM, line, sizeof(line));
	lose_the_word = 1;
	one_turn();
	lose_the_word = 0;
	turns(3);
	expect(lines_taken == 0,
		"a line that is lost is not sent again");
}

static int put_check(uint8_t *frame, int count) {
	frame[count] = link_crc(frame, count);
	return count + 1;
}

static void check_frames_made_by_hand(void) {
	uint8_t counted[] = { 0x90, 0xC5, 3, 'a', 'b', 'c', LINK_VOLUME, 33, 0 };
	uint8_t runs_past[] = { 0xA0, 0x80 | LINK_FREQ, 1, 2, 0 };
	uint8_t too_short[] = { 0x10 };
	int count;

	start_both();
	forget_what_was_taken();
	count = put_check(counted, sizeof(counted) - 1);
	expect(link_take_frame(&radio, counted, count, on_radio_took, NULL) == LINK_TOOK_RECORDS
		&& radio_took_only(LINK_VOLUME, 33),
		"a counted record is stepped over and the record after it is taken");

	forget_what_was_taken();
	count = put_check(runs_past, sizeof(runs_past) - 1);
	expect(link_take_frame(&radio, runs_past, count, on_radio_took, NULL) == LINK_SPOILED
		&& radio_took_count == 0,
		"a record that runs past the end of its frame spoils the frame");
	expect(link_take_frame(&radio, too_short, sizeof(too_short), on_radio_took,
		NULL) == LINK_SPOILED, "a frame of one byte is spoiled");
}

int main(void) {
	check_the_crc();
	check_starting();
	check_a_change();
	check_many_changes();
	check_an_answer_lost();
	check_an_answer_spoiled();
	check_a_word_lost();
	check_the_panel_restarting();
	check_the_radio_restarting();
	check_the_radio_setting();
	check_more_than_fits();
	check_a_spectrum();
	check_frames_made_by_hand();

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("link: all checks passed\n");
	return EXIT_SUCCESS;
}
