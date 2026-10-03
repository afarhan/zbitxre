// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_ft8.c — FT8: the slots heard and searched, and a message sent.
//
// FT8 is heard as USB is, 50 to 4000 Hz above the dial, which is the width
// zbitxv2 gave it, and goes to the speaker as USB does. The same sound is
// kept, a slot at a time, and each slot is searched for messages. What is
// found is added to the radio's messages, with message_add(); messages.c
// says who reads them from there.
//
// The names are zbitxv2's: ft8_rx() takes the sound, ft8_thread_function()
// is the thread that decodes it, ft8_init() starts it. The decoding itself
// is in ft8_monitor.c, which says how it is done and whose work it is.
//
// THE SLOT
//
// Every station sends in slots of 15 seconds that begin at 0, 15, 30 and
// 45 seconds past the minute, by the clock, and a message takes 12.64
// seconds of its slot, beginning half a second in. So the radio has to
// know the time to about a second, and it is the radio's clock that says
// it, hw/radio_clock.c: the kernel's when that can be trusted, and what
// the operator set on the panel when it cannot. With the clock not set at
// all, nothing can be expected to be found, and the terminal is told so.
//
// ft8_rx() is called by the audio thread with each block of sound, 1024
// samples at 96000 a second. Every eighth is kept, which makes 12000 a
// second, the rate the decoder works at; the receiver's filter has
// already cut off everything above 4 kHz, so nothing folds back. When the
// clock's second changes, it looks where in the slot that is, as zbitxv2
// did:
//
//   at second 0   the keeping starts again from nothing, so that what is
//                 kept begins where the slot does, to within a block.
//   at second 14  if 13 seconds or more have been kept, the slot is given
//                 to the decoder. Every message in it had ended a second
//                 before. A slot that was joined late, after a change of
//                 mode, is not long enough and is let go.
//
// THE THREAD
//
// Searching a slot takes far longer than the 10 ms a block allows, so it
// is done by a thread of its own, as modem.h asks. The audio thread must
// never wait, so nothing is locked between them. There are two buffers,
// one being filled and one being searched; the audio thread exchanges
// them and wakes the decoder with a semaphore, and it does so only while
// the decoder is idle, which is_decoding says. Should the decoder still
// be at the last slot when the next is ready, that next one is let go.
//
// The clock is read by the decoder's thread, once as it starts and again
// after every slot, since reading it means asking the kernel and opening a
// file, which the audio thread must not do. What it keeps is how far the
// radio's clock is ahead of the kernel's, and the audio thread adds that
// to the kernel's time, which costs it nothing to read.
//
// SENDING
//
// What is sent, and when, is ft8_qso.c's to decide, from the thread that
// owns the controls. It hands a message and the slot it is for to
// ft8_tx_arm(), which makes it into sound then and there, with
// ft8_signal.c, at the sound card's rate, so that the audio thread has
// only to copy it out.
//
// The radio goes to transmit while wants_tx() says so: from the start of
// that slot until the message has been sent. tx() gives the transmitter
// silence until half a second into the slot, where WSJT-X and every other
// station begin, and then the message, 12.64 seconds of it, at zbitxv2's
// level, a seventh of full scale. A message armed for a slot that began
// more than two seconds ago is not sent at all: begun later, it would be
// too late for most receivers to find. ft8_tx_cancel() stops a message,
// whether it is waiting or being sent.
//
// Which of the two threads has the message is in tx_state: IDLE, ARMED,
// waiting for its slot, and SENDING. Only the thread that owns the
// controls arms, and only while nothing is being sent; the audio thread
// takes an ARMED message to SENDING, and back to IDLE when it is done.
// A change between them is made with an exchange that cannot be split, so
// that a cancel and the start of the sending cannot both win.
//
// ======================================================================
// THE CONTACT: THE DESIGN, NOT YET BUILT
// ======================================================================
//
// What follows was agreed with the author on 3 October 2026, before any of
// it was written. It is here so that the code, when it comes, is written
// to it, and so that whoever reads this file knows why it is so. The
// procedure is the one in the WSJT-X User Guide, version 3.0.0, sections
// 10.1 to 10.6, and the states are the six its auto-sequencer has, so that
// an operator who knows WSJT-X finds the radio doing what is expected.
//
// WHY: WHAT WENT WRONG IN ZBITXV2
//
// Contacts and logging have been the problem with FT8 on the sbitx. In
// zbitxv2's modem_ft8.c, ft8_process() chose what to send from the words
// of the last message alone, and kept no state:
//
//   - ft8_on_signal_report() logged as soon as a report came, with an R or
//     without, which can be before the other station has ours; the
//     RR73/RRR branch logged again. Only enter_qso()'s refusal of the same
//     call within 60 seconds kept the second entry out.
//   - the RR73/RRR branch answered and logged anyone's RR73 to us, without
//     looking at who was being worked.
//   - a 73 aborted without logging, so an answering station that missed
//     the RR73 and copied only the 73 lost the contact.
//   - a message repeated by the other station, because ours was missed,
//     was taken as new, and was answered and logged again.
//   - the report we sent could change under us, taken from whatever
//     message was last clicked or heard.
//
// THE STANDARD CONTACT
//
//     CQ K1ABC FN42                         K1ABC calls CQ
//                       K1ABC G0XYZ IO91    G0XYZ answers          (Tx1)
//     G0XYZ K1ABC -19                       K1ABC sends a report   (Tx2)
//                       K1ABC G0XYZ R-22    G0XYZ: R and a report  (Tx3)
//     G0XYZ K1ABC RR73                      K1ABC: RR73, or RRR    (Tx4)
//                       K1ABC G0XYZ 73      G0XYZ: 73              (Tx5)
//
// A report is how strong the other station was, in dB over the noise in
// 2500 Hz. A station may skip Tx1 and answer with a report. RR73 is sent
// as if it were a grid square, so it is to be looked for before a word of
// four characters is taken for a grid.
//
// THE OPERATOR'S CHOICES
//
// Two settings, in the SET screen's list, kept with the rest:
//
//   FT8-Operations  Normal, Running or Pouncing.
//     Running    CQ is called until someone answers; the contact is
//                finished, or given up, and CQ is called again.
//     Pouncing   only CQs are answered: the operator marks a CQ in the list
//                of messages and clicks the knob. The radio never calls CQ.
//     Normal     a CQ is called no more than three times unanswered. A
//                marked message is answered as in Pouncing.
//
//   FT8 Reply  RRR or RR73: what the station that called CQ sends once it
//              has the other's R and report.
//
// Open, to be settled with the author: in Running, whether a CQ marked
// and clicked while calling CQ is answered, and CQ called again after (we
// think yes); in Pouncing, whether a station that calls us uncalled is
// answered, or only shown (we think shown).
//
// THE RULES BOTH WAYS OF WORKING SHARE
//
//   - Only one station counts. A message moves the contact on only if its
//     first callsign is ours, or our hash, and its second is the station
//     being worked. Every other message is in the list and nothing more.
//   - One step to a slot. The messages of the slot just searched are looked
//     at together, and one from the station being worked moves the state
//     once. What we send next is chosen before our slot begins; a second
//     is plenty for that, the author says, and sbitx has missed no
//     contact for want of it.
//   - We send in the slots the other station does not: if its message
//     began at 0 or 30 seconds, ours begin at 15 or 45, and so on.
//   - Each state sends its message in each of our slots until something
//     moves it, up to a limit, four sends say; then the contact is given
//     up and nothing is logged.
//   - The report we send is fixed when it is first sent, from the strength
//     of the message we answered, and is the same in every repeat.
//   - A contact is logged once. It keeps a flag that says it has been,
//     which takes the place of zbitxv2's 60 second window.
//   - Stopping, or changing the band, the frequency or the mode, ends the
//     contact, and nothing is logged.
//
// WHEN WE CALL CQ
//
//   we are in, sending    from X               we send next
//   ------------------    -----------------    ----------------------------
//   CALLING CQ            MY X GRID            X MY rpt, to REPORT
//     CQ MY GRID          MY X rpt (no Tx1)    X MY R-rpt, to ROGER_REPORT
//                         MY X R-rpt           X MY RR73, to ROGERS; log
//                         more than one        the first heard; the rest
//                         caller               are only shown
//                         nothing              CQ again; in Normal, no
//                                              more than three times
//
//   REPORT                MY X R-rpt           X MY RR73, to ROGERS; log
//     X MY rpt            MY X GRID again      rpt again: ours was missed
//                         MY X rpt (no R)      X MY R-rpt, to ROGER_REPORT
//                         MY X RRR, RR73       log, if we have their
//                         or 73                report; the contact ends
//                         nothing, to the      given up, not logged
//                         limit
//
//   ROGER_REPORT          MY X RRR or RR73     log; X MY 73, to SIGNOFF
//     X MY R-rpt          MY X 73              log; the contact ends
//     (only when X        their report again   R-rpt again
//     skipped a step)
//
//   ROGERS                MY X 73              the contact ends
//     X MY RR73 (or       MY X R-rpt again     RR73 again: ours was missed;
//     RRR)                                     not logged again
//                         nothing for a slot   the contact ends
//
// With RR73, the station that called CQ logs when it first sends RR73,
// which is when the R and the report have come: it has the other's
// callsign and report, and the R says the other has ours. This is when
// WSJT-X asks to log, as it sends a message with 73 in it. Should our RR73
// not be copied, the other's R and report come again, and ROGERS sends
// RR73 again, without a second entry.
//
// With RRR, which asks for a 73 back, the entry is made when the 73 comes,
// or when two slots have passed after RRR with nothing more.
//
// When the contact has ended, or been given up, Running calls CQ again;
// Normal stops, as WSJT-X does after every contact.
//
// WHEN WE ANSWER
//
// It begins with a message the operator has marked in the list and the
// knob's click: a CQ from X, or the end of X's last contact, Y X RR73 or
// Y X 73, which is answering at the tail.
//
//   we are in, sending    from X               we send next
//   ------------------    -----------------    ----------------------------
//   CALLING STN           MY X rpt             X MY R-rpt, to ROGER_REPORT
//     X MY GRID           MY X R-rpt           X MY RR73, to ROGERS; log
//                         Y X ...: X           stop; if X calls us later,
//                         answered another     go on (WSJT-X's "Wait and
//                                              Reply")
//                         nothing, to the      given up, not logged
//                         limit
//
//   ROGER_REPORT          MY X RR73 or RRR     log; X MY 73 once, to SIGNOFF
//     X MY R-rpt          MY X 73              log; the contact ends
//                         MY X rpt again       R-rpt again: ours was missed
//                         nothing, to the      given up, not logged; the
//                         limit                status bar says so
//
//   SIGNOFF               MY X RR73 again      73 once more; not logged
//     X MY 73                                  again
//                         anything else, or    the contact ends
//                         nothing
//
// The answering station logs when the RR73, the RRR or the 73 comes. It is
// what the guide says always to log on, and it is certain: X has had our
// report and says so, and we have X's. A report alone is never enough.
//
// WHAT IS LOGGED
//
// Into the logbook, data/sbitx.db, as zbitxv2 kept it: callsign_recv the
// station worked; rst_sent the report we sent; rst_recv the report X sent,
// without its R; exch_sent our grid; exch_recv X's grid, if it was heard;
// the mode, FT8; the frequency; and the date and the time, in UTC, of the
// moment of logging.
//
// CALLSIGNS SENT AS A HASH
//
// A callsign that does not fit FT8's pattern for one is sent in full in
// some messages and as a hash in others; the guide's section 10.6. These
// fit, and are never hashed: K1ABC, K1ABC/P, K1ABC/R, and the CQs of the
// activities, CQ POTA K1ABC FN42, CQ SOTA. These do not: a prefix,
// VE3/K1ABC, PJ4/K1ABC; another suffix, /QRP, /M, /MM, /3; and calls longer
// than the pattern, as many special event calls are.
//
//     CQ PJ4/K1ABC                    the callsign in full
//     <PJ4/K1ABC> W9XYZ               PJ4/K1ABC as a 22 bit hash
//     W9XYZ <PJ4/K1ABC> +03
//     <W9XYZ> PJ4/K1ABC RRR           in full again; W9XYZ as a 12 bit hash
//     PJ4/K1ABC <W9XYZ> 73
//
// The hash is worked out from the callsign by a fixed rule, so nobody
// publishes a list of hashes: a receiver knows a hash only if it has heard
// or been given the callsign it is of. WSJT-X shows the callsign if it
// heard it in full "in the recent past", and <...> if not. zbitxv2's copy
// of ft8_lib, which is ours, does not look hashes up at all, and always
// shows <...>.
//
// The author's plan: the callsigns and their hashes are kept in the
// SQLite database with the logbook, in a table of their own:
//
//     callsign_hash(call, hash22, hash12, hash10, source, last_seen)
//
// The hashes are worked out once, as a callsign goes in, and looked up by
// an index, never by working them out again. The callsigns come from:
//
//   - ARRL's list of LoTW users, https://lotw.arrl.org/lotw-user-activity.csv,
//     which WSJT-X fetches too and which is brought up to date weekly. On
//     3 October 2026 it had 236,015 callsigns, 29,362 of them with a slash,
//     in 6.3 MB.
//   - the logbook.
//   - every callsign heard in full on the air, saved as it is decoded. It is
//     what WSJT-X keeps in memory, kept here across a restart.
//
// The radio looks for a new LoTW list once a day, when its clock is from
// NTP, which says the network is there. It asks with curl, run as wpa_cli
// is run, giving the ETag the server sent with the list it has: an
// unchanged list is answered "304 Not Modified" and nothing is fetched. A
// new one replaces the old in one transaction, and its ETag is kept.
//
// The three sizes of hash are looked up differently, since a short hash
// is shared by many callsigns:
//
//   22 bits   the nonstandard callsign, in a standard message. Of 4,194,304
//             values, so looked up in the table: the callsigns heard lately
//             first, then the logbook's, then those of the LoTW list with a
//             slash. Among the 29,362 of those, a wrong match is under one
//             in a hundred.
//   12 bits   the standard callsign beside a nonstandard one. Some 57 of the
//             LoTW list share each value, so it is never looked up there: it
//             is ours or the station's we are working, and is compared with
//             those two.
//   10 bits   the Fox's callsign, in DXpedition mode: the Fox being called.
//
// So in a contact the hashes need no table: ours is worked out from our
// callsign, and the other's from the callsign heard in full when we
// answered. The table turns <...> into callsigns in the list of messages,
// and catches a station with a nonstandard callsign that calls us before
// we have heard it.

