// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// cw_keyer.c — the CW keyer: paddles or a key in, a keyed and shaped
// envelope out, a sample at a time.
//
// WHERE IT COMES FROM
//
// zbitxv2's keyer, in its modem_cw.c, was adapted from Richard Chapman
// KC4IFB's description of an iambic keyer in QEX, September/October 2009.
// It was buggy: it lost paddle taps and its timing was uneven.
//
// The zBitx software of JJ, W9JES (github.com/drexjj/zbitx) replaced it
// with a state machine that its source credits to Mike, KB2ML, "the KB2ML
// sBitx keyer state machine", which added the bug and Ultimatic modes to
// the straight key and the two iambic modes, and with an envelope of the
// rising half of a Blackman-Harris window, 5 ms long.
//
// Frank Waarsenburg, PA3CNO, then found, with the help of OpenAI's Codex,
// why that keyer still missed elements and felt uneven, and corrected it
// (waarsenburg.com, September 2026): the straight key was looked at only
// every 5 ms and is now followed sample by sample; the envelope's fall was
// counted on top of the space after an element, making every space 5 ms
// too long, and is now inside it; iambic A added an element when a squeeze
// was let go, which is what tells B from A; and the envelope's table had
// 479 values where 480 were meant, the last one read as nothing, a click
// at the top of every rise.
//
// This keyer is written afresh. It does what KB2ML's state machine does,
// mode for mode, with PA3CNO's corrections, and none of their code is in
// it. The envelope is the same Blackman-Harris rise, worked out here when
// the keyer is reset and not kept as a table.
//
// HOW IT IS USED
//
// cw_keyer_is_key_down() is called once for every sample, 96000 times a
// second, with whether each of the two contacts is closed at that sample,
// and says whether the key is down for that sample. cw_keyer_shape() turns
// that into an amplitude from 0 to 1, with the rise and the fall. So the
// keyer keeps time by counting samples, and an element or a space is exact
// to a sample. Whoever calls it has to know when each contact closed and
// opened, to the sample; modem_cw.c says how.
//
// Nothing here reads a pin, waits, or allocates. It is for the audio
// thread. The mode and the speed are set by the thread that owns the
// controls; each is one word, and a change takes effect at the next
// sample.
//
// THE CONTACTS
//
// A contact that closes is taken as closed at once. One that opens is
// taken as open only when it has stayed open for 2 ms, since contacts
// bounce as they close and as they open. So nothing is added before a
// closure is acted on, and a straight key's mark comes out 2 ms longer
// than the key was held, every time alike.
//
// THE MODES
//
// STRAIGHT   The key is down while the dot contact is closed. The dash
//            contact is not looked at: with a key on a two-pole plug it is
//            shorted to ground all the time.
//
// For the others an element is a mark, a dot long or three, and after it
// a space a dot long, which is part of the element: the next one cannot
// begin until the space is over. A dot is 1.2 seconds divided by the speed
// in words a minute.
//
// What makes a keyer feel right is that it never loses a tap. A paddle
// that closes while an element is being sent, during its mark or its
// space, is remembered, even if it has opened again before the element is
// over, and its element is sent next. That is the dot memory and the dash
// memory. A paddle that is still closed when an element is over is sent
// again without being remembered.
//
// IAMBIC     Iambic A. With both paddles closed, dots and dashes take
//            turns. When both are let go the element being sent is
//            finished and that is all.
// IAMBICB    Iambic B. As A, but if the other paddle was closed at any
//            time during the mark of an element, its element is sent after
//            it; so a squeeze let go during an element is followed by one
//            more, the opposite one.
// ULTIMATIC  With both paddles closed, the one that closed last is
//            repeated.
// BUG        The dot paddle makes dots, as the others do. The dash paddle
//            is a straight key: the key is down while it is closed.
//
// From rest, with both paddles closed in the same sample, the dot goes
// first.

#include <math.h>

#include "cw_keyer.h"

#define SAMPLES_A_MINUTE_PER_WORD (CW_KEYER_RATE_HZ * 12 / 10)
#define DASH_DOTS 3
#define WPM_LOWEST 5
#define WPM_HIGHEST 60
#define STAYS_OPEN_SAMPLES (CW_KEYER_RATE_HZ / 500)
#define RISE_SAMPLES (CW_KEYER_RATE_HZ / 200)

// the four terms of the Blackman-Harris window
#define BH_A0 0.35875
#define BH_A1 0.48829
#define BH_A2 0.14128
#define BH_A3 0.01168

enum element {
	NO_ELEMENT,
	DOT,
	DASH,
};

// a contact, with its bounce taken out
struct contact {
	int is_closed;
	int open_samples;
	int has_just_closed;
};

static enum cw_keyer_mode mode = CW_KEYER_IAMBIC;
static int dot_samples = SAMPLES_A_MINUTE_PER_WORD / 20;

static struct contact dot_paddle;
static struct contact dash_paddle;

static enum element sending;
static enum element last_sent;
static enum element last_closed;
static int mark_samples_left;
static int space_samples_left;
static int is_dot_remembered;
static int is_dash_remembered;
static int is_key_held;

static float rise[RISE_SAMPLES];
static int rise_position;

