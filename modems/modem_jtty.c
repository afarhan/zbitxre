// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_jtty.c — JTTY: text typed and read, as WSJT-X sends it.
//
// JTTY is WSJT-X's mode for typed text, from version 3.2: a chat, or an
// RTTY contest's exchanges, with the error correction of FT8's family.
// Four tones 31.25 Hz apart, 127 Hz in all, make a frame of 1.888 seconds,
// which carries five characters of text, or a callsign, an exchange or a
// phrase of the contests packed into as many bits. A message is one to
// sixteen frames, sent one after the other, the last marked as the end.
// There are no slots: an over begins when the operator sends it. The
// coding is libjtty's, in modems/jtty_lib; its README says whose it is and
// how near to WSJT-X's it reads.
//
// JTTY is heard as FT8 is, 50 to 4000 Hz above the dial, and goes to the
// speaker as USB does.
//
// THE BINS
//
// The author's plan, of 23 October 2026: the passband from 200 to 3200 Hz
// is cut into JTTY_BINS bins of 125 Hz, a signal's four tones and the
// space a signal needs either side, and what is read in each bin is kept,
// so that a list shows what each station is saying, a bin to a row. A
// frame goes to the bin its lowest tone is in, which is where WSJT-X puts
// a signal's frequency. A bin keeps the last JTTY_BIN_TEXT_KEPT characters
// of what has been read in it, each frame's text after the one before,
// which is how a message is put together; a callsign or a phrase packed
// whole is followed by a space, as libjtty's jtty_unpack() does. The end of
// a message is shown as BIN_SEPARATOR, and so is a silence of NEW_OVER_S in
// the bin, when a message's last frame was not read. What this radio sends
// goes into the bin it is sent in, in small letters, so that both sides of
// a chat are read in one place. A message stopped with JTTY_DO ESC is
// followed there by "(stopped)", since it was not sent, or not all of it.
//
// The radio program asks for the bins that have changed, with
// jtty_poll_bins(), and sends each to the clients as a line; server.c
// says how. jtty_resend_bins() marks them all changed, for a client that
// has just come and has none.
//
// The front panel has no list of the bins: it is given the text of the
// chosen bin, the one JTTY_RX is in, as it comes, and runs it along its
// status line as it does CW's. jtty_take_chosen_text() gives what has been
// added to that bin since it was last asked; when another bin is chosen,
// all of that bin's text comes first. panel_cw.c sends it.
//
// Each frame is also printed on the terminal as it is read:
//
//     jtty: 1507 Hz  RAN A
//     jtty: 1507 Hz  S!     (end)
//
// THE THREAD
//
// Finding frames in the sound is too much for the audio thread, which has
// 10 ms a block, so it is done by a thread of its own, as modem.h asks.
// The audio thread keeps every eighth sample, making the 12000 a second
// the library reads, as FT8's does, and puts them in a ring; every
// CHUNK_SAMPLES, a quarter of a second, it wakes the thread, which reads
// what has come and gives it to the library's receiver, jtty_rx_process().
// The ring is written only by the audio thread and read only by the
// decoder, and nothing is locked between them: each keeps its own count of
// the samples, and the audio thread's is read with an atomic load. A
// decoder that falls more than the ring behind leaves out what it missed.
// The receiver searches for a signal's lowest tone from SEARCH_LOWEST_HZ to
// SEARCH_HIGHEST_HZ, the bins' span. reset(), which the audio thread calls
// when the mode is taken up, asks the thread to begin the stream again.
//
// SENDING
//
// The operator's text comes as the action JTTY_TEXT, a line at a time, at
// most a control's value long, 63 characters; a client cuts a longer line
// into pieces. Up to PENDING_MOST lines wait their turn, so that what is
// typed while a message is going out follows it. wants_tx(), which the
// thread that owns the controls asks many times a second, takes the next
// line when nothing is being sent: it is packed into frames by the
// library, jtty_pack() and jtty_encode(), and made into sound at the sound
// card's rate by ft8_signal.c's synth_gfsk(), JTTY's tones gliding as
// FT8's do, after TX_LEAD_S of silence for the radio to have gone over
// to transmit. The audio thread then plays it out, at FT8's level, and is
// heard in the speaker at JTTY_SIDETONE's level. The radio stays in
// transmit while a line is waiting, so that lines typed one after another
// go in one over. JTTY_DO ESC stops what is being sent and drops what
// waits. Which thread has the sound is in tx_state, as in modem_ft8.c,
// which says why.
//
// WHERE IT IS SENT
//
// JTTY sets no rule: every receiver searches the whole passband. But
// WSJT-X shows the station being worked from a few tens of Hz around its
// own receiving frequency, and the others only in its list of the band,
// so a reply is best sent where the other station is. The controls:
//
//   JTTY_RX        the station being chatted with, as the lowest tone's
//                  pitch in Hz; a client sets it when a bin is chosen.
//   JTTY_TX        where this radio sends, the lowest tone's pitch.
//   JTTY_SPLIT     OFF, the author's default: JTTY_TX follows JTTY_RX,
//                  and the reply goes where the other station is. ON:
//                  JTTY_TX stays where it was set, for a pile-up.
//   JTTY_SIDETONE  how loud what is sent is in the speaker, 0 to 100.
//   JTTY_TEXT      a line to send.
//   JTTY_DO        ESC: stop sending, and drop what waits.
//
// JTTY_SPLIT is registered before JTTY_RX and JTTY_TX, so that the
// settings, which are read in the order of the controls, have it in place
// before JTTY_RX moves JTTY_TX, or does not.