#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sdr.h"
#include "modem.h"
#include "messages.h"
#include "radio_clock.h"
#include "ft8_monitor.h"
#include "ft8_signal.h"
#include "modem_ft8.h"

#define FT8_LOW_HZ 50
#define FT8_HIGH_HZ 4000
#define FT8_MAX_BUFF (FT8_SAMPLE_RATE * 18)
#define FT8_FOUND_MAX 50
#define SOUND_CARD_RATE 96000
#define SLOT_SECONDS 15
#define DECODE_AT_SECOND 14
#define ENOUGH_SECONDS 13
// zbitxv2's scaling of the receiver's samples for the decoder
#define FT8_FULL_SCALE 200000000.0f
#define MS_PER_S 1000
#define NS_PER_MS 1000000
#define NS_PER_S 1000000000.0
#define TX_STARTS_AT_S 0.5
#define TX_LATEST_START_S 2.0
// zbitxv2's level for FT8: a seventh of full scale
#define TX_DIVISOR 7.0

enum tx_state {
	TX_IDLE,
	TX_ARMED,
	TX_SENDING,
};

static int32_t baseband_output[MAX_BINS/2];

static float buffer_a[FT8_MAX_BUFF];
static float buffer_b[FT8_MAX_BUFF];
static float *ft8_rx_buffer = buffer_a;
static float *ft8_decode_buffer = buffer_b;
static int ft8_rx_buff_index;
static int ft8_decode_count;
static time_t ft8_decode_slot;
static time_t wallclock;

