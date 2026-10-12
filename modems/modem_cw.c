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
// goes to the speaker, and is read into text, below.
//
// THE DECODER
//
// The decoder is zbitxv2's, with its names and its way, as its notes there
// said: "entirely written from the scratch after a preliminary read of a
// few Arduino decoders". All its state is in a struct cw_decoder; there is
// one. cw_rx() is given each block of the receiver's audio, before the
// sidetone takes its place in the speaker, and:
//
//   1. keeps every eighth sample, which makes 12000 a second, so that a
//      block is N_BINS, 128 samples, one tick of the decoder, 10.7 ms;
//   2. measures how strong the signal is at the pitch, with a Goertzel
//      filter, cw_rx_bin_detect(), the struct bin;
//   3. keeps a running high level and noise floor, and calls the signal a
//      mark above six tenths of the high and a space below four tenths,
//      cw_rx_update_levels();
//   4. lets a mark or a space change only after four ticks of it, which
//      takes out the short glitches, cw_rx_denoise();
//   5. times each mark and space in ticks, cw_rx_detect_symbol(), and at a
//      space as long as half a dash takes the marks so far for a letter,
//      cw_rx_match_letter(): a mark longer than half a dash is a dash, and
//      the dash's length follows the sender's speed as it goes. A space a
//      dash and a half long is a space between words.
//   6. looks the dots and dashes up in morse_rx_table, zbitxv2's, which has
//      the prosigns, <AR>, <BT>, and a few that are run together, 5NN, UR.
//      What is not in it is given as its dots and dashes.
//
// The speed it starts from is WPM's, and it is started again when WPM or
// PITCH is changed.
//
// What it reads goes out of the audio thread through a ring of characters
// that only it writes and only cw_rx_take() reads, in the thread that owns
// the controls; panel_cw.c says where it goes. Where zbitxv2 wrote to its
// console, cw_rx_say() puts the text in the ring.
//
// TWO DECODERS, SIDE BY SIDE
//
// The author can read CW off the waterfall, and set out how a decoder
// should read it from the spectrum, which cw_reader.c does and says how.
// For now both run, to be compared on the air:
//
//   decoder   zbitxv2's, as above: the Goertzel filter at the pitch on the
//             receiver's audio. Its text goes to the radio's terminal,
//             cw_rx_take().
//   reader    cw_reader.c's, from the receiver's own spectrum, the bins of
//             the block. Its text goes to the panel, cw_rx_take_reader().
//
// The reader is given, each block, the ratio to the grass of the bins
// around the pitch: the bin the pitch falls in, bin_of_audio(), and
// CW_READER_SPAN each side, CW_READER_BINS in all. Each bin is windowed as
// the core windows them for the spectrum, half of it less a quarter of each
// neighbour, and its power is taken. The grass is the 25th percentile of the
// power of the bins within GRASS_BINS of the pitch, 1.9 kHz each way: of so
// many bins, the few that are signals do not move it.
//
// The bins come before the receiver's filter, so BW_CW does not narrow what
// the reader hears: it finds the station itself, within 250 Hz of the
// pitch. The Goertzel filter is at a whole number of its 93.75 Hz bins, the
// one nearest the pitch: with PITCH at 600 it listens at 562.5 Hz. That is
// zbitxv2's, and is kept.
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
// TEXT AND MACROS
//
// Text is sent as CW too: cw_text.c turns characters into the key going
// down and up, at the keyer's speed, and the modem shapes and sends that
// as it does the paddles'. The radio goes to transmit for it and comes
// back when it is done, as for the paddles. A touch of a paddle or the key
// while text is being sent stops it. So does CW_DO, an action with the one
// choice ESC, for a client's button: the author asked for an ESC on the
// screen that halts a macro or what was typed. The text is the audio
// thread's to stop, so the action leaves it word, is_text_to_stop, and the
// audio thread stops the text at its next sample.
//
// The macros are the function keys' messages, read when the radio starts
// from data/cw.mc, a file in zbitxv2's form; macros.c says what is in one.
// MACRO is the control that sends them, an action: its choices are the
// macros' labels in the order of the file, and setting it to a label sends
// that macro. Two macros with the same label are one choice, the first.
// With no file there is no MACRO control.
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