#include <ctype.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"
#include "ft8_signal.h"
#include "jtty_lib/jtty.h"
#include "modem_jtty.h"

#define JTTY_LOW_HZ 50
#define JTTY_HIGH_HZ 4000
#define SOUND_CARD_RATE 96000
#define DECIMATION (SOUND_CARD_RATE / JTTY_SAMPLE_RATE)
// FT8's scaling of the receiver's samples for its decoder
#define JTTY_FULL_SCALE 200000000.0f
#define RING_SAMPLES 65536
#define CHUNK_SAMPLES 3072
#define BIN_LOWEST_HZ 200
#define BIN_WIDTH_HZ 125
#define SEARCH_LOWEST_HZ 200.0
#define SEARCH_HIGHEST_HZ 3199.0
#define CANDIDATES_MOST 256
#define DECODES_A_CHUNK 128
#define JTTY_BIN_TEXT_KEPT (JTTY_BIN_TEXT_ROOM - 1)
#define BIN_SEPARATOR " | "
#define NEW_OVER_S 4.0
#define NS_PER_S 1000000000.0
#define TONES_MOST (JTTY_MAX_FRAMES * JTTY_SYMBOLS)
#define SYMBOL_SECONDS ((float)JTTY_SPS / JTTY_SAMPLE_RATE)
#define SYMBOL_BT 2.0f
#define SAMPLES_A_SYMBOL (JTTY_SPS * DECIMATION)
#define TX_LEAD_S 0.25
#define TX_LEAD_SAMPLES ((int)(TX_LEAD_S * SOUND_CARD_RATE))
#define TX_WAVE_ROOM (TX_LEAD_SAMPLES + TONES_MOST * SAMPLES_A_SYMBOL)
// FT8's level: a seventh of full scale
#define TX_DIVISOR 7.0
// the speaker's peak with the sidetone at its loudest, as CW's
#define SIDETONE_PEAK 150000000.0
#define SIDETONE_HIGHEST 100
#define PENDING_MOST 3
#define PITCH_LOWEST 200
#define PITCH_HIGHEST 3100
#define TEXT_ROOM (JTTY_BIN_TEXT_ROOM)
#define CHOSEN_TEXT_KEPT 160
#define CHOSEN_BEGINS "  "
// a frame's text and the space after it
#define FRAME_TEXT_ROOM (TEXT_ROOM + 1)

enum tx_state {
	TX_IDLE,
	TX_ARMED,
	TX_SENDING,
};

struct bin {
	int hz;
	double heard_at;
	int is_changed;
	char text[JTTY_BIN_TEXT_ROOM];
};

static int32_t baseband_output[MAX_BINS/2];

