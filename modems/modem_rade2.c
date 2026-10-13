// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_rade2.c — RADE2: FreeDV's digital voice, RADE version 2.
//
// WHAT IT IS
//
// RADE is David Rowe VK5DGR's radio autoencoder: speech goes out as an
// OFDM signal between 300 and 2700 Hz of an SSB transmitter's audio, and
// comes out of the far receiver as speech, at signal levels where SSB is
// hard to copy. dsp/rade2_codec.c is the radio's way in to the code that
// does it, FreeDV's and Opus's, and says whose each part is.
//
// The author asked for version 2 alone, as the mode RADE2. Version 1
// searches for a signal all the while it hears none, and that search took
// a processor and a half of the zbitx's Pi Zero 2 W, measured 6 October
// 2026; version 2 has no such search.
//
// FreeDV says version 2's signal may still change, and asks that it not go
// on the air until they release it. This is for the bench and for trying
// between two of our own stations until then.
//
// IT HAS A THREAD OF ITS OWN
//
// The author's ruling. The codec's work comes in lumps, a frame of the
// signal at a time, and on a Pi Zero receiving takes half a processor; the
// audio thread has ten milliseconds a block and is not to wait for it. So
// rx() and tx(), which the audio thread calls, only move samples in and
// out of four rings and change their rates, and the thread does the rest:
//
//     receiving
//         rx()      the SSB audio, every twelfth sample  -> heard, at 8000
//         thread    heard -> the codec -> speech         -> spoken, at 16000
//         rx()      spoken, six samples for one          -> the speaker
//     sending
//         tx()      the microphone, every sixth sample   -> said, at 16000
//         thread    said -> the codec -> the signal      -> sent, at 8000
//         tx()      sent, twelve samples for one         -> the transmitter
//
// Each ring has one that writes it and one that reads it, each moving its
// own place only, and needs no lock. The audio thread wakes the thread
// with each block.
//
// The rates are changed with filters made when the radio starts, a sinc
// under a Hamming window: one before the microphone's samples are thinned,
// and one each for the two that are filled out. The receiver's audio is
// thinned without one: the passband, 250 to 2750 Hz, has already taken
// away what would fold down, as FT8 relies on too.
//
// WHAT IS HEARD
//
// Until the codec has found a signal, the receiver's SSB audio, so that a
// station can be tuned in by ear; it sounds like a buzz. Once it has, the
// speech, from when a tenth of a second of it is waiting, the codec giving
// it in pieces; and SSB again when the signal is lost or the over ends.
//
// WHICH SIDEBAND
//
// The upper above 10 MHz and the lower below, as for speech, and as
// FreeDV's operators have it. core_sdr.c chooses when it is asked for the
// mode, and radio.c asks again when the frequency crosses 10 MHz.
//
// HOW AN OVER BEGINS AND ENDS
//
// At the first block of a transmission the codec's transmitter is begun
// afresh. Nothing is sent until two frames of the signal are waiting, 80
// ms, so that the thread being a little late does not put a gap in it.
//
// The far receiver has to be told the over has ended, or it speaks noise
// until it gives the signal up. So when the PTT is let go the radio
// program says so, tx_end(), and the thread makes what speech it still
// has into signal, and after it the codec's frame that ends an over and a
// fifth of a second of silence. wants_tx() keeps the radio sending until
// that has gone, as CW's does for its hang time. A transmission ended any
// other way, by a client's TX OFF or a change of mode, stops at once
// without it.
//
// HOW LOUD
//
// Sending: the codec's signal has a level of about a half and peaks of
// one. The peaks are put where 2TONE's are, which is the drive the
// transmitter is known to take cleanly; the level is then 3 dB under
// 2TONE's, the signal's peaks being 6 dB over its level. The
// microphone's samples are brought to 16 bits for the codec, whose
// training was on speech at ordinary levels; MIC is how the operator sets
// it.
//
// Receiving: the receiver's audio, levelled by its AGC, is divided by
// HEARD_FULL_SCALE to come to the codec near the level it expects, which
// its own levelling then corrects within 20 dB. The figure was set with
// the test, tests/test_rade2.c, which puts a signal through the whole
// receiver.
//
// WHAT THE OPERATOR IS TOLD
//
// RADE2_STATUS, in the group RADE2: "--" with no signal, and with one,
// its signal to noise ratio and how far off tune it is, "SYNC 12 dB +3
// Hz". The thread only notes them; wants_tx(), which the thread that owns
// the controls calls many times a second, puts them in the control.

