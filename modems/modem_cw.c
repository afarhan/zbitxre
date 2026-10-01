// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_cw.c — CW and CWR.
//
// RECEIVING
//
// CW is received as one sideband, narrow, with the first oscillator set off
// by the pitch so that the signal is heard at the pitch; core_sdr.c does
// that with the pitch it is given, and its filter does the rest. The audio
// goes to the speaker. Decoding the CW into text is to come here.
//
// THE KEYER
//
// The keyer is cw_keyer.c, which says where it comes from and what each of
// its modes does. It works a sample at a time and has to be told, for each
// sample, whether each contact of the key is closed. This file tells it.
//
// hw/key.c has the kernel report every closure and opening of the key
// jack's two contacts with its time, so none is lost however short. The
// tip is the dot paddle, or the straight key; the ring is the dash paddle.
// At the start of each block of samples the reports that have come are
// taken, and as the block's samples are made, one by one, each report is
// acted on at the sample its time falls on.
//
// For that the samples have to be made a little behind the clock: a report
// can only be acted on at its own time if that time has not yet been
// passed. So the keyer's time runs KEYER_LAG_US behind, a little more than
// a block, 12 ms. It is the same for every closure and every opening, so
// what is keyed is what was sent, to the sample, 12 ms later. The author's
// rule for CW is that no tap is ever lost and the timing is true; a steady
// few milliseconds are not felt.
//
// The keyer's clock is set from the system's when keying begins and then
// counts samples, so that the elements are timed by the sound card's own
// clock and are exact. When nothing has been sent and nothing is waiting,
// it stops, and the next closure sets it again.
//
// Should a report be lost, the pins themselves are looked at whenever no
// report is waiting: no report for 12 ms means the pins are as the keyer
// was last told, and if they are not, it is told.
//
// THE SIDETONE
//
// While the keyer is sending, the speaker is given its sidetone in place of
// the receiver's audio: a tone at the pitch, shaped by the keyer's
// envelope. It begins with the first element, in receive, before the radio
// has gone to transmit.
//
// TRANSMITTING
//
// The author's rule: what troubles an operator is a tap of the paddle that
// is lost, and a sidetone that does not come at once; a transmitter that
// follows some tens of milliseconds behind troubles nobody, as a radio
// with relays never did.
//
// So the keyer does not wait for the transmitter. An element begins when
// the paddle closes, with its sidetone, and the modem says, through
// wants_tx(), that it has something to send. The radio then goes to
// transmit, which takes the waits of tr_switch() in core_sdr.c, 33 ms as
// zbitxv2 had them. What is transmitted is what the keyer keyed, held back
// by just as much as lets the transmitter come on first; so the first
// element goes out whole, and every element and space after it is as long
// as the keyer made it.
//
// How much that is: with nothing held back, the RF follows the paddle by
// the keyer's 12 ms and the 7 ms of the transmit filter and the sound card,
// 19 ms. The transmitter is on TR_START_MS after the paddle, for the radio
// program to see that the modem wants it, and then tr_switch()'s waits.
// The keyed shape is held back by the difference, and TR_MARGIN_MS more;
// with waits of 14 ms or less it is not held back at all. The core gives
// the waits, tr_wait_to_tx_ms(), and the modem works the delay out afresh
// each time keying begins from rest.
//
// HOW LATE THE RF IS, AND WHY IT IS LEFT THERE
//
// The author measured it, an oscilloscope on the paddle and on the RF:
//
//   65 ms   the first build, the shape held back four blocks
//   40 ms   held back two blocks, the changeover begun on the contacts
//   30 ms   the changeover's waits brought from 23 + 10 ms to 10 + 10
//   25 ms   the waits at 1 + 1, the dots perfectly shaped, and steady
//
// The author had chosen the changeover's waits out of thin air, and they
// came down because from sbitx v3 on, and in every zbitx, the switching is
// by PIN diodes and not relays; core_sdr.c says where they are kept. With
// waits that add up to 13 ms or less the lines are not what the RF waits
// for: at 1 + 1 the transmitter's line was up 5 ms after the paddle.
//
// The 25 ms that are left are all here and in the sound card: the keyer's
// 12 ms, some 8 ms that the block waits in the card's queue behind what is
// already there, and the 5 ms of the transmit filter. The 8 is what is left
// over from the other two and was not measured by itself.
//
// The author's decision, 2 October 2026, is to leave it at that and give it
// to operators to try. 25 ms is 40 Hz, barely to be noticed, and some 20 ms
// is no more than the hand takes to reach the paddle and the paddle to
// close its contact. Nothing more is to be built to shorten it until they
// have said what they feel.
//
// What could be built, if they want it: the audio thread works for about
// 1.9 ms of each 10.7 ms block in receive and waits for the card the rest
// of the time. It could wait for the key as well, and when a contact
// closes write the keyed samples over what is already queued in the card,
// from 2 ms ahead of what is being played; ALSA allows it,
// snd_pcm_rewind(), and it was tried once on this card, a tone put 2 ms
// ahead of 23 ms queued, which came out clean. With that the keyer need not
// run behind the clock, and with CW's carrier sent past the transmit
// filter, which a keyed and shaped tone does not need, the sidetone and the
// RF would both follow the paddle by 3 to 5 ms. It is a rewriting of the
// loop in hw/audio.c: two things waited for, the keyed samples written in
// slices of 2 ms, and a copy kept of what is queued so that the rest goes
// back as it was. A slice written too close to what is being played would
// click, and the changeover's lines would be what the RF waits for again.
//
// For comparison: an Elecraft K3S puts its RF out a little over 10 ms
// after the key, 4.6 ms in its QRQ mode, and a Kenwood TS-590SG about 10;
// a Hermes-Lite 2 keyed through a computer, 50 to 70. How long the sidetone
// takes here has not been measured; it should be the keyer's 12 ms and the
// queue, some 20 ms.
//
// To lose no time in starting the changeover, wants_tx() also looks at the
// contacts themselves: a contact that is closed is something to send,
// before the keyer has come to it. The transmitted signal is a tone at the pitch, shaped by the
// same envelope, a third of full scale as in zbitxv2, which the core sends
// as one sideband; the oscillator is set off by the pitch, so the signal
// is on the frequency shown.
//
// The modem goes on wanting to transmit until CW_DELAY has passed since
// the last of what was sent, and a little more, PLAYED_OUT_SAMPLES, for
// what is still queued in the sound card; then the radio goes back to
// receive.
//
// THE CONTROLS
//
// The modem's own controls are registered in its start(), in the group CW:
//
//   WPM       the keyer's speed, in words a minute, 5 to 50.
//   PITCH     the tone a CW signal is heard at, and the sidetone's, 100 to
//             3000 Hz in steps of 10.
//   BW_CW     the width of the passband, 50 to 3000 Hz in steps of 50.
//   CW_INPUT  what is plugged into the key jack and how it is worked:
//             STRAIGHT, IAMBIC, IAMBICB, BUG or ULTIMATIC. The names are
//             zbitxv2's, with the two that JJ's software added.
//   SIDETONE  how loud the sidetone is, 0 to 100.
//   CW_DELAY  how long the radio stays in transmit after the last element,
//             0 to 1000 ms in steps of 50.
//
// As in zbitxv2, the passband is centred on the pitch: from PITCH - BW_CW/2
// to PITCH + BW_CW/2. Narrowing it closes in on the signal being heard, and
// a signal off the pitch by more than half the bandwidth is not heard. CW
// keeps a bandwidth of its own, apart from the voice modes'.
//
// WHO CHANGES WHAT
//
// The controls are changed by the thread that owns them, and the keyer and
// the sidetone run in the audio thread. What passes between them is single
// numbers, the speed, the mode, the pitch and the sidetone's level, each
// kept where the audio thread reads it whole.

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdr.h"
#include "board.h"
#include "key.h"
#include "control.h"
#include "modem.h"
#include "cw_keyer.h"

