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
// A CHARACTER ALREADY KEYED
//
// A client with a keyer of its own, the telephone's, sends what was keyed
// a character at a time: its dots and dashes, and how long before it the
// key was up. cw_text_put_code() takes that. The dots and dashes are sent
// as text's are, at the speed set here, so that what goes on the air is
// evenly made however unevenly the characters came across the internet;
// and the character begins when the key has been up, here, for as long as
// it was up there, or at once if it already has been for longer, and never
// sooner than the three dots that end the character before. So the rhythm
// is the operator's own: the spaces between letters and between words are
// what was keyed. When a character comes too late for that, because it
// took longer to key than the one before it or the line held it up, the
// space before it is the longer by that much, and everything after is that
// much later; it does not happen again until a still longer wait.
//
// In the ring such a character is a mark that one begins, the milliseconds
// in figures, the dots and dashes, and a mark that it ends, put there
// whole or not at all.
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
#include <stdio.h>
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
#define CODE_BEGINS '\001'
#define CODE_ENDS '\002'
#define CODE_SYMBOLS_MOST 12
#define GAP_MOST_MS 10000
#define GAP_FIGURES_MOST 5
#define SAMPLES_A_MS (CW_KEYER_RATE_HZ / 1000)
// a key that has been up for longer than any gap asked for
#define UP_FOR_LONG (GAP_MOST_MS * SAMPLES_A_MS + 1)

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
static char keyed_code[CODE_SYMBOLS_MOST + 1];
static int gap_samples;
static int up_samples = UP_FOR_LONG;

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
	gap_samples = 0;
	up_samples = UP_FOR_LONG;
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

// a character as it was keyed elsewhere: its dots and dashes, "-.-", and
// for how many milliseconds the key was up before it, 0 if that is not
// known. Says whether it was taken: it is not if it is not dots and dashes,
// one to twelve of them, or there is no room for all of it
int cw_text_put_code(const char *code, int gap_ms) {
	char whole[1 + GAP_FIGURES_MOST + CODE_SYMBOLS_MOST + 2];
	unsigned int put = atomic_load(&ring_put);
	unsigned int taken = atomic_load(&ring_taken);
	size_t symbols = strlen(code);
	size_t length;
	size_t i;

	if (symbols == 0 || symbols > CODE_SYMBOLS_MOST || strspn(code, ".-") != symbols)
		return 0;
	if (gap_ms < 0)
		gap_ms = 0;
	if (gap_ms > GAP_MOST_MS)
		gap_ms = GAP_MOST_MS;
	length = (size_t)snprintf(whole, sizeof(whole), "%c%d%s%c", CODE_BEGINS, gap_ms, code,
		CODE_ENDS);
	if (RING_SIZE - (put - taken) < length)
		return 0;
	for (i = 0; i < length; i++)
		ring[(put + i) % RING_SIZE] = whole[i];
	atomic_store(&ring_put, put + (unsigned int)length);
	atomic_store(&is_busy, 1);
	return 1;
}

// what is being sent is cut off where it is, and what is waiting is dropped
void cw_text_abort(void) {
	cw_text_reset();
}

// a character that was keyed elsewhere is read out of the ring, from the
// mark that it begins to the mark that it ends, which is left for the
// caller to step over: the gap asked for before it, and its dots and
// dashes. It was put whole, so its end is there
static const char *take_a_code(unsigned int *taken) {
	int count = 0;
	int ms = 0;
	char c;

	for (c = ring[++*taken % RING_SIZE]; c != CODE_ENDS; c = ring[++*taken % RING_SIZE]) {
		if (isdigit((unsigned char)c))
			ms = ms * 10 + (c - '0');
		else if (count < CODE_SYMBOLS_MOST)
			keyed_code[count++] = c;
	}
	keyed_code[count] = 0;
	gap_samples = ms * SAMPLES_A_MS;
	return keyed_code;
}

// the next character that has a code is taken up; says whether there was one
static int take_a_character(void) {
	unsigned int taken = atomic_load(&ring_taken);
	const char *code = NULL;

	gap_samples = 0;
	while (code == NULL && taken != atomic_load(&ring_put)) {
		if (ring[taken % RING_SIZE] == CODE_BEGINS)
			code = take_a_code(&taken);
		else
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

// one sample of what is being sent; says whether the key is down
static int text_step(void) {
	if (mark_samples_left > 0) {
		mark_samples_left--;
		return 1;
	}
	if (space_samples_left > 0) {
		space_samples_left--;
		return 0;
	}
	if (symbol_next == NULL) {
		if (!take_a_character()) {
			//text put just now, after the ring was found empty, is not lost
			//sight of
			atomic_store(&is_busy, 0);
			if (atomic_load(&ring_taken) != atomic_load(&ring_put))
				atomic_store(&is_busy, 1);
			return 0;
		}
		// a keyed character waits until the key has been up for as long
		// as it was where it was keyed; this sample is one of those
		if (gap_samples > up_samples) {
			space_samples_left = gap_samples - up_samples - 1;
			return 0;
		}
	}
	take_a_symbol();
	return text_step();
}

int cw_text_is_key_down(void) {
	int is_down = text_step();

	// for how long the key has been up, for a keyed character's gap
	if (is_down)
		up_samples = 0;
	else if (up_samples < UP_FOR_LONG)
		up_samples++;
	return is_down;
}

// whether there is text being sent or waiting to be; any thread may ask
int cw_text_is_busy(void) {
	return atomic_load(&is_busy);
}