#include <math.h>
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
#include "cw_text.h"
#include "macros.h"
#include "modem_cw.h"
#include "cw_reader.h"

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

#define MACRO_FILE "cw"
#define MACRO_CHOICES_MAX 64

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
static atomic_int decoder_wpm;
static atomic_int sidetone_level;
static atomic_int hang_samples;
static atomic_int is_wanting_tx;
static atomic_int is_ring_a_paddle;
static atomic_int is_text_to_stop;

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
	if (event_count == 0 && !is_dot_closed && !is_dash_closed && !cw_text_is_busy())
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
// whether anything is being sent or is waiting to be, by the paddles'
// keyer or as text
static int is_sending(void) {
	return cw_keyer_is_busy() || cw_text_is_busy();
}

// the key for a sample: the paddles' keyer's, or while text is being sent,
// the text's. A paddle or the key touched while text is being sent stops
// the text and drops what was waiting, as zbitxv2 did
static int key_for_a_sample(void) {
	int is_key_down = cw_keyer_is_key_down(is_dot_closed, is_dash_closed);

	if (!cw_text_is_busy())
		return is_key_down;
	if (atomic_load(&is_text_to_stop)) {
		atomic_store(&is_text_to_stop, 0);
		cw_text_abort();
		return is_key_down;
	}
	if (is_dot_closed || (atomic_load(&is_ring_a_paddle) && is_dash_closed)) {
		cw_text_abort();
		return is_key_down;
	}
	return cw_text_is_key_down();
}

static void note_what_is_to_send(float shape, int count) {
	if (shape > 0 || is_sending()) {
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
		is_key_down = key_for_a_sample();
		shape = cw_keyer_shape(is_key_down);
		if (is_sending())
			speaker[i] = (int32_t)(vfo_read(&sidetone) / VFO_PEAK * shape * peak);
		delayed = delay_the_shape(shape);
		if (tx != NULL)
			tx[i] = vfo_read(&tx_tone) / VFO_PEAK * delayed / TX_TONE_DIVISOR;
		note_what_is_to_send(shape, 1);
	}
	if (!is_sending() && event_count == 0)
		is_keyer_running = 0;
	atomic_store(&is_wanting_tx, samples_to_send > 0 || samples_to_hang > 0);
}

/* the decoder, zbitxv2's */

#define N_BINS 128
#define SAMPLING_FREQ 12000
#define DECIMATION (SOUND_CARD_RATE / SAMPLING_FREQ)
#define SOUND_CARD_RATE 96000
#define SAMPLE_SHIFT 8
#define MAX_SYMBOLS 100
#define HIGH_DECAY 100
#define NOISE_DECAY 100
#define LOWEST_LEVEL 100
#define MARK_LEVEL 30000
#define DECODED_ROOM 256
#define HISTORY_LENGTH 0xf
// the Hann window on the bins: half of a bin less a quarter of each
// neighbour
#define HANN_MIDDLE 0.5f
#define HANN_SIDE 0.25f
// the grass is the 25th percentile of the bins this many each side of the
// pitch
#define GRASS_BINS 40
#define GRASS_COUNT (2 * GRASS_BINS + 1)
#define GRASS_PERCENTILE 4
#define SECONDS_A_TICK ((double)(MAX_BINS / 2) / SOUND_CARD_RATE)

struct morse_rx {
	const char *c;
	const char *code;
};