#define WPM_LOWEST 5
#define WPM_HIGHEST 50
#define PITCH_LOWEST 100
#define PITCH_HIGHEST 3000
#define PITCH_STEP 10
#define BW_CW_LOWEST 50
#define BW_CW_HIGHEST 3000
#define BW_CW_STEP 50
#define SIDETONE_HIGHEST 100
#define CW_DELAY_HIGHEST 1000
#define CW_DELAY_STEP 50
#define SAMPLES_PER_MS (CW_KEYER_RATE_HZ / 1000)
#define REQUEST_MAX 32
#define ANSWER_MAX 100

#define KEYER_LAG_US 12000
#define KEY_EVENTS_MAX 32
// a sample lasts 125/12 microseconds at 96000 a second
#define US_PER_SAMPLE_TIMES_12 125
#define TWELFTHS 12

// what vfo_read() gives at the peak of the tone, and the speaker's sample
// at the peak of the sidetone at its loudest
#define VFO_PEAK 1073741824.0
#define SIDETONE_PEAK 150000000.0
#define NO_PITCH 0

// the most the keyed shape can be held back for the transmitter, and how
// much longer than the hang time the radio stays in transmit
#define RF_DELAY_SAMPLES_MAX (8 * MAX_BINS/2)
#define PLAYED_OUT_SAMPLES (3 * MAX_BINS/2)
// from the paddle to the start of tr_switch(), what is allowed over its
// waits, and from the keyer to the RF with nothing held back, as measured
#define TR_START_MS 2
#define TR_MARGIN_MS 3
#define TX_PIPELINE_MS 7
#define US_PER_MS 1000
// zbitxv2 sends the keyed tone at a third of full scale
#define TX_TONE_DIVISOR 3.0