static pthread_t ft8_thread;
static sem_t slot_is_ready;
static atomic_int is_decoding;
static atomic_long clock_ahead_s;

static struct ft8_decoded found[FT8_FOUND_MAX];
static atomic_long searched_slot;

static float *tx_wave;
static int tx_wave_samples;
static int tx_wave_room;
static time_t tx_slot;
static long tx_pos;
static atomic_int tx_state;

// the time, by the radio's clock, in seconds and their fractions
double ft8_time_now(void) {
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	return (double)(now.tv_sec + atomic_load(&clock_ahead_s)) + now.tv_nsec / NS_PER_S;
}

// the start of the newest slot that has been searched
time_t ft8_searched_slot(void) {
	return (time_t)atomic_load(&searched_slot);
}

// how far the radio's clock is ahead of the kernel's, for the audio thread
static void read_the_clock(void) {
	static int was_told;
	time_t now;

	if (radio_clock_now(&now) == RADIO_CLOCK_NOT_SET) {
		if (!was_told)
			printf("ft8: the radio's clock is not set, and FT8 needs the time: "
				"set it on the panel, SET, Date/Time\n");
		was_told = 1;
		atomic_store(&clock_ahead_s, 0);
		return;
	}
	was_told = 0;
	atomic_store(&clock_ahead_s, (long)(now - time(NULL)));
}