static float ring[RING_SAMPLES];
static atomic_ulong ring_written;
static unsigned long written_at_wake;
static sem_t chunk_is_ready;
static atomic_int is_reset_asked;
static pthread_t jtty_thread;
static jtty_rx *receiver;
static atomic_int is_working;

static struct bin bins[JTTY_BINS];
static pthread_mutex_t bins_lock = PTHREAD_MUTEX_INITIALIZER;
// the chosen bin, and what has been added to it and not yet taken; both
// under bins_lock
static int chosen_bin;
static char chosen_new[CHOSEN_TEXT_KEPT + 1];

static float *tx_wave;
static int tx_wave_samples;
static long tx_pos;
static atomic_int tx_state;
static atomic_int tx_hz;
static atomic_int sidetone_level;
static int32_t sidetone_output[MAX_BINS/2];
static char pending[PENDING_MOST][CONTROL_VALUE_MAX];
static int pending_count;

static double seconds_now(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + now.tv_nsec / NS_PER_S;
}

static int bin_of(int hz) {
	int bin = (hz - BIN_LOWEST_HZ) / BIN_WIDTH_HZ;

	if (bin < 0)
		bin = 0;
	if (bin > JTTY_BINS - 1)
		bin = JTTY_BINS - 1;
	return bin;
}

// the newest characters are kept, the oldest let go to make room
static void append_text(struct bin *b, const char *text) {
	size_t had = strlen(b->text);
	size_t adding = strlen(text);
	size_t dropped;

	if (adding > JTTY_BIN_TEXT_KEPT)
		text += adding - JTTY_BIN_TEXT_KEPT;
	adding = strlen(text);
	if (had + adding > JTTY_BIN_TEXT_KEPT) {
		dropped = had + adding - JTTY_BIN_TEXT_KEPT;
		memmove(b->text, b->text + dropped, had - dropped + 1);
		had -= dropped;
	}
	memcpy(b->text + had, text, adding + 1);
}

// the newest of the chosen bin's text, kept until it is taken
static void append_chosen(const char *text) {
	size_t had = strlen(chosen_new);
	size_t adding = strlen(text);

	if (adding > CHOSEN_TEXT_KEPT)
		text += adding - CHOSEN_TEXT_KEPT;
	adding = strlen(text);
	if (had + adding > CHOSEN_TEXT_KEPT) {
		memmove(chosen_new, chosen_new + had + adding - CHOSEN_TEXT_KEPT,
			CHOSEN_TEXT_KEPT - adding + 1);
		had = CHOSEN_TEXT_KEPT - adding;
	}
	memcpy(chosen_new + had, text, adding + 1);
}

// text added to a bin, and to what the panel is to be given if the bin is
// the chosen one
static void append_to(struct bin *b, const char *text) {
	append_text(b, text);
	if (b - bins == chosen_bin)
		append_chosen(text);
}

static int ends_with(const char *text, const char *end) {
	size_t length = strlen(text);
	size_t end_length = strlen(end);

	return length >= end_length && !strcmp(text + length - end_length, end);
}

// a message ended, or a silence that says one did: the separator, once
static void end_message(struct bin *b) {
	size_t length = strlen(b->text);

	while (length > 0 && b->text[length - 1] == ' ')
		b->text[--length] = 0;
	if (length > 0 && !ends_with(b->text, BIN_SEPARATOR))
		append_to(b, BIN_SEPARATOR);
}

// what is read or sent at a pitch goes to its bin; a bin long silent
// begins a new message. A bin's pitch is the station's heard in it, so that
// a bin chosen is listened to there; what this radio sends gives the bin
// its pitch only when nothing has been heard in it
static void add_to_bin(int hz, const char *text, int is_end, int is_heard) {
	struct bin *b = &bins[bin_of(hz)];
	double now = seconds_now();

	pthread_mutex_lock(&bins_lock);
	if (b->heard_at > 0 && now - b->heard_at > NEW_OVER_S)
		end_message(b);
	append_to(b, text);
	if (is_end)
		end_message(b);
	if (is_heard || b->hz == 0)
		b->hz = hz;
	b->heard_at = now;
	b->is_changed = 1;
	pthread_mutex_unlock(&bins_lock);
}