#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"
#include "rade2_codec.h"

#define AUDIO_RATE_HZ 96000
#define BLOCK (MAX_BINS / 2)
#define PASSBAND_LOW_HZ 250
#define PASSBAND_HIGH_HZ 2750
#define SIGNAL_STEP (AUDIO_RATE_HZ / RADE2_MODEM_RATE_HZ)
#define SPEECH_STEP (AUDIO_RATE_HZ / RADE2_SPEECH_RATE_HZ)
#define TAPS_A_PHASE 16
#define SIGNAL_TAPS (SIGNAL_STEP * TAPS_A_PHASE)
#define SPEECH_TAPS (SPEECH_STEP * TAPS_A_PHASE)
#define SIGNAL_CUTOFF_HZ 3600.0
#define SPEECH_CUTOFF_HZ 7200.0
#define HAMMING_A 0.54
#define HAMMING_B 0.46
#define RING_SAMPLES 32768
#define SPOKEN_LEAST (RADE2_SPEECH_RATE_HZ / 10)
#define SENT_LEAST 640
#define TAIL_SAMPLES (RADE2_MODEM_RATE_HZ / 5)
// the radio still sending this long after an over's end has gone, the
// PTT has been pressed again: another over
#define NEW_OVER_AFTER_BLOCKS 30
#define STATUS_ROOM 32
#define STATUS_QUIET "--"
// the receiver's audio as it comes from demod_ssb(), to the codec's measure
#define HEARD_FULL_SCALE 200000000.0f
// 16 bits of speech to the speaker's measure. The receiver's own audio,
// levelled by its AGC, was measured in the test at a quarter of
// HEARD_FULL_SCALE; this puts speech of an ordinary level about there, so
// that the buzz of a signal not yet found is no louder than the voice that
// follows it
#define SPEAKER_A_SAMPLE 16000.0f
// the microphone's 32 bits to the 16 the codec takes
#define MIC_A_SAMPLE 65536.0f
#define SPEECH_MOST 32767.0f
// the codec's peaks, of one, put where 2TONE's are
#define TX_LEVEL 0.042

struct ring {
	float samples[RING_SAMPLES];
	atomic_uint put;
	atomic_uint taken;
};

// a filter that fills a signal out, so many samples for one, or, used the
// other way, goes before one that is thinned
struct rate_filter {
	int step;
	float taps[SIGNAL_TAPS];
	float history[TAPS_A_PHASE * SIGNAL_STEP];
	int at;
	int phase;
};

enum over {
	OVER_NONE,
	OVER_SPEAKING,
	OVER_ENDING,
};

static struct ring heard;
static struct ring spoken;
static struct ring said;
static struct ring sent;

static struct rate_filter speech_up;
static struct rate_filter signal_up;
static struct rate_filter mic_down;

static pthread_t rade2_thread;
static sem_t work;
static int is_open;

// set by the audio thread, taken by the modem's
static atomic_int tx_begin_wanted;
static atomic_int rx_begin_wanted;
static atomic_int tx_end_wanted;
// set by the modem's thread
static atomic_int is_synced;
static atomic_int snr_db;
static atomic_int offset_hz;
static atomic_int is_tail_made;

// the audio thread's
static int32_t baseband_output[BLOCK];
static int32_t speech_output[BLOCK];
static int heard_phase;
static int is_speaking;
static int is_tx_on;
static int is_sending;
static atomic_int is_tail_sent;
static atomic_int is_over_on;
static int blocks_since_tail;

// the modem thread's
static enum over over;
// the thread that owns the controls'
static char status_shown[STATUS_ROOM];

/* the rings */

static unsigned ring_count(struct ring *r) {
	return atomic_load(&r->put) - atomic_load(&r->taken);
}