static long elapsed_ms(const struct timespec *since) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - since->tv_sec) * MS_PER_S
		+ (now.tv_nsec - since->tv_nsec) / NS_PER_MS;
}

static void *ft8_thread_function(void *ptr) {
	struct timespec began;
	struct tm utc;
	int count;
	int i;

	(void)ptr;
	while (1) {
		read_the_clock();
		sem_wait(&slot_is_ready);
		clock_gettime(CLOCK_MONOTONIC, &began);
		count = sbitx_ft8_decode(ft8_decode_buffer, ft8_decode_count, found, FT8_FOUND_MAX);
		for (i = 0; i < count; i++)
			message_add("FT8", ft8_decode_slot, found[i].snr, found[i].freq_hz,
				found[i].text);
		gmtime_r(&ft8_decode_slot, &utc);
		printf("ft8: slot %02d:%02d:%02d, found %d, searched in %ld ms\n",
			utc.tm_hour, utc.tm_min, utc.tm_sec, count, elapsed_ms(&began));
		fflush(stdout);
		atomic_store(&searched_slot, (long)ft8_decode_slot);
		atomic_store(&is_decoding, 0);
	}
	return NULL;
}

// the slot that has been kept goes to the decoder, if the decoder is idle,
// and the keeping goes on in the other buffer
static void give_the_slot(time_t slot_start) {
	float *kept = ft8_rx_buffer;

	if (atomic_load(&is_decoding))
		return;
	ft8_rx_buffer = ft8_decode_buffer;
	ft8_decode_buffer = kept;
	ft8_decode_count = ft8_rx_buff_index;
	ft8_decode_slot = slot_start;
	atomic_store(&is_decoding, 1);
	sem_post(&slot_is_ready);
}