static int32_t baseband_output[MAX_BINS/2];

static atomic_int pitch_hz;
static atomic_int sidetone_level;
static atomic_int hang_samples;
static atomic_int is_wanting_tx;
static atomic_int is_ring_a_paddle;

static int32_t sidetone_output[MAX_BINS/2];
static float shape_delayed[RF_DELAY_SAMPLES_MAX];
static int shape_position;
static int rf_delay_samples;
static int samples_to_send;
static int samples_to_hang;
static struct vfo tx_tone;

static struct key_event events[KEY_EVENTS_MAX];
static int event_count;
static int is_keyer_running;
static int64_t keyer_us;
static int keyer_twelfths;
static int is_dot_closed;
static int is_dash_closed;
static struct vfo sidetone;
static int sidetone_hz = NO_PITCH;

static void take_key_events(void) {
	while (event_count < KEY_EVENTS_MAX && key_take_event(&events[event_count]))
		event_count++;
}

static void set_contact(const struct key_event *event) {
	if (event->contact == KEY_TIP)
		is_dot_closed = event->is_closed;
	else
		is_dash_closed = event->is_closed;
}

// the reports whose time has come are acted on, and the keyer's clock
// moves on a sample
static void follow_key_for_a_sample(void) {
	int acted_on = 0;

	while (acted_on < event_count && events[acted_on].us <= keyer_us) {
		set_contact(&events[acted_on]);
		acted_on++;
	}
	if (acted_on > 0) {
		event_count -= acted_on;
		memmove(events, events + acted_on, event_count * sizeof(events[0]));
	}
	keyer_twelfths += US_PER_SAMPLE_TIMES_12;
	keyer_us += keyer_twelfths / TWELFTHS;
	keyer_twelfths %= TWELFTHS;
}

// with no report waiting, the pins are as the keyer was last told; if a
// report was lost they are not, and the keyer is told
static void follow_the_pins(void) {
	if (event_count > 0)
		return;
	is_dot_closed = board_key_closed();
	is_dash_closed = board_ptt_closed();
}

// how long the keyed shape is held back so that the transmitter is on
// before it; the header says how it is worked out
static int rf_delay_for_the_changeover(void) {
	int ms = TR_START_MS + tr_wait_to_tx_ms() + TR_MARGIN_MS
		- KEYER_LAG_US / US_PER_MS - TX_PIPELINE_MS;
	int samples = ms * SAMPLES_PER_MS;

	if (samples < 0)
		return 0;
	if (samples > RF_DELAY_SAMPLES_MAX)
		return RF_DELAY_SAMPLES_MAX;
	return samples;
}

// with nothing held back from an earlier element, the delay is set afresh
static void set_the_rf_delay(void) {
	if (samples_to_send > 0)
		return;
	rf_delay_samples = rf_delay_for_the_changeover();
	memset(shape_delayed, 0, sizeof(shape_delayed));
	shape_position = 0;
}

// whether there is keying to do in this block; the keyer's clock is set
// when it begins
static int is_keying(void) {
	take_key_events();
	follow_the_pins();
	if (is_keyer_running)
		return 1;
	if (event_count == 0 && !is_dot_closed && !is_dash_closed)
		return 0;
	set_the_rf_delay();
	keyer_us = key_now_us() - KEYER_LAG_US;
	keyer_twelfths = 0;
	is_keyer_running = 1;
	return 1;
}

static void tune_the_sidetone(void) {
	int wanted_hz = atomic_load(&pitch_hz);

	if (wanted_hz == sidetone_hz)
		return;
	vfo_start(&sidetone, wanted_hz, 0);
	vfo_start(&tx_tone, wanted_hz, 0);
	sidetone_hz = wanted_hz;
}

// the shape the keyer made rf_delay_samples ago, for the transmitter; the
// shape just made is kept in its place
static float delay_the_shape(float shape) {
	float delayed;

	if (rf_delay_samples == 0)
		return shape;
	delayed = shape_delayed[shape_position];
	shape_delayed[shape_position] = shape;
	shape_position++;
	if (shape_position >= rf_delay_samples)
		shape_position = 0;
	return delayed;
}