static const struct morse_rx morse_rx_table[] = {
	{"~", " "}, //dummy, a null character
	{" ", " "},
	{"A", ".-"},
	{"B", "-..."},
	{"C", "-.-."},
	{"D", "-.."},
	{"E", "."},
	{"F", "..-."},
	{"G", "--."},
	{"H", "...."},
	{"I", ".."},
	{"J", ".---"},
	{"K", "-.-"},
	{"L", ".-.."},
	{"M", "--"},
	{"N", "-."},
	{"O", "---"},
	{"P", ".--."},
	{"Q", "--.-"},
	{"R", ".-."},
	{"S", "..."},
	{"T", "-"},
	{"U", "..-"},
	{"V", "...-"},
	{"W", ".--"},
	{"X", "-..-"},
	{"Y", "-.--"},
	{"Z", "--.."},
	{"1", ".----"},
	{"2", "..---"},
	{"3", "...--"},
	{"4", "....-"},
	{"5", "....."},
	{"6", "-...."},
	{"7", "--..."},
	{"8", "---.."},
	{"9", "----."},
	{"0", "-----"},
	{"<STOP>", ".-.-.-"},
	{"<COMMA>", "--..--"},
	{"?", "..--.."},
	{"/", "-..-."},
	{ "'", ".----."},
	{"!", "-.-.--"},
	{":", "---..."},
	{"-", "-....-"},
	{"_", "..--.-"},
	{"@", ".--.-."},
	{"<AR>", ".-.-."},
	{"<AS>", ".-..."},
	{"<STOP>", ".-.-."},
	{"<BT>", "-...-"},
	{"vu2", "...-..-..---"},
	{"vu3", "...-..-...--"},
	{"5nn", ".....-.-."},
	{"ur", "..-.-."},
};

struct bin {
	float coeff;
	float sine;
	float cosine;
	float omega;
	int k;
	double scalingFactor;
	int	freq;
	int n;
};

struct symbol {
	char is_mark;
	int	magnitude;
	int	ticks;
};

// what a decoder has read, on its way out of the audio thread; only the
// audio thread writes it, and only the thread that owns the controls reads
struct decoded {
	char text[DECODED_ROOM];
	atomic_uint head;
	atomic_uint tail;
};

struct cw_decoder{
	struct decoded *out;
	int dash_len;
	int mark;
	int prev_mark;
	int	n_bins;
	int ticker;
	int high_level;
	int noise_floor;
	int sig_state;
	int magnitude;
	int symbol_magnitude; // track the magnitude of the current symbol
	int wpm; // as set by the user
	struct bin signal;
	// this is a shift register of the states encountered
	int32_t history_sig;
	struct symbol symbol_str[MAX_SYMBOLS];
	int next_symbol;
};

static struct decoded decoded_by_tone;
static struct decoded decoded_by_reader;
static struct cw_decoder decoder = { .out = &decoded_by_tone };
static struct cw_reader reader;
static int reader_wpm;

// the text read is put in a ring; what does not fit is lost
static void say_into(struct decoded *d, const char *text) {
	unsigned head = atomic_load(&d->head);

	while (*text != 0) {
		if (head - atomic_load(&d->tail) >= DECODED_ROOM)
			break;
		d->text[head % DECODED_ROOM] = *text++;
		head++;
	}
	atomic_store(&d->head, head);
}

static void cw_rx_say(struct cw_decoder *p, const char *text) {
	say_into(p->out, text);
}

static int take_from(struct decoded *d, char *text, int room) {
	unsigned tail = atomic_load(&d->tail);
	unsigned head = atomic_load(&d->head);
	int count = 0;

	while (tail != head && count < room - 1) {
		text[count++] = d->text[tail % DECODED_ROOM];
		tail++;
	}
	text[count] = 0;
	atomic_store(&d->tail, tail);
	return count;
}

// what each decoder has read since the last call, for the thread that owns
// the controls; says how many characters
int cw_rx_take(char *text, int room) {
	return take_from(&decoded_by_tone, text, room);
}

int cw_rx_take_reader(char *text, int room) {
	return take_from(&decoded_by_reader, text, room);
}

static void cw_rx_bin_init(struct bin *p, float freq, int n,
	float sampling_freq){

	p->k = (int) (0.5 + ((n * freq) / sampling_freq));
	p->omega = (2.0 * M_PI * p->k) / n;
	p->sine = sin(p->omega);
	p->cosine = cos(p->omega);
	p->coeff = 2.0 * p->cosine;
	p->n = n;
	p->freq = freq;
	p->scalingFactor = n / 2.0;
}

static int cw_rx_bin_detect(struct bin *p, int32_t *data){
	float Q2 = 0;
	float Q1 = 0;
	for (int index = 0; index < p->n; index++){
		float Q0;
		Q0 = p->coeff * Q1 - Q2 + (float) (*data);
		Q2 = Q1;
		Q1 = Q0;
		data++;
	}
	double real = (Q1 * p->cosine - Q2) / p->scalingFactor;
	double imag = (Q1 * p->sine) / p->scalingFactor;

	int  magnitude = sqrt(real*real + imag*imag);
	return magnitude;
}