// a frame read: its text, and a space after a callsign or a phrase that
// was packed whole, which five characters of text do not have
static void take_frame(const jtty_frame *frame, void *user) {
	char text[FRAME_TEXT_ROOM];
	jtty_atom atom;
	int eom;

	(void)user;
	snprintf(text, sizeof(text), "%s", frame->text);
	if (jtty_atom_unpack(frame->payload, &atom, &eom) == 0 && atom.kind != JTTY_TEXT5)
		snprintf(text, sizeof(text), "%s ", frame->text);
	if (frame->eom)
		printf("jtty: %4.0f Hz  %s  (end)\n", frame->frequency, frame->text);
	else
		printf("jtty: %4.0f Hz  %s\n", frame->frequency, frame->text);
	fflush(stdout);
	add_to_bin((int)(frame->frequency + 0.5), text, frame->eom, 1);
}

static void *jtty_thread_function(void *ptr) {
	static float chunk[CHUNK_SAMPLES];
	unsigned long read_up_to = 0;
	unsigned long written;
	int count;
	int i;

	(void)ptr;
	while (1) {
		sem_wait(&chunk_is_ready);
		written = atomic_load(&ring_written);
		if (atomic_exchange(&is_reset_asked, 0)) {
			jtty_rx_reset(receiver);
			read_up_to = written;
		}
		if (written - read_up_to > RING_SAMPLES)
			read_up_to = written - RING_SAMPLES;
		while (read_up_to < written) {
			count = CHUNK_SAMPLES;
			if (written - read_up_to < (unsigned long)count)
				count = (int)(written - read_up_to);
			for (i = 0; i < count; i++)
				chunk[i] = ring[(read_up_to + i) % RING_SAMPLES];
			read_up_to += count;
			jtty_rx_process(receiver, chunk, count, take_frame, NULL);
		}
	}
	return NULL;
}

// every eighth sample to the ring, and the decoder woken each chunk
static void keep_for_the_decoder(const int32_t *samples, int count) {
	unsigned long written = atomic_load(&ring_written);
	int i;

	for (i = 0; i < count; i += DECIMATION) {
		ring[written % RING_SAMPLES] = samples[i] / JTTY_FULL_SCALE;
		written++;
	}
	atomic_store(&ring_written, written);
	if (written - written_at_wake >= CHUNK_SAMPLES) {
		written_at_wake = written;
		sem_post(&chunk_is_ready);
	}
}

