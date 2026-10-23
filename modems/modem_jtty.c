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
// A filler frame, sent while a station typing live has no whole word to
// send, is a space, and a run of spaces is one, as WSJT-X shows them.
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
// What is sent goes a frame at a time. The frames wait in a queue, and two
// buffers each hold the sound of one: while the audio thread plays one
// out, wants_tx(), which the thread that owns the controls asks many times
// a second, makes the next into the other, with ft8_signal.c's
// synth_gfsk() at the sound card's rate, JTTY's tones gliding as FT8's do.
// It makes no more than one frame ahead, so that what is typed live is
// never long behind. The first frame of an over has TX_LEAD_S of silence
// before it, for the radio to have gone over to transmit. Each frame rises
// and falls at its ends, and a receiver reads each frame on its own, by
// its own synchronising tones. The audio thread plays at FT8's level,
// heard in the speaker at JTTY_SIDETONE's. As each frame is made its text
// goes into the bin it is sent in, in small letters, so that both sides
// of a chat are read in one place and are seen to go as they go. Which
// thread has a buffer is in its state, EMPTY, READY or PLAYING, changed
// with atomic operations, as modem_ft8.c's tx_state is, which says why.
//
// Text comes to be sent in two ways.
//
// A whole line, JTTY_TEXT, at most a control's value long, 63 characters,
// as the web page's Enter sends it: it is packed into the fewest frames by
// the library, jtty_pack(), which uses the compact forms of callsigns,
// exchanges and phrases, and its frames are put in the queue, up to
// PENDING_MOST lines, so that what is typed while a line goes follows it
// in the same over.
//
// Live, as CW's KBD sends, the author's design of 23 October 2026: the
// window keeps what is typed in a box, and while its PLAY is on gives it to
// the radio, JTTY_LIVE, with each character after as it is struck, "~" for
// a space, since a value cannot end in one. The first begins an over. It is
// WSJT-X 3.3's live entry: whenever a frame is wanted, what has been typed
// up to its last space, whole words, is taken, five characters to a frame
// of plain text, JTTY_TEXT5, the last of them filled out with spaces; a
// word still being typed waits, and JTTY_DO RUBOUT takes back its last
// character. While no whole word is waiting, a filler frame, FILLER,
// "<<<<<", is sent, which WSJT-X and this radio's receiver take for a
// space; spaces alone waiting go as a filler too. So a
// frame never splits a word, and the filler falls only between words,
// where WSJT-X reads it as a space. When nothing has been typed for
// LIVE_IDLE_S, what is left goes, the last frame marked as the end of the
// message, a filler if nothing was left, and the radio goes back to
// receive: "if the typing is paused, it goes back to rx. if i resume
// typing, it starts to transmit again". A live message that has gone on
// for LIVE_MOST_S is ended in the same way, before the radio's own limit
// on transmitting. Live text goes at most five characters a frame, some
// 32 words a minute, and without the compact forms. A line, JTTY_TEXT, is
// refused while a live message is going.
//
// As each live frame is made, the characters it takes from what was typed
// are kept for jtty_take_sent(), and the radio program tells the clients,
// JTTY_SENT, so that the window takes them off the front of its box, as
// CW_SENT does in CW: the box always holds what has still to go.
//
// JTTY_DO ESC, the window's PAUSE, stops what is being sent at once and
// drops what waits; the window keeps in its box what had not gone, for the
// next PLAY. The bin says "(stopped)".
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
//   JTTY_LIVE      characters typed, to go live.
//   JTTY_DO        ESC: stop sending, and drop what waits. RUBOUT: take
//                  back the last character typed live, if it has not
//                  gone.
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
#define SYMBOL_SECONDS ((float)JTTY_SPS / JTTY_SAMPLE_RATE)
#define SYMBOL_BT 2.0f
#define SAMPLES_A_SYMBOL (JTTY_SPS * DECIMATION)
#define TX_LEAD_S 0.25
#define TX_LEAD_SAMPLES ((int)(TX_LEAD_S * SOUND_CARD_RATE))
#define SLOTS 2
#define SLOT_ROOM (TX_LEAD_SAMPLES + JTTY_SYMBOLS * SAMPLES_A_SYMBOL)
#define FRAMES_QUEUED_MOST (PENDING_MOST * JTTY_MAX_FRAMES)
#define LIVE_ROOM 240
#define TEXT5_LENGTH 5
#define FILLER "<<<<<"
#define SPACE_TYPED '~'
#define LIVE_IDLE_S 5.0
#define LIVE_MOST_S 100.0
#define SENT_ROOM 256
// FT8's level: a seventh of full scale
#define TX_DIVISOR 7.0
// the speaker's peak with the sidetone at its loudest, as CW's
#define SIDETONE_PEAK 150000000.0
#define SIDETONE_HIGHEST 100
#define PENDING_MOST 3
#define PITCH_LOWEST 200
#define PITCH_HIGHEST 3100
// the author's step for both, 23 October 2026; a station chosen in a bin
// is listened to at the step nearest it, which JTTY's receivers take in
// their stride, WSJT-X looking 50 Hz either side
#define PITCH_STEP 25
#define TEXT_ROOM (JTTY_BIN_TEXT_ROOM)
#define CHOSEN_TEXT_KEPT 160
#define CHOSEN_BEGINS "  "
// a frame's text and the space after it
#define FRAME_TEXT_ROOM (TEXT_ROOM + 1)