// by the one that writes; a sample there is no room for is let go
static void ring_put(struct ring *r, float sample) {
	unsigned put = atomic_load(&r->put);

	if (put - atomic_load(&r->taken) >= RING_SAMPLES)
		return;
	r->samples[put % RING_SAMPLES] = sample;
	atomic_store(&r->put, put + 1);
}

// by the one that reads; nought when there is nothing
static float ring_take(struct ring *r) {
	unsigned taken = atomic_load(&r->taken);
	float sample;

	if (taken == atomic_load(&r->put))
		return 0;
	sample = r->samples[taken % RING_SAMPLES];
	atomic_store(&r->taken, taken + 1);
	return sample;
}

// by the one that reads: what is waiting is let go
static void ring_empty(struct ring *r) {
	atomic_store(&r->taken, atomic_load(&r->put));
}

/* the filters */

// a low-pass filter of step * TAPS_A_PHASE taps for samples at the sound
// card's rate, with a gain of gain
static void rate_filter_make(struct rate_filter *f, int step, double cutoff_hz, double gain) {
	int taps = step * TAPS_A_PHASE;
	double middle = (taps - 1) / 2.0;
	double turns = 2.0 * cutoff_hz / AUDIO_RATE_HZ;
	double sum = 0;
	double x;
	double tap;
	int i;

	memset(f, 0, sizeof(*f));
	f->step = step;
	for (i = 0; i < taps; i++) {
		x = M_PI * turns * (i - middle);
		tap = (x == 0 ? 1.0 : sin(x) / x)
			* (HAMMING_A - HAMMING_B * cos(2 * M_PI * i / (taps - 1)));
		f->taps[i] = (float)tap;
		sum += tap;
	}
	for (i = 0; i < taps; i++)
		f->taps[i] = (float)(f->taps[i] * gain / sum);
}

// filling out: the next of the step samples that are made of each one
// taken from the ring, a new one being taken as each step begins
static float rate_filter_up(struct rate_filter *f, struct ring *from) {
	float out = 0;
	int k;

	if (f->phase == 0) {
		f->at = (f->at + TAPS_A_PHASE - 1) % TAPS_A_PHASE;
		f->history[f->at] = ring_take(from);
	}
	for (k = 0; k < TAPS_A_PHASE; k++)
		out += f->taps[f->phase + k * f->step] * f->history[(f->at + k) % TAPS_A_PHASE];
	f->phase = (f->phase + 1) % f->step;
	return out;
}

// thinning: a sample is taken in, and of each step of them one is put in
// the ring, filtered
static void rate_filter_down(struct rate_filter *f, float sample, struct ring *to) {
	int taps = f->step * TAPS_A_PHASE;
	float out = 0;
	int k;

	f->at = (f->at + taps - 1) % taps;
	f->history[f->at] = sample;
	f->phase = (f->phase + 1) % f->step;
	if (f->phase != 0)
		return;
	for (k = 0; k < taps; k++)
		out += f->taps[k] * f->history[(f->at + k) % taps];
	ring_put(to, out);
}

static void rate_filter_clear(struct rate_filter *f) {
	memset(f->history, 0, sizeof(f->history));
	f->at = 0;
	f->phase = 0;
}

/* the thread */

// what has been heard goes to the codec in the pieces it asks for, and the
// speech it makes of them is queued for the speaker
static void receive_heard(void) {
	float signal[RADE2_MODEM_MOST];
	float features[RADE2_FRAMES_MOST * RADE2_FEATURES];
	int16_t speech[RADE2_SPEECH_FRAME];
	int was_synced;
	int is_end;
	int frames;
	int wanted;
	int count;
	int f;
	int i;

	if (atomic_exchange(&rx_begin_wanted, 0)) {
		ring_empty(&heard);
		rade2_rx_begin();
		atomic_store(&is_synced, 0);
	}
	for (wanted = rade2_rx_wanted(); (int)ring_count(&heard) >= wanted;
			wanted = rade2_rx_wanted()) {
		for (i = 0; i < wanted; i++)
			signal[i] = ring_take(&heard);
		was_synced = atomic_load(&is_synced);
		frames = rade2_rx_demodulate(signal, features, &is_end);
		for (f = 0; f < frames; f++) {
			count = rade2_rx_synthesise(features + f * RADE2_FEATURES, speech);
			for (i = 0; i < count; i++)
				ring_put(&spoken, speech[i]);
		}
		atomic_store(&snr_db, (int)lroundf(rade2_rx_snr_db()));
		atomic_store(&offset_hz, (int)lroundf(rade2_rx_offset_hz()));
		atomic_store(&is_synced, rade2_rx_is_synced());
		// the next signal is another voice, or the same after a silence:
		// the vocoder begins again
		if (is_end || (was_synced && !rade2_rx_is_synced()))
			rade2_rx_synthesis_begin();
	}
}