static void rx_jtty(complex float *bins_in) {
	demod_ssb(bins_in, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
	if (atomic_load(&is_working))
		keep_for_the_decoder(baseband_output, MAX_BINS/2);
}

static void jtty_reset(void) {
	if (!atomic_load(&is_working))
		return;
	atomic_store(&is_reset_asked, 1);
	sem_post(&chunk_is_ready);
}

static void jtty_select(void) {
	set_passband(JTTY_LOW_HZ, JTTY_HIGH_HZ);
}

/* the bins, for the radio program */

void jtty_poll_bins(void (*tell)(int bin, int hz, const char *text)) {
	char text[JTTY_BIN_TEXT_ROOM];
	int is_changed;
	int hz;
	int i;

	for (i = 0; i < JTTY_BINS; i++) {
		pthread_mutex_lock(&bins_lock);
		is_changed = bins[i].is_changed;
		bins[i].is_changed = 0;
		hz = bins[i].hz;
		snprintf(text, sizeof(text), "%s", bins[i].text);
		pthread_mutex_unlock(&bins_lock);
		if (is_changed)
			tell(i, hz, text);
	}
}

int jtty_take_chosen_text(char *text, int room) {
	int count;

	pthread_mutex_lock(&bins_lock);
	snprintf(text, room, "%s", chosen_new);
	chosen_new[0] = 0;
	pthread_mutex_unlock(&bins_lock);
	count = (int)strlen(text);
	return count;
}

void jtty_resend_bins(void) {
	int i;

	pthread_mutex_lock(&bins_lock);
	for (i = 0; i < JTTY_BINS; i++) {
		if (bins[i].text[0] != 0)
			bins[i].is_changed = 1;
	}
	pthread_mutex_unlock(&bins_lock);
}

/* sending */

// the next line waiting is made into sound, and is in its bin in small
// letters; 0, or -1 if there was none or it could not be packed
static int arm_the_next(void) {
	static uint8_t tones[TONES_MOST];
	uint64_t frames[JTTY_MAX_FRAMES];
	char sent[TEXT_ROOM];
	char text[CONTROL_VALUE_MAX];
	int count;
	int pitch = atomic_load(&tx_hz);
	int i;

	if (pending_count == 0)
		return -1;
	snprintf(text, sizeof(text), "%s", pending[0]);
	pending_count--;
	memmove(pending[0], pending[1], (size_t)pending_count * sizeof(pending[0]));
	count = jtty_pack(text, JTTY_UNKNOWN, frames, sent);
	if (count <= 0)
		return -1;
	for (i = 0; i < count; i++) {
		if (jtty_encode(frames[i], tones + i * JTTY_SYMBOLS) != 0)
			return -1;
	}
	memset(tx_wave, 0, TX_LEAD_SAMPLES * sizeof(tx_wave[0]));
	synth_gfsk(tones, count * JTTY_SYMBOLS, (float)pitch, SYMBOL_BT, SYMBOL_SECONDS,
		SOUND_CARD_RATE, tx_wave + TX_LEAD_SAMPLES);
	tx_wave_samples = TX_LEAD_SAMPLES + count * JTTY_SYMBOLS * SAMPLES_A_SYMBOL;
	printf("jtty: sending at %d Hz, %d frames: %s\n", pitch, count, sent);
	fflush(stdout);
	for (i = 0; sent[i] != 0; i++)
		sent[i] = (char)tolower((unsigned char)sent[i]);
	add_to_bin(pitch, sent, 1, 0);
	atomic_store(&tx_state, TX_ARMED);
	return 0;
}

static int jtty_wants_tx(void) {
	if (atomic_load(&tx_state) != TX_IDLE)
		return 1;
	while (pending_count > 0) {
		if (arm_the_next() == 0)
			return 1;
	}
	return 0;
}

static void tx_jtty(const int32_t *mic, double *samples) {
	double peak = SIDETONE_PEAK * atomic_load(&sidetone_level) / SIDETONE_HIGHEST;
	int armed = TX_ARMED;
	int i;

	(void)mic;
	memset(sidetone_output, 0, sizeof(sidetone_output));
	if (atomic_compare_exchange_strong(&tx_state, &armed, TX_SENDING))
		tx_pos = 0;
	if (atomic_load(&tx_state) != TX_SENDING) {
		memset(samples, 0, MAX_BINS/2 * sizeof(samples[0]));
		speaker_play(sidetone_output, MAX_BINS/2);
		return;
	}
	for (i = 0; i < MAX_BINS/2; i++) {
		samples[i] = 0;
		if (tx_pos < tx_wave_samples) {
			samples[i] = tx_wave[tx_pos] / TX_DIVISOR;
			sidetone_output[i] = (int32_t)(tx_wave[tx_pos] * peak);
		}
		tx_pos++;
	}
	speaker_play(sidetone_output, MAX_BINS/2);
	if (tx_pos >= tx_wave_samples)
		atomic_store(&tx_state, TX_IDLE);
}

/* the controls */

static int apply_split(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	if (!strcmp(value, "OFF")) {
		atomic_store(&tx_hz, control_get_int("JTTY_RX"));
		control_update("JTTY_TX", control_get("JTTY_RX"));
	}
	return 0;
}

// another bin chosen: what the panel shows begins again, with all of it
static void choose_bin(int bin) {
	pthread_mutex_lock(&bins_lock);
	if (bin != chosen_bin) {
		chosen_bin = bin;
		snprintf(chosen_new, sizeof(chosen_new), "%s%s", CHOSEN_BEGINS, bins[bin].text);
	}
	pthread_mutex_unlock(&bins_lock);
}

static int apply_rx(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	choose_bin(bin_of(atoi(value)));
	if (strcmp(control_get("JTTY_SPLIT"), "ON")) {
		atomic_store(&tx_hz, atoi(value));
		control_update("JTTY_TX", value);
	}
	return 0;
}

static int apply_tx(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	atomic_store(&tx_hz, atoi(value));
	return 0;
}

static int apply_sidetone(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	atomic_store(&sidetone_level, atoi(value));
	return 0;
}

static int apply_text(struct control *c, const char *value, char *why, int room) {
	uint64_t frames[JTTY_MAX_FRAMES];
	char normalized[TEXT_ROOM];

	(void)c;
	if (!atomic_load(&is_working)) {
		snprintf(why, room, "JTTY could not be started: see the terminal");
		return -1;
	}
	if (pending_count >= PENDING_MOST) {
		snprintf(why, room, "%d messages are waiting to be sent already", PENDING_MOST);
		return -1;
	}
	if (jtty_pack(value, JTTY_UNKNOWN, frames, normalized) <= 0) {
		snprintf(why, room, "nothing in that can be sent");
		return -1;
	}
	snprintf(pending[pending_count], sizeof(pending[0]), "%s", value);
	pending_count++;
	return 0;
}

static int apply_do(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)value;
	(void)why;
	(void)room;
	pending_count = 0;
	if (atomic_exchange(&tx_state, TX_IDLE) != TX_IDLE)
		add_to_bin(atomic_load(&tx_hz), "(stopped)", 1, 0);
	return 0;
}