// the ft8 sampling is at 12000, the incoming samples are at
// 96000 samples/sec
static void ft8_rx(int32_t *samples, int count) {
	int decimation_ratio = SOUND_CARD_RATE / FT8_SAMPLE_RATE;
	int slot_second;
	time_t now;
	int i;

	//if there is an overflow, then reset to the begining
	if (ft8_rx_buff_index + (count / decimation_ratio) >= FT8_MAX_BUFF)
		ft8_rx_buff_index = 0;

	//down convert to 12000 Hz sampling rate
	for (i = 0; i < count; i += decimation_ratio)
		ft8_rx_buffer[ft8_rx_buff_index++] = samples[i] / FT8_FULL_SCALE;

	now = time(NULL) + atomic_load(&clock_ahead_s);
	if (now == wallclock)
		return;
	wallclock = now;

	slot_second = (int)(wallclock % SLOT_SECONDS);
	if (slot_second == 0)
		ft8_rx_buff_index = 0;

	//we should have atleast 13 seconds of samples to decode
	if (ft8_rx_buff_index >= ENOUGH_SECONDS * FT8_SAMPLE_RATE
			&& slot_second == DECODE_AT_SECOND) {
		give_the_slot(wallclock - slot_second);
		ft8_rx_buff_index = 0;
	}
}