// what has been said goes to the codec a frame at a time, and the signal
// it makes is queued for the transmitter; at the end of an over, what ends
// it
static void send_said(void) {
	float signal[RADE2_MODEM_MOST];
	float features[RADE2_FEATURES];
	int16_t speech[RADE2_SPEECH_FRAME];
	float sample;
	int count;
	int i;

	if (atomic_exchange(&tx_begin_wanted, 0)) {
		ring_empty(&said);
		rade2_tx_begin();
		atomic_store(&tx_end_wanted, 0);
		atomic_store(&is_tail_made, 0);
		over = OVER_SPEAKING;
	}
	if (over != OVER_SPEAKING) {
		ring_empty(&said);
		return;
	}
	while (ring_count(&said) >= RADE2_SPEECH_FRAME) {
		for (i = 0; i < RADE2_SPEECH_FRAME; i++) {
			sample = ring_take(&said);
			if (sample > SPEECH_MOST)
				sample = SPEECH_MOST;
			if (sample < -SPEECH_MOST)
				sample = -SPEECH_MOST;
			speech[i] = (int16_t)sample;
		}
		rade2_tx_analyse(speech, features);
		count = rade2_tx_modulate(features, signal);
		for (i = 0; i < count; i++)
			ring_put(&sent, signal[i]);
	}
	if (!atomic_load(&tx_end_wanted))
		return;
	count = rade2_tx_end(signal);
	for (i = 0; i < count; i++)
		ring_put(&sent, signal[i]);
	for (i = 0; i < TAIL_SAMPLES; i++)
		ring_put(&sent, 0);
	over = OVER_ENDING;
	atomic_store(&is_tail_made, 1);
}

static void *rade2_thread_function(void *unused) {
	(void)unused;
	while (1) {
		sem_wait(&work);
		send_said();
		receive_heard();
	}
	return NULL;
}

/* the audio thread */

static void rx_rade2(complex float *bins) {
	int i;

	// the first block after a transmission: the receiver begins afresh
	if (is_tx_on) {
		is_tx_on = 0;
		is_sending = 0;
		atomic_store(&is_over_on, 0);
		heard_phase = 0;
		atomic_store(&rx_begin_wanted, 1);
	}
	demod_ssb(bins, baseband_output);
	if (!is_open) {
		speaker_play(baseband_output, BLOCK);
		return;
	}
	for (i = 0; i < BLOCK; i++) {
		if (heard_phase == 0)
			ring_put(&heard, baseband_output[i] / HEARD_FULL_SCALE);
		heard_phase = (heard_phase + 1) % SIGNAL_STEP;
	}
	// speech is played from when enough of it is waiting until it runs
	// out, and the receiver's own audio otherwise
	if (!is_speaking && ring_count(&spoken) >= SPOKEN_LEAST) {
		is_speaking = 1;
		rate_filter_clear(&speech_up);
	}
	if (is_speaking && ring_count(&spoken) == 0 && !atomic_load(&is_synced))
		is_speaking = 0;
	if (is_speaking) {
		for (i = 0; i < BLOCK; i++)
			speech_output[i] = (int32_t)(SPEAKER_A_SAMPLE * rate_filter_up(&speech_up, &spoken));
		speaker_play(speech_output, BLOCK);
	} else {
		speaker_play(baseband_output, BLOCK);
	}
	sem_post(&work);
}