// the modem wants to transmit while there is anything to send, and for
// the hang time after
static void note_what_is_to_send(float shape, int count) {
	if (shape > 0 || cw_keyer_is_busy()) {
		samples_to_send = rf_delay_samples;
		samples_to_hang = atomic_load(&hang_samples) + PLAYED_OUT_SAMPLES;
	}
	else if (samples_to_send > 0)
		samples_to_send -= count;
	else if (samples_to_hang > 0)
		samples_to_hang -= count;
}

// a block of keying. The speaker's samples are replaced by the sidetone
// wherever the keyer is sending; where it is not, a dash contact that a
// straight key's plug holds closed, say, they are left as they are. In
// transmit, tx is given the tone to send, shaped as the keyer shaped it
// rf_delay_samples before; in receive tx is NULL
static void key_a_block(int32_t *speaker, double *tx) {
	double peak = SIDETONE_PEAK * atomic_load(&sidetone_level) / SIDETONE_HIGHEST;
	float shape = 0;
	float delayed;
	int is_key_down;
	int i;

	if (!is_keying() && samples_to_send <= 0 && tx == NULL) {
		note_what_is_to_send(0, MAX_BINS/2);
		atomic_store(&is_wanting_tx, samples_to_hang > 0);
		return;
	}
	tune_the_sidetone();
	for (i = 0; i < MAX_BINS/2; i++) {
		follow_key_for_a_sample();
		is_key_down = cw_keyer_is_key_down(is_dot_closed, is_dash_closed);
		shape = cw_keyer_shape(is_key_down);
		if (cw_keyer_is_busy())
			speaker[i] = (int32_t)(vfo_read(&sidetone) / VFO_PEAK * shape * peak);
		delayed = delay_the_shape(shape);
		if (tx != NULL)
			tx[i] = vfo_read(&tx_tone) / VFO_PEAK * delayed / TX_TONE_DIVISOR;
		note_what_is_to_send(shape, 1);
	}
	if (!cw_keyer_is_busy() && event_count == 0)
		is_keyer_running = 0;
	atomic_store(&is_wanting_tx, samples_to_send > 0 || samples_to_hang > 0);
}

static void rx_cw(complex float *bins) {
	demod_ssb(bins, baseband_output);
	key_a_block(baseband_output, NULL);
	speaker_play(baseband_output, MAX_BINS/2);
}

static void tx_cw(const int32_t *mic, double *samples) {
	(void)mic;
	memset(sidetone_output, 0, sizeof(sidetone_output));
	key_a_block(sidetone_output, samples);
	speaker_play(sidetone_output, MAX_BINS/2);
}

// with a straight key the ring is not a contact of the key: its plug may
// hold it closed
static int cw_wants_tx(void) {
	if (board_key_closed())
		return 1;
	if (atomic_load(&is_ring_a_paddle) && board_ptt_closed())
		return 1;
	return atomic_load(&is_wanting_tx);
}

static void cw_reset(void) {
	cw_keyer_reset();
	event_count = 0;
	is_keyer_running = 0;
	is_dot_closed = 0;
	is_dash_closed = 0;
	samples_to_send = 0;
	set_the_rf_delay();
	samples_to_hang = 0;
	atomic_store(&is_wanting_tx, 0);
}

//the core moves the oscillator by the pitch only when the mode is set, as
//zbitxv2 did, so the mode is set again after this, and after a new pitch
static void cw_select(void) {
	char request[REQUEST_MAX];
	char answer[ANSWER_MAX];
	int pitch = control_get_int("PITCH");
	int half_width = control_get_int("BW_CW") / 2;

	snprintf(request, sizeof(request), "rx_pitch=%d", pitch);
	sdr_request(request, answer);
	set_passband(pitch - half_width, pitch + half_width);
}

static int apply_pitch(struct control *c, const char *value, char *why, int room) {
	const char *mode = control_get("MODE");
	char request[REQUEST_MAX];
	char answer[ANSWER_MAX];

	(void)why;
	(void)room;
	snprintf(c->value, sizeof(c->value), "%s", value);
	atomic_store(&pitch_hz, atoi(value));
	if (mode == NULL || modem_find(mode) == NULL || modem_find(mode)->select != cw_select)
		return 0;
	cw_select();
	snprintf(request, sizeof(request), "r1:mode=%s", mode);
	sdr_request(request, answer);
	return 0;
}