static void rx_ft8(complex float *bins) {
	demod_ssb(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
	ft8_rx(baseband_output, MAX_BINS/2);
}

/* sending */

// the message is made into sound, at its pitch, for the slot that begins
// at slot_start; 0, or -1 if it cannot be packed or something is being
// sent. For the thread that owns the controls
int ft8_tx_arm(const char *message, int pitch_hz, time_t slot_start) {
	int samples;

	if (tx_wave == NULL || atomic_load(&tx_state) == TX_SENDING)
		return -1;
	atomic_store(&tx_state, TX_IDLE);
	samples = sbitx_ft8_encode(message, pitch_hz, SOUND_CARD_RATE, tx_wave, tx_wave_room);
	if (samples < 0)
		return -1;
	tx_wave_samples = samples;
	tx_slot = slot_start;
	atomic_store(&tx_state, TX_ARMED);
	return 0;
}

void ft8_tx_cancel(void) {
	atomic_store(&tx_state, TX_IDLE);
}

int ft8_tx_is_busy(void) {
	return atomic_load(&tx_state) != TX_IDLE;
}

// from the start of the armed message's slot until it has been sent; one
// whose slot began too long ago is let go
static int ft8_wants_tx(void) {
	int armed = TX_ARMED;
	double now;

	if (atomic_load(&tx_state) == TX_SENDING)
		return 1;
	if (atomic_load(&tx_state) != TX_ARMED)
		return 0;
	now = ft8_time_now();
	if (now < (double)tx_slot)
		return 0;
	if (now > (double)tx_slot + TX_LATEST_START_S) {
		if (atomic_compare_exchange_strong(&tx_state, &armed, TX_IDLE))
			printf("ft8: too late for the slot, not sent\n");
		return 0;
	}
	return 1;
}

// silence until half a second into the slot, then the message
static void tx_ft8(const int32_t *mic, double *samples) {
	int armed = TX_ARMED;
	int i;

	(void)mic;
	if (atomic_compare_exchange_strong(&tx_state, &armed, TX_SENDING))
		tx_pos = (long)((ft8_time_now() - (double)tx_slot - TX_STARTS_AT_S)
			* SOUND_CARD_RATE);
	if (atomic_load(&tx_state) != TX_SENDING) {
		memset(samples, 0, MAX_BINS/2 * sizeof(samples[0]));
		return;
	}
	for (i = 0; i < MAX_BINS/2; i++) {
		samples[i] = 0;
		if (tx_pos >= 0 && tx_pos < tx_wave_samples)
			samples[i] = tx_wave[tx_pos] / TX_DIVISOR;
		tx_pos++;
	}
	if (tx_pos >= tx_wave_samples)
		atomic_store(&tx_state, TX_IDLE);
}

static void ft8_select(void) {
	set_passband(FT8_LOW_HZ, FT8_HIGH_HZ);
}

// a slot that was being kept when the mode was left is not one to decode
static void ft8_reset(void) {
	ft8_rx_buff_index = 0;
}

static void ft8_init(void) {
	tx_wave_room = ft8_signal_samples(SOUND_CARD_RATE);
	tx_wave = malloc(tx_wave_room * sizeof(tx_wave[0]));
	sem_init(&slot_is_ready, 0, 0);
	pthread_create(&ft8_thread, NULL, ft8_thread_function, NULL);
}

const struct modem modem_ft8 = {
	.name = "FT8",
	.order = 6,
	.start = ft8_init,
	.select = ft8_select,
	.reset = ft8_reset,
	.rx = rx_ft8,
	.tx = tx_ft8,
	.wants_tx = ft8_wants_tx,
};