static struct control split_control = {
	.group = "JTTY", .name = "JTTY_SPLIT", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON", .value = "OFF", .apply = apply_split,
};

static struct control rx_control = {
	.group = "JTTY", .name = "JTTY_RX", .kind = CONTROL_NUMBER,
	.min = PITCH_LOWEST, .max = PITCH_HIGHEST, .step = 1,
	.value = "1500", .apply = apply_rx,
};

static struct control tx_control = {
	.group = "JTTY", .name = "JTTY_TX", .kind = CONTROL_NUMBER,
	.min = PITCH_LOWEST, .max = PITCH_HIGHEST, .step = 1,
	.value = "1500", .apply = apply_tx,
};

static struct control sidetone_control = {
	.group = "JTTY", .name = "JTTY_SIDETONE", .kind = CONTROL_NUMBER,
	.min = 0, .max = SIDETONE_HIGHEST, .step = 1,
	.value = "30", .apply = apply_sidetone,
};

static struct control text_control = {
	.group = "JTTY", .name = "JTTY_TEXT", .kind = CONTROL_ACTION,
	.choices = "", .apply = apply_text,
};

static struct control do_control = {
	.group = "JTTY", .name = "JTTY_DO", .kind = CONTROL_ACTION,
	.choices = "ESC", .apply = apply_do,
};

static void jtty_start(void) {
	jtty_rx_config config = {
		.fmin = SEARCH_LOWEST_HZ,
		.fmax = SEARCH_HIGHEST_HZ,
		.candidate_capacity = CANDIDATES_MOST,
		.decode_budget = DECODES_A_CHUNK,
	};

	control_register(&split_control);
	control_register(&rx_control);
	control_register(&tx_control);
	control_register(&sidetone_control);
	control_register(&text_control);
	control_register(&do_control);
	atomic_store(&tx_hz, atoi(tx_control.value));
	chosen_bin = bin_of(atoi(rx_control.value));
	atomic_store(&sidetone_level, atoi(sidetone_control.value));
	tx_wave = malloc(TX_WAVE_ROOM * sizeof(tx_wave[0]));
	receiver = jtty_rx_create(&config);
	if (receiver == NULL || tx_wave == NULL) {
		printf("jtty: there is not the memory for it\n");
		return;
	}
	sem_init(&chunk_is_ready, 0, 0);
	pthread_create(&jtty_thread, NULL, jtty_thread_function, NULL);
	atomic_store(&is_working, 1);
}

const struct modem modem_jtty = {
	.name = "JTTY",
	.order = 7,
	.start = jtty_start,
	.select = jtty_select,
	.reset = jtty_reset,
	.rx = rx_jtty,
	.tx = tx_jtty,
	.wants_tx = jtty_wants_tx,
};
