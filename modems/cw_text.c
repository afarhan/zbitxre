// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// cw_text.c — text sent as CW: characters in, the key down or up out, a
// sample at a time.
//
// A macro, or what is typed, is sent by this. The paddles' keyer is
// cw_keyer.c; modem_cw.c puts the two together, and lets a touch of the
// paddle stop the text.
//
// THE CODE
//
// morse_tx_table is zbitxv2's, from its modem_cw.c, kept as it is. Besides
// the letters, the figures and . , ? / it has zbitxv2's own signs for the
// procedure signals: [ and + are AR, ] is AS, & is BT, = is BK, and ' is
// sent as a comma. A character that is not in the table is passed over.
// Capitals are sent as small letters.
//
// THE TIMING
//
// A dot is 1.2 seconds divided by the speed in words a minute, counted in
// samples as the keyer counts it. A dash is three dots. Between the dots
// and dashes of a character is a space of one dot, between characters
// three, and a space in the text makes it seven.
//
// WHO CALLS
//
// cw_text_put() is for the thread that owns the controls: it puts
// characters into a ring. Everything else is for the audio thread, which
// takes them out. The ring has one writer and one reader, and its two
// positions are each written by one of them only, so no lock is needed
// and the audio thread never waits.
//
// cw_text_is_key_down() is called once for every sample, 96000 times a
// second, as cw_keyer_is_key_down() is.

#include <ctype.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "cw_keyer.h"
#include "cw_text.h"

#define RING_SIZE 512
#define SAMPLES_A_MINUTE_PER_WORD (CW_KEYER_RATE_HZ * 12 / 10)
#define WPM_LOWEST 5
#define WPM_HIGHEST 60
#define DASH_DOTS 3
// after a character's last mark there is the one dot's space that follows
// every mark, and these make it up to three, and for a space to seven
#define MORE_DOTS_AFTER_A_CHARACTER 2
#define MORE_DOTS_FOR_A_SPACE 4

struct morse_tx {
	char c;
	const char *code;
};

static const struct morse_tx morse_tx_table[] = {
	{'~', " "}, //dummy, a null character
	{' ', " "},
	{'a', ".-"},
	{'b', "-..."},
	{'c', "-.-."},
	{'d', "-.."},
	{'e', "."},
	{'f', "..-."},
	{'g', "--."},
	{'h', "...."},
	{'i', ".."},
	{'j', ".---"},
	{'k', "-.-"},
	{'l', ".-.."},
	{'m', "--"},
	{'n', "-."},
	{'o', "---"},
	{'p', ".--."},
	{'q', "--.-"},
	{'r', ".-."},
	{'s', "..."},
	{'t', "-"},
	{'u', "..-"},
	{'v', "...-"},
	{'w', ".--"},
	{'x', "-..-"},
	{'y', "-.--"},
	{'z', "--.."},
	{'1', ".----"},
	{'2', "..---"},
	{'3', "...--"},
	{'4', "....-"},
	{'5', "....."},
	{'6', "-...."},
	{'7', "--..."},
	{'8', "---.."},
	{'9', "----."},
	{'0', "-----"},
	{'.', ".-.-.-"},
	{',', "--..--"},
	{'?', "..--.."},
	{'/', "-..-."},
	{' ', " "},
	{'[', ".-.-."},
	{']', ".-..."},
	{'+', ".-.-."},
	{'&', "-...-"},
	{'\'', "--..--"},
	{'=', "-...-.-"},
};

#define MORSE_TX_COUNT ((int)(sizeof(morse_tx_table) / sizeof(morse_tx_table[0])))
#define FIRST_REAL_ENTRY 1

static char ring[RING_SIZE];
static atomic_uint ring_put;
static atomic_uint ring_taken;
static atomic_int dot_samples = SAMPLES_A_MINUTE_PER_WORD / 20;
static atomic_int is_busy;

static const char *symbol_next;
static int mark_samples_left;
static int space_samples_left;

static const char *code_of(char c) {
	int i;

	c = (char)tolower((unsigned char)c);
	for (i = FIRST_REAL_ENTRY; i < MORSE_TX_COUNT; i++) {
		if (morse_tx_table[i].c == c)
			return morse_tx_table[i].code;
	}
	return NULL;
}

void cw_text_reset(void) {
	atomic_store(&ring_taken, atomic_load(&ring_put));
	symbol_next = NULL;
	mark_samples_left = 0;
	space_samples_left = 0;
	atomic_store(&is_busy, 0);
}

void cw_text_set_wpm(int wpm) {
	if (wpm < WPM_LOWEST)
		wpm = WPM_LOWEST;
	if (wpm > WPM_HIGHEST)
		wpm = WPM_HIGHEST;
	atomic_store(&dot_samples, SAMPLES_A_MINUTE_PER_WORD / wpm);
}

// puts text to be sent after whatever is waiting; says how many of its
// characters there was room for
int cw_text_put(const char *text) {
	unsigned int put = atomic_load(&ring_put);
	unsigned int taken = atomic_load(&ring_taken);
	int count = 0;

	while (text[count] != 0 && put - taken < RING_SIZE) {
		ring[put % RING_SIZE] = text[count];
		put++;
		count++;
	}
	atomic_store(&ring_put, put);
	if (count > 0)
		atomic_store(&is_busy, 1);
	return count;
}

// what is being sent is cut off where it is, and what is waiting is dropped
void cw_text_abort(void) {
	cw_text_reset();
}

// the next character that has a code is taken up; says whether there was one
static int take_a_character(void) {
	unsigned int taken = atomic_load(&ring_taken);
	const char *code = NULL;

	while (code == NULL && taken != atomic_load(&ring_put)) {
		code = code_of(ring[taken % RING_SIZE]);
		taken++;
	}
	atomic_store(&ring_taken, taken);
	symbol_next = code;
	return code != NULL;
}

// the next dot, dash or space of the character being sent
static void take_a_symbol(void) {
	int dot = atomic_load(&dot_samples);

	if (*symbol_next == '.')
		mark_samples_left = dot;
	else if (*symbol_next == '-')
		mark_samples_left = dot * DASH_DOTS;
	else
		space_samples_left = dot * MORE_DOTS_FOR_A_SPACE;
	if (mark_samples_left > 0)
		space_samples_left = dot;
	symbol_next++;
	if (*symbol_next == 0) {
		symbol_next = NULL;
		if (mark_samples_left > 0)
			space_samples_left += dot * MORE_DOTS_AFTER_A_CHARACTER;
	}
}

int cw_text_is_key_down(void) {
	if (mark_samples_left > 0) {
		mark_samples_left--;
		return 1;
	}
	if (space_samples_left > 0) {
		space_samples_left--;
		return 0;
	}
	if (symbol_next == NULL && !take_a_character()) {
		//text put just now, after the ring was found empty, is not lost
		//sight of
		atomic_store(&is_busy, 0);
		if (atomic_load(&ring_taken) != atomic_load(&ring_put))
			atomic_store(&is_busy, 1);
		return 0;
	}
	take_a_symbol();
	return cw_text_is_key_down();
}

// whether there is text being sent or waiting to be; any thread may ask
int cw_text_is_busy(void) {
	return atomic_load(&is_busy);
}