static void cw_rx_match_letter(struct cw_decoder *p){
	char code[MAX_SYMBOLS];

	if (p->next_symbol == 0){
		return;
	}

	int in_mark = 0;
	int total_ticks = 0;
	int min_dot = (p->dash_len / 6);
	code[0] = 0;
	int i = 0;

	while(i < p->next_symbol){
		if (p->symbol_str[i].is_mark){
			if(!in_mark && p->symbol_str[i].ticks > min_dot){
				in_mark = 1;
				total_ticks = 0;
			}
		}
		else {
			if(in_mark && p->symbol_str[i].ticks > min_dot){
				in_mark = 0;
				if (total_ticks > p->dash_len / 2){
					strcat(code, "-");
					//track the dashes
					int new_dash = ((p->dash_len * 3) + total_ticks)/4;
					int init_dash_len = (18 * SAMPLING_FREQ) / (5 * N_BINS* p->wpm);
					if (init_dash_len/2 <  new_dash && new_dash < init_dash_len * 2)
						p->dash_len = new_dash;
				}
				else if (min_dot <= total_ticks && total_ticks <= p->dash_len/2)
					strcat(code, ".");
			}
		}
		total_ticks += p->symbol_str[i].ticks;
		i++;
	}

	p->next_symbol = 0;
	for (size_t j = 0; j < sizeof(morse_rx_table)/sizeof(struct morse_rx); j++)
		if (!strcmp(code, morse_rx_table[j].code)){
			cw_rx_say(p, morse_rx_table[j].c);
			return;
		}
	//un-decoded phrases
	cw_rx_say(p, code);
}

static void cw_rx_add_symbol(struct cw_decoder *p, char symbol){
	if (p->next_symbol == MAX_SYMBOLS)
		p->next_symbol = 0;
	p->symbol_str[p->next_symbol].is_mark = 1;
	if (symbol == ' ')
		p->symbol_str[p->next_symbol].is_mark = 0;
	p->symbol_str[p->next_symbol].ticks = p->ticker;
	p->symbol_str[p->next_symbol].magnitude =
		((p->symbol_str[p->next_symbol].magnitude *10) + p->magnitude)/11;
	p->next_symbol++;
}

/*
The highs maybe due to noise (that usually lasts very short durations,
Using large n_bins usually does away with that.
*/
static void cw_rx_update_levels(struct cw_decoder *p){
	int new_high = p->magnitude;

	if (p->high_level < p->magnitude)
		p->high_level = new_high;
	else
		p->high_level = (p->magnitude + ((HIGH_DECAY -1)
			* p->high_level))/HIGH_DECAY;

	if (p->magnitude <  (p->high_level * 4)/10 ){
		// clamp the lows to prevent inf
		if (p->magnitude < LOWEST_LEVEL)
			p->magnitude = LOWEST_LEVEL;
		p->noise_floor = (p->magnitude + ((NOISE_DECAY -1)
			* p->noise_floor))/NOISE_DECAY;
		p->symbol_magnitude += p->magnitude;
	}
}

//we skip the smaller glitches
static void cw_rx_denoise(struct cw_decoder *p){

	p->history_sig <<= 1;
	if (p->sig_state)
		p->history_sig |= 1;

	p->prev_mark = p->mark;
	switch(p->history_sig & HISTORY_LENGTH){
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 8:
		p->mark = 0;
		break;
	default:
		p->mark = MARK_LEVEL;
		break;
	}
}

static void cw_rx_detect_symbol(struct cw_decoder *p){

	if (p->mark == 0 && p->prev_mark > 0){ //end of mark
		cw_rx_add_symbol(p, 'm');
		p->ticker = 0;
	}
	else if (p->mark > 1 && p->prev_mark == 0){ //start of mark
		cw_rx_add_symbol(p, ' ');
		p->ticker = 0;//reset the timer to measure the length of the mark
	}
	else if (p->mark == 0 && p->prev_mark == 0){ //continuing space
		if (p->next_symbol == 0){
			if(p->ticker > (p->dash_len * 3)/2){
				cw_rx_say(p, " ");
				p->ticker = 0;
			}
		}
		else if (p->ticker >  p->dash_len/2){
			cw_rx_add_symbol(p, ' ');
			cw_rx_match_letter(p);
			if (p->ticker > (p->dash_len * 3)/2){
				cw_rx_say(p, " ");
			}
			p->ticker = 0;
		}
	}
	else if (p->mark > 0  && p->prev_mark > 0){	// skip unusually long dashes
		if (p->ticker > p->dash_len * 3)
			p->ticker = p->dash_len;
	}
}