//a new bandwidth is put to use at once by whichever mode is in use, if it
//is one of those that use it
static int apply_to_mode_in_use(struct control *c, const char *value, char *why, int room) {
	const char *mode = control_get("MODE");
	const struct modem *m;

	(void)why;
	(void)room;
	if (mode == NULL)
		return 0;
	//control.c keeps the new value only after apply returns, and select()
	//reads it from the control, so it is kept here first
	snprintf(c->value, sizeof(c->value), "%s", value);
	m = modem_find(mode);
	if (m != NULL && m->select != NULL)
		m->select();
	return 0;
}

static int apply_wpm(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	cw_keyer_set_wpm(atoi(value));
	return 0;
}

static int apply_cw_input(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	atomic_store(&is_ring_a_paddle, strcmp(value, "STRAIGHT") != 0);
	if (!strcmp(value, "STRAIGHT"))
		cw_keyer_set_mode(CW_KEYER_STRAIGHT);
	else if (!strcmp(value, "IAMBIC"))
		cw_keyer_set_mode(CW_KEYER_IAMBIC);
	else if (!strcmp(value, "IAMBICB"))
		cw_keyer_set_mode(CW_KEYER_IAMBICB);
	else if (!strcmp(value, "BUG"))
		cw_keyer_set_mode(CW_KEYER_BUG);
	else
		cw_keyer_set_mode(CW_KEYER_ULTIMATIC);
	return 0;
}

static int apply_sidetone(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	atomic_store(&sidetone_level, atoi(value));
	return 0;
}

static int apply_cw_delay(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	atomic_store(&hang_samples, atoi(value) * SAMPLES_PER_MS);
	return 0;
}

static struct control wpm = {
	.group = "CW", .name = "WPM", .kind = CONTROL_NUMBER,
	.min = WPM_LOWEST, .max = WPM_HIGHEST, .step = 1,
	.value = "20", .apply = apply_wpm,
};

static struct control pitch = {
	.group = "CW", .name = "PITCH", .kind = CONTROL_NUMBER,
	.min = PITCH_LOWEST, .max = PITCH_HIGHEST, .step = PITCH_STEP,
	.value = "600", .apply = apply_pitch,
};

static struct control bw_cw = {
	.group = "CW", .name = "BW_CW", .kind = CONTROL_NUMBER,
	.min = BW_CW_LOWEST, .max = BW_CW_HIGHEST, .step = BW_CW_STEP,
	.value = "400", .apply = apply_to_mode_in_use,
};

static struct control cw_input = {
	.group = "CW", .name = "CW_INPUT", .kind = CONTROL_CHOICE,
	.choices = "STRAIGHT/IAMBIC/IAMBICB/BUG/ULTIMATIC",
	.value = "IAMBIC", .apply = apply_cw_input,
};

static struct control sidetone_control = {
	.group = "CW", .name = "SIDETONE", .kind = CONTROL_NUMBER,
	.min = 0, .max = SIDETONE_HIGHEST, .step = 1,
	.value = "30", .apply = apply_sidetone,
};

static struct control cw_delay = {
	.group = "CW", .name = "CW_DELAY", .kind = CONTROL_NUMBER,
	.min = 0, .max = CW_DELAY_HIGHEST, .step = CW_DELAY_STEP,
	.value = "300", .apply = apply_cw_delay,
};

static void cw_start(void) {
	control_register(&wpm);
	control_register(&pitch);
	control_register(&bw_cw);
	control_register(&cw_input);
	control_register(&sidetone_control);
	control_register(&cw_delay);
	apply_cw_delay(&cw_delay, cw_delay.value, NULL, 0);
	atomic_store(&pitch_hz, atoi(pitch.value));
	atomic_store(&sidetone_level, atoi(sidetone_control.value));
	cw_keyer_reset();
	cw_keyer_set_wpm(atoi(wpm.value));
	apply_cw_input(&cw_input, cw_input.value, NULL, 0);
	if (key_open(board_in_use()) < 0)
		printf("the key cannot be read: the kernel will not report its edges\n");
}

const struct modem modem_cw = {
	.name = "CW",
	.order = 3,
	.start = cw_start,
	.select = cw_select,
	.reset = cw_reset,
	.rx = rx_cw,
	.tx = tx_cw,
	.wants_tx = cw_wants_tx,
};

const struct modem modem_cwr = {
	.name = "CWR",
	.order = 4,
	.select = cw_select,
	.reset = cw_reset,
	.rx = rx_cw,
	.tx = tx_cw,
	.wants_tx = cw_wants_tx,
};