// the rising half of a Blackman-Harris window, from 0 at the first sample
// to 1 at the last
static void make_the_rise(void) {
	double lowest = BH_A0 - BH_A1 + BH_A2 - BH_A3;
	double angle;
	double window;
	int i;

	for (i = 0; i < RISE_SAMPLES; i++) {
		angle = M_PI * i / (RISE_SAMPLES - 1);
		window = BH_A0 - BH_A1 * cos(angle) + BH_A2 * cos(2 * angle)
			- BH_A3 * cos(3 * angle);
		rise[i] = (float)((window - lowest) / (1.0 - lowest));
	}
}

void cw_keyer_reset(void) {
	dot_paddle.is_closed = 0;
	dot_paddle.open_samples = 0;
	dot_paddle.has_just_closed = 0;
	dash_paddle = dot_paddle;
	sending = NO_ELEMENT;
	last_sent = NO_ELEMENT;
	last_closed = NO_ELEMENT;
	mark_samples_left = 0;
	space_samples_left = 0;
	is_dot_remembered = 0;
	is_dash_remembered = 0;
	is_key_held = 0;
	rise_position = 0;
	make_the_rise();
}

void cw_keyer_set_mode(enum cw_keyer_mode new_mode) {
	mode = new_mode;
}

void cw_keyer_set_wpm(int wpm) {
	if (wpm < WPM_LOWEST)
		wpm = WPM_LOWEST;
	if (wpm > WPM_HIGHEST)
		wpm = WPM_HIGHEST;
	dot_samples = SAMPLES_A_MINUTE_PER_WORD / wpm;
}

static void contact_follow(struct contact *c, int is_closed_now) {
	c->has_just_closed = 0;
	if (is_closed_now) {
		c->has_just_closed = !c->is_closed;
		c->is_closed = 1;
		c->open_samples = 0;
		return;
	}
	if (!c->is_closed)
		return;
	if (c->open_samples == STAYS_OPEN_SAMPLES)
		c->is_closed = 0;
	c->open_samples++;
}

static void element_start(enum element e) {
	sending = e;
	last_sent = e;
	space_samples_left = dot_samples;
	if (e == DOT) {
		mark_samples_left = dot_samples;
		is_dot_remembered = 0;
	}
	else {
		mark_samples_left = dot_samples * DASH_DOTS;
		is_dash_remembered = 0;
	}
}

// while an element is being sent: a paddle that closes is remembered, and
// in iambic B so is the other paddle if it is closed during the mark
static void remember_paddles(void) {
	if (dot_paddle.has_just_closed)
		is_dot_remembered = 1;
	if (dash_paddle.has_just_closed && mode != CW_KEYER_BUG)
		is_dash_remembered = 1;
	if (mode != CW_KEYER_IAMBICB || mark_samples_left == 0)
		return;
	if (sending == DOT && dash_paddle.is_closed)
		is_dash_remembered = 1;
	if (sending == DASH && dot_paddle.is_closed)
		is_dot_remembered = 1;
}

// with both a dot and a dash wanted, which goes first
static enum element element_of_two(void) {
	if (mode == CW_KEYER_ULTIMATIC && last_closed != NO_ELEMENT)
		return last_closed;
	if (last_sent == DOT)
		return DASH;
	return DOT;
}

// with no element being sent: the next one, if a paddle or a memory asks
// for one
static enum element element_wanted(void) {
	int is_dot_wanted = dot_paddle.is_closed || is_dot_remembered;
	int is_dash_wanted = dash_paddle.is_closed || is_dash_remembered;

	if (mode == CW_KEYER_BUG)
		is_dash_wanted = 0;
	if (is_dot_wanted && is_dash_wanted)
		return element_of_two();
	if (is_dot_wanted)
		return DOT;
	if (is_dash_wanted)
		return DASH;
	return NO_ELEMENT;
}

// one sample of the element being sent; says whether the key is down
static int element_step(void) {
	if (mark_samples_left > 0) {
		mark_samples_left--;
		return 1;
	}
	space_samples_left--;
	if (space_samples_left == 0)
		sending = NO_ELEMENT;
	return 0;
}

static int paddles_step(void) {
	enum element wanted;

	if (dash_paddle.has_just_closed)
		last_closed = DASH;
	if (dot_paddle.has_just_closed)
		last_closed = DOT;
	if (sending != NO_ELEMENT) {
		remember_paddles();
		return element_step();
	}
	is_key_held = mode == CW_KEYER_BUG && dash_paddle.is_closed;
	if (is_key_held)
		return 1;
	wanted = element_wanted();
	if (wanted == NO_ELEMENT) {
		last_sent = NO_ELEMENT;
		return 0;
	}
	element_start(wanted);
	return element_step();
}

int cw_keyer_is_key_down(int is_dot_closed, int is_dash_closed) {
	contact_follow(&dot_paddle, is_dot_closed);
	contact_follow(&dash_paddle, is_dash_closed);
	if (mode == CW_KEYER_STRAIGHT) {
		is_key_held = dot_paddle.is_closed;
		return is_key_held;
	}
	return paddles_step();
}

// whether there is anything still to send: an element under way, one
// remembered, or the key held
int cw_keyer_is_busy(void) {
	return sending != NO_ELEMENT || is_dot_remembered || is_dash_remembered
		|| is_key_held || rise_position > 0;
}

// the amplitude for a sample: it rises over 5 ms from when the key goes
// down and falls over 5 ms from when it comes up, and a key that goes down
// again during the fall rises from where the fall had got to
float cw_keyer_shape(int is_key_down) {
	if (is_key_down && rise_position < RISE_SAMPLES - 1)
		rise_position++;
	if (!is_key_down && rise_position > 0)
		rise_position--;
	return rise[rise_position];
}