// a tick of the decoder, given how strong the signal is
static void cw_rx_level(struct cw_decoder *p, int sig_now){

	p->magnitude = sig_now;

	if (p->magnitude > (p->high_level * 6)/10){
		p->sig_state = MARK_LEVEL;
	}
	else if (p->magnitude <  (p->high_level * 4)/10 ){
		p->sig_state = 0;
	}

	cw_rx_update_levels(p);
	cw_rx_denoise(p); //this also updates the mark member of struct cw_decode
	cw_rx_detect_symbol(p);
	p->ticker++;
}

static void cw_rx_bin(struct cw_decoder *p, int32_t *samples){
	cw_rx_level(p, cw_rx_bin_detect(&p->signal, samples));
}

// the decoder is started from the speed and the pitch set: its dash is as
// long as the speed's, and it follows the sender's from there
static void cw_rx_init(struct cw_decoder *p, int wpm, int pitch){
	p->ticker = 0;
	p->n_bins = N_BINS;
	p->next_symbol = 0;
	p->sig_state = 0;
	p->magnitude= 0;
	p->prev_mark = 0;
	p->history_sig = 0;
	p->symbol_magnitude = 0;
	p->wpm = wpm;

	// dot len (in msec)) = 1200/wpm; dash len = 3600/wpm
	// each block of nbins = n_bins/sampling seconds;
	// dash len is (3600 / wpm)/ ((nbins * 1000)/samping_freq)
	p->dash_len = (18 * SAMPLING_FREQ) / (5 * N_BINS* wpm);

	cw_rx_bin_init(&p->signal, pitch, N_BINS, SAMPLING_FREQ);
}

// a block of the receiver's audio, MAX_BINS/2 samples at 96000 a second,
// is one tick of the decoder at 12000
static void cw_rx(int32_t *samples, int count){
	int32_t s[N_BINS];
	int wpm = atomic_load(&decoder_wpm);
	int pitch = atomic_load(&pitch_hz);

	if (count != N_BINS * DECIMATION || wpm <= 0)
		return;
	if (wpm != decoder.wpm || pitch != decoder.signal.freq)
		cw_rx_init(&decoder, wpm, pitch);

	//we decimate the samples from 96000 to 12000
	for (int i = 0; i < decoder.n_bins; i++){
		s[i] = samples[i * DECIMATION] >> SAMPLE_SHIFT;
	}
	cw_rx_bin(&decoder, s);
}

// the windowed bin's power, at a place among the block's bins
static float hann_power(complex float *bins, int k){
	complex float below = bins[(k + MAX_BINS - 1) % MAX_BINS];
	complex float above = bins[(k + 1) % MAX_BINS];
	float magnitude = cabsf(HANN_MIDDLE * bins[k % MAX_BINS] - HANN_SIDE * (below + above));

	return magnitude * magnitude;
}