static void tx_rade2(const int32_t *mic, double *samples) {
	int i;

	if (!is_open) {
		memset(samples, 0, BLOCK * sizeof(double));
		return;
	}
	// the first block of a transmission, or the PTT pressed again before
	// the radio had gone back to receive: an over begins
	if (atomic_load(&is_tail_sent) && ++blocks_since_tail > NEW_OVER_AFTER_BLOCKS)
		is_tx_on = 0;
	if (!is_tx_on) {
		is_tx_on = 1;
		blocks_since_tail = 0;
		atomic_store(&is_over_on, 1);
		is_sending = 0;
		is_speaking = 0;
		rate_filter_clear(&mic_down);
		rate_filter_clear(&signal_up);
		atomic_store(&is_tail_sent, 0);
		atomic_store(&tx_begin_wanted, 1);
	}
	for (i = 0; i < BLOCK; i++)
		rate_filter_down(&mic_down, mic[i] / MIC_A_SAMPLE, &said);
	// the signal is sent from when enough of it is waiting; if it runs
	// out, the thread being late, it waits for as much again
	if (!is_sending && ring_count(&sent) >= SENT_LEAST)
		is_sending = 1;
	if (is_sending && ring_count(&sent) == 0) {
		is_sending = 0;
		if (atomic_load(&is_tail_made))
			atomic_store(&is_tail_sent, 1);
	}
	for (i = 0; i < BLOCK; i++)
		samples[i] = is_sending ? TX_LEVEL * rate_filter_up(&signal_up, &sent) : 0.0;
	sem_post(&work);
}

/* the thread that owns the controls */

static struct control rade2_status = {
	.group = "RADE2", .name = "RADE2_STATUS", .kind = CONTROL_TEXT,
	.value = STATUS_QUIET,
};

static void show_status(void) {
	char status[STATUS_ROOM];

	if (atomic_load(&is_synced))
		snprintf(status, sizeof(status), "SYNC %d dB %+d Hz", atomic_load(&snr_db),
			atomic_load(&offset_hz));
	else
		snprintf(status, sizeof(status), "%s", STATUS_QUIET);
	if (!strcmp(status, status_shown))
		return;
	snprintf(status_shown, sizeof(status_shown), "%s", status);
	control_update("RADE2_STATUS", status);
}

// whether the radio is to go on sending: from when the PTT is let go until
// what ends the over has gone
static int rade2_wants_tx(void) {
	show_status();
	return atomic_load(&is_over_on) && atomic_load(&tx_end_wanted)
		&& !atomic_load(&is_tail_sent);
}

// the PTT has been let go
static void rade2_ptt_let_go(void) {
	if (is_open && atomic_load(&is_over_on))
		atomic_store(&tx_end_wanted, 1);
}

static void rade2_select(void) {
	set_passband(PASSBAND_LOW_HZ, PASSBAND_HIGH_HZ);
}

static void rade2_reset(void) {
	atomic_store(&rx_begin_wanted, 1);
	atomic_store(&tx_end_wanted, 0);
}

static void rade2_start(void) {
	control_register(&rade2_status);
	rate_filter_make(&speech_up, SPEECH_STEP, SPEECH_CUTOFF_HZ, SPEECH_STEP);
	rate_filter_make(&signal_up, SIGNAL_STEP, SIGNAL_CUTOFF_HZ, SIGNAL_STEP);
	rate_filter_make(&mic_down, SPEECH_STEP, SPEECH_CUTOFF_HZ, 1.0);
	if (rade2_codec_open() < 0) {
		printf("rade2: the codec would not open, and the mode will be plain SSB\n");
		return;
	}
	sem_init(&work, 0, 0);
	if (pthread_create(&rade2_thread, NULL, rade2_thread_function, NULL) != 0) {
		printf("rade2: no thread could be started, and the mode will be plain SSB\n");
		return;
	}
	is_open = 1;
}

const struct modem modem_rade2 = {
	.name = "RADE2",
	.order = 8,
	.start = rade2_start,
	.select = rade2_select,
	.reset = rade2_reset,
	.rx = rx_rade2,
	.tx = tx_rade2,
	.wants_tx = rade2_wants_tx,
	.tx_end = rade2_ptt_let_go,
};
