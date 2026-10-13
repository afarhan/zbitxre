// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_cat.c — what another program asks of the radio in Hamlib's words,
// answered.
//
// It needs no radio and no network. Lines such as Hamlib sends are given
// to clients/remote/cat.c, as the gateway gives them, and what it answers
// and what it would tell the radio program are compared with what they
// should be: the frequency, the mode, the VFO and the keying, asked and
// set, by letter and by long name, with the VFO named and without; what is
// refused; and that the list of what the radio can do is whole.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cat.h"

static int checks_failed;
static struct cat_radio radio;
static char answer[CAT_ANSWER_ROOM];
static char for_radio[CAT_FOR_RADIO_ROOM];

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

// one line is asked, and the answer and the line for the radio program are
// those expected
static void expect_answer(const char *asked, const char *answered, const char *told,
		const char *what) {
	int result = cat_answer(asked, &radio, answer, for_radio);

	if (result != CAT_ANSWERED || strcmp(answer, answered) || strcmp(for_radio, told))
		printf("          \"%s\" was answered \"%s\", the radio told \"%s\"\n", asked, answer,
			for_radio);
	expect(result == CAT_ANSWERED && !strcmp(answer, answered) && !strcmp(for_radio, told), what);
}

static void begin(void) {
	radio.hz = 7074000;
	snprintf(radio.mode, sizeof(radio.mode), "USB");
	radio.vfo = 'A';
	radio.is_sending = 0;
}

static int lines_in(const char *text) {
	int count = 0;

	for (; *text; text++)
		if (*text == '\n')
			count++;
	return count;
}

int main(void) {
	printf("test_cat\n");

	begin();
	expect_answer("f", "7074000\n", "", "the frequency is said");
	expect_answer("\\get_freq", "7074000\n", "", "and by its long name");
	expect_answer("f VFOA", "7074000\n", "", "and with the VFO named");
	expect_answer("F 14074000", "RPRT 0\n", "FREQ 14074000\n", "a frequency is set");
	expect_answer("f", "14074000\n", "", "and is then the frequency said");
	expect_answer("F VFOA 21074000.000000", "RPRT 0\n", "FREQ 21074000\n",
		"a frequency with a fraction and the VFO named is set");
	expect_answer("\\set_freq 7030000", "RPRT 0\n", "FREQ 7030000\n", "and by its long name");
	expect_answer("F 100", "RPRT -1\n", "", "a frequency below the radio's is refused");
	expect_answer("F 144200000", "RPRT -1\n", "", "and one above");
	expect_answer("F", "RPRT -1\n", "", "and none at all");
	expect_answer("f", "7030000\n", "", "and a refusal changes nothing");

	begin();
	expect_answer("m", "USB\n3000\n", "", "the mode is said, with a width");
	expect_answer("M CW 500", "RPRT 0\n", "MODE CW\n", "a mode is set");
	expect_answer("m", "CW\n500\n", "", "and is then the mode said");
	expect_answer("M VFOA LSB 0", "RPRT 0\n", "MODE LSB\n", "a mode is set with the VFO named");
	expect_answer("M LSB 2400", "RPRT 0\n", "", "the mode it has is not set again");
	expect_answer("M PKTUSB 3000", "RPRT 0\n", "MODE USB\n", "Hamlib's PKTUSB is the radio's USB");
	expect_answer("m", "USB\n3000\n", "", "and is said as USB");
	expect_answer("M PKTLSB 3000", "RPRT 0\n", "MODE LSB\n", "and PKTLSB its LSB");
	expect_answer("M FM 15000", "RPRT -1\n", "", "a mode the radio has not is refused");
	expect_answer("\\set_mode AM 6000", "RPRT 0\n", "MODE AM\n", "a mode is set by its long name");
	expect_answer("\\get_mode", "AM\n6000\n", "", "and said by it");
	snprintf(radio.mode, sizeof(radio.mode), "FT8");
	expect_answer("m", "PKTUSB\n3000\n", "", "the radio's own FT8 is said to be PKTUSB");
	expect_answer("M PKTUSB 3000", "RPRT 0\n", "", "and is left alone by a program asking for that");
	expect_answer("M USB 3000", "RPRT 0\n", "MODE USB\n", "but not by one asking for USB");

	begin();
	expect_answer("v", "VFOA\n", "", "the VFO is said");
	expect_answer("V VFOB", "RPRT 0\n", "", "choosing the other VFO is accepted, the radio left alone");
	expect_answer("v", "VFOA\n", "", "and the VFO said is still the radio's");
	expect_answer("V currVFO", "RPRT 0\n", "", "\"the VFO in use\" is accepted too");
	expect_answer("V MEM", "RPRT -1\n", "", "what is not a VFO is refused");
	radio.vfo = 'B';
	expect_answer("v", "VFOB\n", "", "the radio's other VFO is said when it is in use");
	expect_answer("s", "0\nVFOB\n", "", "split is said to be off");
	expect_answer("S 0 VFOA", "RPRT 0\n", "", "turning it off is accepted");
	expect_answer("S 1 VFOB", "RPRT -11\n", "", "and turning it on is refused");

	begin();
	expect_answer("t", "0\n", "", "the radio is said not to be sending");
	expect_answer("T 1", "RPRT 0\n", "TX ON\n", "it is keyed");
	expect_answer("t", "1\n", "", "and is then said to be sending");
	expect_answer("T 0", "RPRT 0\n", "TX OFF\n", "it is let go");
	expect_answer("T VFOA 3", "RPRT 0\n", "TX ON\n", "it is keyed for data, with the VFO named");
	expect_answer("\\set_ptt 0", "RPRT 0\n", "TX OFF\n", "and let go by the long name");
	expect_answer("\\get_ptt", "0\n", "", "and said so by it");
	expect_answer("T 7", "RPRT -1\n", "", "a keying that is neither is refused");
	expect_answer("T on", "RPRT -1\n", "", "and one in a word");

	begin();
	expect_answer("\\chk_vfo", "0\n", "", "the VFO need not be named in each command");
	expect_answer("\\get_powerstat", "1\n", "", "the radio is said to be on");
	expect_answer("_", "zbitxre\n", "", "the radio says what it is");
	expect_answer("l STRENGTH", "RPRT -11\n", "", "what is not done is said not to be");
	expect_answer("\\no_such_thing", "RPRT -11\n", "", "and a long name not known");
	expect_answer("d", "RPRT -11\n", "", "and a letter that is none of Hamlib's");
	expect_answer("", "", "", "an empty line is given no answer");
	cat_answer("\\dump_state", &radio, answer, for_radio);
	expect(lines_in(answer) == 25 && !strncmp(answer, "0\n2\n", 4) && for_radio[0] == 0,
		"what the radio can do is said, in 25 lines");
	expect(strlen(answer) + 1 < CAT_ANSWER_ROOM, "and fits its room");
	expect(cat_answer("q", &radio, answer, for_radio) == CAT_QUIT && answer[0] == 0,
		"a program that says it is leaving is let go");
	expect(cat_answer("\\quit", &radio, answer, for_radio) == CAT_QUIT, "by the long name too");

	if (checks_failed) {
		printf("test_cat: %d FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("test_cat: all passed\n");
	return EXIT_SUCCESS;
}