// the reader's tick: the ratio to the grass of the bins around the pitch;
// what it reads goes into its ring
static void cw_rx_reader(complex float *bins){
	float power[GRASS_COUNT];
	float sorted[GRASS_COUNT];
	float ratio[CW_READER_BINS];
	char text[CW_READER_TEXT_ROOM];
	int wpm = atomic_load(&decoder_wpm);
	int middle;
	float grass;
	float held;
	int i;
	int j;

	if (wpm <= 0)
		return;
	if (reader_wpm == 0)
		cw_reader_init(&reader, wpm, SECONDS_A_TICK);
	if (wpm != reader_wpm){
		cw_reader_set_wpm(&reader, wpm);
		reader_wpm = wpm;
	}
	middle = (int)lroundf(bin_of_audio(atomic_load(&pitch_hz))) + MAX_BINS;
	for (i = 0; i < GRASS_COUNT; i++){
		power[i] = hann_power(bins, middle - GRASS_BINS + i);
		sorted[i] = power[i];
	}
	//enough of a sort to find the percentile: the least few, in order
	for (i = 0; i <= GRASS_COUNT / GRASS_PERCENTILE; i++){
		for (j = i + 1; j < GRASS_COUNT; j++){
			if (sorted[j] < sorted[i]){
				held = sorted[i];
				sorted[i] = sorted[j];
				sorted[j] = held;
			}
		}
	}
	grass = sorted[GRASS_COUNT / GRASS_PERCENTILE];
	if (grass <= 0)
		return;
	for (i = 0; i < CW_READER_BINS; i++)
		ratio[i] = power[GRASS_BINS - CW_READER_SPAN - 1 + i] / grass;
	cw_reader_tick(&reader, ratio);
	if (cw_reader_take(&reader, text, sizeof(text)) > 0)
		say_into(&decoded_by_reader, text);
}

static void rx_cw(complex float *bins) {
	demod_ssb(bins, baseband_output);
	cw_rx(baseband_output, MAX_BINS/2);
	cw_rx_reader(bins);
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
	if (cw_text_is_busy())
		return 1;
	return atomic_load(&is_wanting_tx);
}

static void cw_reset(void) {
	cw_keyer_reset();
	cw_text_reset();
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
	cw_text_set_wpm(atoi(value));
	atomic_store(&decoder_wpm, atoi(value));
	return 0;
}

// a macro is sent: its text, with the station's callsign and the rest put
// in, goes to be keyed. Only in CW, or it would wait and be sent when CW was
// next chosen
static int apply_macro(struct control *c, const char *value, char *why, int room) {
	const char *mode = control_get("MODE");
	char text[MACRO_MAX_TEXT];

	(void)c;
	if (mode == NULL || (strcmp(mode, "CW") && strcmp(mode, "CWR"))) {
		snprintf(why, room, "macros are sent in CW");
		return -1;
	}
	if (macro_exec(macro_key(value), text, sizeof(text)) < 0) {
		snprintf(why, room, "no such macro");
		return -1;
	}
	atomic_store(&is_text_to_stop, 0);
	cw_text_put(text);
	return 0;
}

// the text being sent is stopped and what was waiting is dropped. With no
// text being sent there is nothing to do, and no word is left to stop the
// next one
static int apply_cw_do(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)value;
	(void)why;
	(void)room;
	if (cw_text_is_busy())
		atomic_store(&is_text_to_stop, 1);
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

static struct control cw_do = {
	.group = "CW", .name = "CW_DO", .kind = CONTROL_ACTION,
	.choices = "ESC", .apply = apply_cw_do,
};

static char macro_choices[MACRO_CHOICES_MAX];

static struct control macro = {
	.group = "CW", .name = "MACRO", .kind = CONTROL_ACTION,
	.choices = macro_choices, .apply = apply_macro,
};

// the macros are read from data/cw.mc; with no such file there are none
static void load_the_macros(void) {
	if (macro_load(MACRO_FILE) < 0) {
		printf("no CW macros: data/%s.mc is not there\n", MACRO_FILE);
		return;
	}
	macro_labels(macro_choices, sizeof(macro_choices));
	if (macro_choices[0] != 0)
		control_register(&macro);
}

static void cw_start(void) {
	control_register(&wpm);
	control_register(&pitch);
	control_register(&bw_cw);
	control_register(&cw_input);
	control_register(&sidetone_control);
	control_register(&cw_delay);
	control_register(&cw_do);
	apply_cw_delay(&cw_delay, cw_delay.value, NULL, 0);
	atomic_store(&pitch_hz, atoi(pitch.value));
	atomic_store(&sidetone_level, atoi(sidetone_control.value));
	cw_keyer_reset();
	cw_keyer_set_wpm(atoi(wpm.value));
	cw_text_set_wpm(atoi(wpm.value));
	atomic_store(&decoder_wpm, atoi(wpm.value));
	apply_cw_input(&cw_input, cw_input.value, NULL, 0);
	load_the_macros();
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