enum slot_state {
	SLOT_EMPTY,
	SLOT_READY,
	SLOT_PLAYING,
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

static atomic_int tx_hz;
static atomic_int sidetone_level;
static int32_t sidetone_output[MAX_BINS/2];
// the two buffers, filled by the thread that owns the controls, in turn,
// and played by the audio thread in the same turn
static float *slot_wave[SLOTS];
static int slot_samples[SLOTS];
static atomic_int slot_state[SLOTS];
static unsigned long slots_filled;
static atomic_ulong slots_played;
static long slot_pos;
// for the thread that owns the controls: the frames waiting, what is being
// typed live, and the characters of it sent, for the clients
static uint64_t queued[FRAMES_QUEUED_MOST];
static int queued_count;
static char live[LIVE_ROOM + 1];
static int is_live;
static double live_began;
static double live_typed_at;
static char sent_new[SENT_ROOM + 1];

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

// the newest characters are kept, the oldest let go to make room; a run
// of spaces is one
static void append_text(struct bin *b, const char *given) {
	char text[FRAME_TEXT_ROOM];
	size_t had = strlen(b->text);
	size_t adding = 0;
	size_t dropped;
	char before = 0;

	if (had > 0)
		before = b->text[had - 1];
	for (; *given != 0 && adding < sizeof(text) - 1; given++) {
		if (*given == ' ' && before == ' ')
			continue;
		text[adding++] = *given;
		before = *given;
	}
	text[adding] = 0;
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

	if (ends_with(b->text, BIN_SEPARATOR))
		return;
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
	if (!strcmp(frame->text, FILLER))
		snprintf(text, sizeof(text), " ");
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

static int is_sending(void) {
	int i;

	for (i = 0; i < SLOTS; i++) {
		if (atomic_load(&slot_state[i]) != SLOT_EMPTY)
			return 1;
	}
	return 0;
}

static void queue_frame(uint64_t payload) {
	if (queued_count < FRAMES_QUEUED_MOST)
		queued[queued_count++] = payload;
}

// a frame of five characters of plain text, the live one's
static int queue_text5(const char *five, int is_end) {
	jtty_atom atom;
	uint64_t payload;

	memset(&atom, 0, sizeof(atom));
	atom.kind = JTTY_TEXT5;
	snprintf(atom.text, sizeof(atom.text), "%.*s", TEXT5_LENGTH, five);
	if (jtty_atom_pack(&atom, is_end, &payload) != 0)
		return -1;
	queue_frame(payload);
	return 0;
}

// characters a live frame took from what was typed, for the clients
static void note_sent(const char *text, int count) {
	size_t had = strlen(sent_new);

	if (had + count > SENT_ROOM)
		return;
	memcpy(sent_new + had, text, count);
	sent_new[had + count] = 0;
}

// the next live frame: the whole words typed, five characters at a time, a
// filler while there are none, and the end when typing has stopped
static void take_live_frame(void) {
	char five[TEXT5_LENGTH + 1];
	double now = seconds_now();
	int is_idle = now - live_typed_at > LIVE_IDLE_S || now - live_began > LIVE_MOST_S;
	int whole = 0;
	size_t count;
	int is_end;
	int i;

	if (!is_live)
		return;
	for (i = 0; live[i] != 0; i++) {
		if (live[i] == ' ')
			whole = i + 1;
	}
	if (is_idle)
		whole = (int)strlen(live);
	// spaces alone are not worth a frame: a filler is a space to a receiver
	for (i = 0; i < whole && live[i] == ' '; i++)
		;
	if (i == whole && whole > 0) {
		note_sent(live, whole);
		memmove(live, live + whole, strlen(live) - whole + 1);
		whole = 0;
	}
	if (whole == 0) {
		is_end = is_idle;
		queue_text5(FILLER, is_end);
		if (is_end)
			is_live = 0;
		return;
	}
	count = (size_t)whole;
	if (count > TEXT5_LENGTH)
		count = TEXT5_LENGTH;
	memset(five, ' ', TEXT5_LENGTH);
	five[TEXT5_LENGTH] = 0;
	memcpy(five, live, count);
	is_end = is_idle && count == strlen(live);
	queue_text5(five, is_end);
	note_sent(live, (int)count);
	memmove(live, live + count, strlen(live) - count + 1);
	if (is_end)
		is_live = 0;
}

// the next frame waiting is made into sound in the buffer whose turn it
// is, with the over's lead before it if it is the over's first, and goes
// into its bin in small letters
static void fill_a_slot(void) {
	uint8_t tones[JTTY_SYMBOLS];
	char text[FRAME_TEXT_ROOM];
	int slot = (int)(slots_filled % SLOTS);
	int pitch = atomic_load(&tx_hz);
	int lead = 0;
	jtty_atom atom;
	int eom = 0;
	int i;

	if (atomic_load(&slot_state[slot]) != SLOT_EMPTY)
		return;
	for (i = 0; i < SLOTS; i++) {
		if (atomic_load(&slot_state[i]) == SLOT_READY)
			return;
	}
	if (queued_count == 0)
		take_live_frame();
	if (queued_count == 0)
		return;
	if (!is_sending())
		lead = TX_LEAD_SAMPLES;
	if (jtty_encode(queued[0], tones) != 0)
		memset(tones, 0, sizeof(tones));
	text[0] = 0;
	if (jtty_atom_unpack(queued[0], &atom, &eom) == 0)
		jtty_atom_render(&atom, text);
	queued_count--;
	memmove(queued, queued + 1, (size_t)queued_count * sizeof(queued[0]));
	memset(slot_wave[slot], 0, lead * sizeof(slot_wave[slot][0]));
	synth_gfsk(tones, JTTY_SYMBOLS, (float)pitch, SYMBOL_BT, SYMBOL_SECONDS, SOUND_CARD_RATE,
		slot_wave[slot] + lead);
	slot_samples[slot] = lead + JTTY_SYMBOLS * SAMPLES_A_SYMBOL;
	atomic_store(&slot_state[slot], SLOT_READY);
	slots_filled++;
	if (eom)
		printf("jtty: sending at %d Hz  %s  (end)\n", pitch, text);
	else
		printf("jtty: sending at %d Hz  %s\n", pitch, text);
	fflush(stdout);
	if (!strcmp(text, FILLER))
		snprintf(text, sizeof(text), " ");
	else if (atom.kind != JTTY_TEXT5)
		strncat(text, " ", sizeof(text) - strlen(text) - 1);
	for (i = 0; text[i] != 0; i++)
		text[i] = (char)tolower((unsigned char)text[i]);
	add_to_bin(pitch, text, eom, 0);
}

static int jtty_wants_tx(void) {
	fill_a_slot();
	return is_sending() || queued_count > 0 || is_live;
}

// the next buffer in turn, if it is ready, is begun
static int begin_a_slot(void) {
	int slot = (int)(atomic_load(&slots_played) % SLOTS);
	int ready = SLOT_READY;

	if (atomic_compare_exchange_strong(&slot_state[slot], &ready, SLOT_PLAYING))
		slot_pos = 0;
	return atomic_load(&slot_state[slot]) == SLOT_PLAYING;
}

static void tx_jtty(const int32_t *mic, double *samples) {
	double peak = SIDETONE_PEAK * atomic_load(&sidetone_level) / SIDETONE_HIGHEST;
	int slot;
	int is_playing = begin_a_slot();
	int i;

	(void)mic;
	for (i = 0; i < MAX_BINS/2; i++) {
		samples[i] = 0;
		sidetone_output[i] = 0;
		if (!is_playing)
			continue;
		slot = (int)(atomic_load(&slots_played) % SLOTS);
		samples[i] = slot_wave[slot][slot_pos] / TX_DIVISOR;
		sidetone_output[i] = (int32_t)(slot_wave[slot][slot_pos] * peak);
		slot_pos++;
		if (slot_pos < slot_samples[slot])
			continue;
		atomic_store(&slot_state[slot], SLOT_EMPTY);
		atomic_fetch_add(&slots_played, 1);
		is_playing = begin_a_slot();
	}
	speaker_play(sidetone_output, MAX_BINS/2);
}

// what live frames have taken from what was typed, since it was last asked
int jtty_take_sent(char *text, int room) {
	int count;

	snprintf(text, room, "%s", sent_new);
	count = (int)strlen(text);
	memmove(sent_new, sent_new + count, strlen(sent_new) - count + 1);
	return count;
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
	int count;
	int i;

	(void)c;
	if (!atomic_load(&is_working)) {
		snprintf(why, room, "JTTY could not be started: see the terminal");
		return -1;
	}
	if (is_live) {
		snprintf(why, room, "a message is being typed live: pause it first");
		return -1;
	}
	count = jtty_pack(value, JTTY_UNKNOWN, frames, normalized);
	if (count <= 0) {
		snprintf(why, room, "nothing in that can be sent");
		return -1;
	}
	if (queued_count + count > FRAMES_QUEUED_MOST) {
		snprintf(why, room, "too much is waiting to be sent already");
		return -1;
	}
	for (i = 0; i < count; i++)
		queue_frame(frames[i]);
	printf("jtty: to send, %d frames: %s\n", count, normalized);
	fflush(stdout);
	return 0;
}

// characters typed, "~" for a space: the first begins a live message;
// one with no place in JTTY's alphabet is sent as "#", as the library does
static int apply_live(struct control *c, const char *value, char *why, int room) {
	jtty_atom atom;
	uint64_t payload;
	size_t length = strlen(live);
	char typed;

	(void)c;
	if (!atomic_load(&is_working)) {
		snprintf(why, room, "JTTY could not be started: see the terminal");
		return -1;
	}
	for (; *value != 0 && length < LIVE_ROOM; value++) {
		typed = (char)toupper((unsigned char)*value);
		if (typed == SPACE_TYPED)
			typed = ' ';
		memset(&atom, 0, sizeof(atom));
		atom.kind = JTTY_TEXT5;
		atom.text[0] = typed;
		if (jtty_atom_pack(&atom, 0, &payload) != 0)
			typed = '#';
		live[length++] = typed;
	}
	live[length] = 0;
	if (!is_live) {
		is_live = 1;
		live_began = seconds_now();
	}
	live_typed_at = seconds_now();
	return 0;
}

static int apply_do(struct control *c, const char *value, char *why, int room) {
	size_t length = strlen(live);
	int was_sending = is_sending() || queued_count > 0 || is_live;
	int i;

	(void)c;
	(void)why;
	(void)room;
	if (!strcmp(value, "RUBOUT")) {
		if (length > 0)
			live[length - 1] = 0;
		return 0;
	}
	queued_count = 0;
	is_live = 0;
	live[0] = 0;
	for (i = 0; i < SLOTS; i++)
		atomic_store(&slot_state[i], SLOT_EMPTY);
	slots_filled = atomic_load(&slots_played);
	if (was_sending)
		add_to_bin(atomic_load(&tx_hz), " (stopped)", 1, 0);
	return 0;
}

static struct control split_control = {
	.group = "JTTY", .name = "JTTY_SPLIT", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON", .value = "OFF", .apply = apply_split,
};

static struct control rx_control = {
	.group = "JTTY", .name = "JTTY_RX", .kind = CONTROL_NUMBER,
	.min = PITCH_LOWEST, .max = PITCH_HIGHEST, .step = PITCH_STEP,
	.value = "1500", .apply = apply_rx,
};

static struct control tx_control = {
	.group = "JTTY", .name = "JTTY_TX", .kind = CONTROL_NUMBER,
	.min = PITCH_LOWEST, .max = PITCH_HIGHEST, .step = PITCH_STEP,
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

static struct control live_control = {
	.group = "JTTY", .name = "JTTY_LIVE", .kind = CONTROL_ACTION,
	.choices = "", .apply = apply_live,
};

static struct control do_control = {
	.group = "JTTY", .name = "JTTY_DO", .kind = CONTROL_ACTION,
	.choices = "ESC/RUBOUT", .apply = apply_do,
};

static void jtty_start(void) {
	jtty_rx_config config = {
		.fmin = SEARCH_LOWEST_HZ,
		.fmax = SEARCH_HIGHEST_HZ,
		.candidate_capacity = CANDIDATES_MOST,
		.decode_budget = DECODES_A_CHUNK,
	};
	int i;

	control_register(&split_control);
	control_register(&rx_control);
	control_register(&tx_control);
	control_register(&sidetone_control);
	control_register(&text_control);
	control_register(&live_control);
	control_register(&do_control);
	atomic_store(&tx_hz, atoi(tx_control.value));
	chosen_bin = bin_of(atoi(rx_control.value));
	atomic_store(&sidetone_level, atoi(sidetone_control.value));
	for (i = 0; i < SLOTS; i++) {
		slot_wave[i] = malloc(SLOT_ROOM * sizeof(slot_wave[i][0]));
		if (slot_wave[i] == NULL) {
			printf("jtty: there is not the memory for it\n");
			return;
		}
	}
	receiver = jtty_rx_create(&config);
	if (receiver == NULL) {
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
