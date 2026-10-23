// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_rade.c — RADE1 and RADE2: FreeDV's digital voice, RADE versions 1
// and 2.
//
// WHAT IT IS
//
// RADE is David Rowe VK5DGR's radio autoencoder: speech goes out as an
// OFDM signal between 300 and 2700 Hz of an SSB transmitter's audio, and
// comes out of the far receiver as speech, at signal levels where SSB is
// hard to copy. dsp/rade_codec.c is the radio's way in to the code that
// does it, FreeDV's and Opus's, and says whose each part is.
//
// TWO MODES
//
// RADE1 is version 1, the one FreeDV has released, which operators use on
// the air with freedv-gui 2: it is how the radio talks to them. RADE2 is
// version 2, FreeDV's next, without the pilots of version 1 and narrower.
// FreeDV says version 2's signal may still change, and asks that it not go
// on the air until they release it; RADE2 is for the bench and for trying
// between our own stations until then.
//
// The author asked first, on 6 October 2026, for version 2 alone: version
// 1 searches for a signal all the while it hears none, and that search
// took a processor and a half of the zbitx's Pi Zero 2 W. On 15 October he
// asked for RADE1 on both radios too, for its pilots, which keep its
// timing, and for the stations on the air.
//
// The two modes are one modem: the same thread, rings, filters and
// controls, each mode with a codec of its own version, opened when the
// radio starts. Choosing the mode chooses which codec the thread works
// with. What differs between them is in struct version below: version 1's
// codec works in frames of 120 ms, three times version 2's, so more of its
// signal is held before it is sent and more of its speech before it is
// played.
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
// The speech, while the codec holds a signal, from when a tenth of a
// second of it is waiting, the codec giving it in pieces; and silence
// otherwise. The codec says when it has found a signal and when it has
// lost it, and that is the squelch. The receiver's SSB audio was played
// while there was none, so that a station could be tuned in by ear, a
// buzz; the author asked on 14 October 2026 for the noise to be squelched
// when no one is transmitting, and for the squelch to be a switch in the
// mode's list: SQUELCH, ON as the radio starts. At OFF the SSB audio is
// heard while there is no signal, as before. A station is still found with
// it ON: the codec takes a signal some way off tune, and says how far.
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
// Sending: the peaks of the signal the transmitter is given are put where
// 2TONE's are, which is the drive the transmitter is known to take
// cleanly; TX_LEVEL_V1 says why the two versions differ in it. Version 2's
// level is then 3 dB under 2TONE's, its peaks being 6 dB over its level,
// and version 1's about the same. The
// microphone's samples are brought to 16 bits for the codec, whose
// training was on speech at ordinary levels.
//
// WHAT THE CODEC IS GIVEN TO SAY
//
// On 14 October 2026, recorded during overs, the zbitx's microphone carried
// a hum at 93.75 Hz, the rate of the sound card's blocks, and at twice
// that, 11 dB under the speech in its pauses, and stood a third of the
// codec's full scale off nought. SSB's passband, from 300 Hz, never sends
// either. A cut at 300 Hz was tried first and the author found the voice
// "tinny": the codec was trained on speech whose strength is mostly below
// 300 Hz, FreeDV's own test speech two thirds of it. Then, at the author's
// asking, nothing is cut but the offset, by a high-pass at 10 Hz, far
// below any voice, where it would only eat the codec's room. The hum goes
// to the codec as it comes.
//
// The "background singing" heard on 14 and 15 October, in both modes, was
// a faulty microphone. Its buzz, at the sound card's block rate and its
// harmonics, the codec took for a voice: with nobody speaking the far
// speaker played a voiced drone through every pause, and with digital
// silence sent in its place it played nothing. A 200 Hz high-pass and
// notches at 93.75 and 187.5 Hz were tried and made no difference to the
// author's ear; with the microphones changed, the copy between the author
// and Sasi across the city was excellent. So nothing more is cut.
//
// The same day the two radios' microphones gave speech 20 dB apart at
// nearly the same MIC, 49 and 50, and the sbitx's peaks went to three
// times the codec's full scale and were clipped. A leveller before the
// codec was tried, and the author heard "a background chatter of
// intelligible voices" when he stopped speaking, the pauses' noise lifted
// with the speech; he asked for it to be taken out. MIC sets the level,
// and what goes over the full scale is clipped.
//
// Receiving: the receiver's audio, levelled by its AGC, is divided by
// HEARD_FULL_SCALE to come to the codec near the level it expects, which
// its own levelling then corrects within 20 dB. The figure was set with
// the test, tests/test_rade.c, which puts a signal through the whole
// receiver.
//
// WHAT THE OPERATOR IS TOLD
//
// RADE_STATUS, in the group RADE that both modes share: "--" with no
// signal, and with one,
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
#include "rade_codec.h"

#define AUDIO_RATE_HZ 96000
#define BLOCK (MAX_BINS / 2)
#define PASSBAND_LOW_HZ 250
#define PASSBAND_HIGH_HZ 2750
#define SIGNAL_STEP (AUDIO_RATE_HZ / RADE_CODEC_MODEM_RATE_HZ)
#define SPEECH_STEP (AUDIO_RATE_HZ / RADE_CODEC_SPEECH_RATE_HZ)
#define TAPS_A_PHASE 16
#define SIGNAL_TAPS (SIGNAL_STEP * TAPS_A_PHASE)
#define SPEECH_TAPS (SPEECH_STEP * TAPS_A_PHASE)
#define SIGNAL_CUTOFF_HZ 3600.0
#define SPEECH_CUTOFF_HZ 7200.0
#define HAMMING_A 0.54
#define HAMMING_B 0.46
#define RING_SAMPLES 32768
// speech is played from when this much is waiting: a tenth of a second
// for version 2, whose codec gives 40 ms of it at a time, and a fifth for
// version 1, which gives 120 ms at a time
#define SPOKEN_LEAST_V2 (RADE_CODEC_SPEECH_RATE_HZ / 10)
#define SPOKEN_LEAST_V1 (RADE_CODEC_SPEECH_RATE_HZ / 5)
// the signal is sent from when this much is waiting. Version 2: three of
// its frames, 120 ms. It was two, and on the first day two radios spoke,
// the receiving one lost the signal every few seconds when the zbitx was
// the sender: the signal was thought to be running out, the zbitx's thread
// being late with a frame. What the terminal now says at the end of each
// over is how that is known. Version 1: two of its frames, 240 ms, for the
// same reason, its frames coming three times as far apart
#define SENT_LEAST_V2 960
#define SENT_LEAST_V1 1920
#define TAIL_SAMPLES (RADE_CODEC_MODEM_RATE_HZ / 5)
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
// the microphone's 32 bits to the 16 the codec takes, and ten times that.
// The author had to turn MIC from 40, where it stands for SSB, to 80 for
// the speech to come through whole at the far end, on 14 October 2026, the
// first day two radios spoke in this mode: that is 20 dB of the sound
// card's gain. Speech from a microphone at SSB's setting is that far under
// the sound card's full scale, and the codec, made for speech at ordinary
// levels, took the quieter parts of it for silence. So the speech is
// brought up by that much here, and MIC can stay where SSB has it
#define MIC_A_SAMPLE 6553.6f
#define SPEECH_MOST 32767.0f
#define SPEECH_LOW_HZ 10.0
#define BUTTERWORTH_Q 0.70710678
#define SPEECH_BIQUADS 1
// the codec's peaks put where 2TONE's are, at the transmitter. Both
// versions' samples peak at one, measured on FreeDV's test speech on 15
// October 2026; but what the transmitter sends is the signal's envelope,
// and version 1's, whose sender does not filter it, peaks 3 dB over its
// samples: at version 2's level it reached the transmitter 3 dB over
// 2TONE's peaks (tests/test_rade.c, 25 dB under full scale against 28). So
// it is put 3 dB lower. Its level, 3.8 dB under its samples' peaks against
// version 2's 6.3, then gives about as much power as version 2's
#define TX_LEVEL_V2 0.042
#define TX_LEVEL_V1 0.0297

// what differs between the two modes
struct version {
	const char *mode;
	// how the terminal is told of it
	const char *said_as;
	int codec_version;
	unsigned sent_least;
	unsigned spoken_least;
	// the peaks of its signal put where 2TONE's are
	double tx_level;
	struct rade_codec *codec;
};

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

// a filter of two poles and two zeros, made as Robert Bristow-Johnson's
// cookbook has it
struct biquad {
	float b0, b1, b2, a1, a2;
	float x1, x2, y1, y2;
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

static struct version versions[] = {
	{ "RADE1", "rade1", RADE_CODEC_V1, SENT_LEAST_V1, SPOKEN_LEAST_V1, TX_LEVEL_V1, NULL },
	{ "RADE2", "rade2", RADE_CODEC_V2, SENT_LEAST_V2, SPOKEN_LEAST_V2, TX_LEVEL_V2, NULL },
};
#define VERSION_1 0
#define VERSION_2 1
// which of versions[] is in use: set when a mode is chosen, by the thread
// that owns the controls, and read by the audio thread and the modem's
static atomic_int version_in_use = VERSION_2;

static pthread_t rade_thread;
static sem_t work;
static int is_thread_started;

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
static int32_t silence[BLOCK];
// SQUELCH, set by the thread that owns the controls
static atomic_int is_squelched;
static int heard_phase;
static int is_speaking;
static int is_tx_on;
static int is_sending;
static atomic_int is_tail_sent;
static atomic_int is_over_on;
static int blocks_since_tail;
// counted by the audio thread for the terminal: how often the signal ran
// out while sending, and the speech while a signal was held
static atomic_int sent_ran_out;
static atomic_int spoken_ran_out;
static atomic_int over_blocks;

// the modem thread's
static enum over over;
static struct biquad speech_cut[SPEECH_BIQUADS];
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

static void biquad_make_high_pass(struct biquad *b, double cutoff_hz, double rate_hz, double q) {
	double w = 2 * M_PI * cutoff_hz / rate_hz;
	double alpha = sin(w) / (2 * q);
	double a0 = 1 + alpha;

	memset(b, 0, sizeof(*b));
	b->b0 = (float)((1 + cos(w)) / 2 / a0);
	b->b1 = (float)(-(1 + cos(w)) / a0);
	b->b2 = b->b0;
	b->a1 = (float)(-2 * cos(w) / a0);
	b->a2 = (float)((1 - alpha) / a0);
}

static float biquad_filter(struct biquad *b, float x) {
	float y = b->b0 * x + b->b1 * b->x1 + b->b2 * b->x2 - b->a1 * b->y1 - b->a2 * b->y2;

	b->x2 = b->x1;
	b->x1 = x;
	b->y2 = b->y1;
	b->y1 = y;
	return y;
}

static void biquad_clear(struct biquad *b) {
	b->x1 = b->x2 = b->y1 = b->y2 = 0;
}

/* the thread */

// what has been heard goes to the codec in the pieces it asks for, and the
// speech it makes of them is queued for the speaker
static void receive_heard(struct rade_codec *c) {
	float signal[RADE_CODEC_MODEM_MOST];
	float features[RADE_CODEC_FRAMES_MOST * RADE_CODEC_FEATURES];
	int16_t speech[RADE_CODEC_SPEECH_FRAME];
	int was_synced;
	int is_end;
	int frames;
	int wanted;
	int count;
	int f;
	int i;

	if (atomic_exchange(&rx_begin_wanted, 0)) {
		ring_empty(&heard);
		rade_codec_rx_begin(c);
		atomic_store(&is_synced, 0);
	}
	for (wanted = rade_codec_rx_wanted(c); (int)ring_count(&heard) >= wanted;
			wanted = rade_codec_rx_wanted(c)) {
		for (i = 0; i < wanted; i++)
			signal[i] = ring_take(&heard);
		was_synced = atomic_load(&is_synced);
		frames = rade_codec_rx_demodulate(c, signal, features, &is_end);
		for (f = 0; f < frames; f++) {
			count = rade_codec_rx_synthesise(c, features + f * RADE_CODEC_FEATURES, speech);
			for (i = 0; i < count; i++)
				ring_put(&spoken, speech[i]);
		}
		atomic_store(&snr_db, (int)lroundf(rade_codec_rx_snr_db(c)));
		atomic_store(&offset_hz, (int)lroundf(rade_codec_rx_offset_hz(c)));
		atomic_store(&is_synced, rade_codec_rx_is_synced(c));
		// the next signal is another voice, or the same after a silence:
		// the vocoder begins again
		if (is_end || (was_synced && !rade_codec_rx_is_synced(c)))
			rade_codec_rx_synthesis_begin(c);
	}
}

// what has been said goes to the codec a frame at a time, and the signal
// it makes is queued for the transmitter; at the end of an over, what ends
// it
static void send_said(struct rade_codec *c) {
	float signal[RADE_CODEC_MODEM_MOST];
	float features[RADE_CODEC_FEATURES];
	int16_t speech[RADE_CODEC_SPEECH_FRAME];
	float sample;
	int count;
	int i;
	int k;

	if (atomic_exchange(&tx_begin_wanted, 0)) {
		ring_empty(&said);
		for (i = 0; i < SPEECH_BIQUADS; i++)
			biquad_clear(&speech_cut[i]);
		rade_codec_tx_begin(c);
		atomic_store(&tx_end_wanted, 0);
		atomic_store(&is_tail_made, 0);
		over = OVER_SPEAKING;
	}
	if (over != OVER_SPEAKING) {
		ring_empty(&said);
		return;
	}
	while (ring_count(&said) >= RADE_CODEC_SPEECH_FRAME) {
		for (i = 0; i < RADE_CODEC_SPEECH_FRAME; i++) {
			sample = ring_take(&said);
			for (k = 0; k < SPEECH_BIQUADS; k++)
				sample = biquad_filter(&speech_cut[k], sample);
			if (sample > SPEECH_MOST)
				sample = SPEECH_MOST;
			if (sample < -SPEECH_MOST)
				sample = -SPEECH_MOST;
			speech[i] = (int16_t)sample;
		}
		rade_codec_tx_analyse(c, speech, features);
		count = rade_codec_tx_modulate(c, features, signal);
		for (i = 0; i < count; i++)
			ring_put(&sent, signal[i]);
	}
	if (!atomic_load(&tx_end_wanted))
		return;
	count = rade_codec_tx_end(c, signal);
	for (i = 0; i < count; i++)
		ring_put(&sent, signal[i]);
	for (i = 0; i < TAIL_SAMPLES; i++)
		ring_put(&sent, 0);
	over = OVER_ENDING;
	atomic_store(&is_tail_made, 1);
}

static void *rade_thread_function(void *unused) {
	struct rade_codec *c;

	(void)unused;
	while (1) {
		sem_wait(&work);
		c = versions[atomic_load(&version_in_use)].codec;
		if (c == NULL)
			continue;
		send_said(c);
		receive_heard(c);
	}
	return NULL;
}

// the mode in use
static const struct version *version(void) {
	return &versions[atomic_load(&version_in_use)];
}

// whether the mode in use has its codec and the thread; if not, it is
// plain SSB
static int is_open(void) {
	return is_thread_started && version()->codec != NULL;
}

/* the audio thread */

static void rx_rade(complex float *bins) {
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
	if (!is_open()) {
		speaker_play(baseband_output, BLOCK);
		return;
	}
	for (i = 0; i < BLOCK; i++) {
		if (heard_phase == 0)
			ring_put(&heard, baseband_output[i] / HEARD_FULL_SCALE);
		heard_phase = (heard_phase + 1) % SIGNAL_STEP;
	}
	// speech is played from when enough of it is waiting until it runs
	// out, and otherwise silence, or the SSB audio with SQUELCH OFF
	if (!is_speaking && ring_count(&spoken) >= version()->spoken_least) {
		is_speaking = 1;
		rate_filter_clear(&speech_up);
	}
	if (is_speaking && ring_count(&spoken) == 0) {
		if (atomic_load(&is_synced))
			atomic_fetch_add(&spoken_ran_out, 1);
		else
			is_speaking = 0;
	}
	if (is_speaking) {
		for (i = 0; i < BLOCK; i++)
			speech_output[i] = (int32_t)(SPEAKER_A_SAMPLE * rate_filter_up(&speech_up, &spoken));
		speaker_play(speech_output, BLOCK);
	} else if (atomic_load(&is_squelched)) {
		speaker_play(silence, BLOCK);
	} else {
		speaker_play(baseband_output, BLOCK);
	}
	sem_post(&work);
}

static void tx_rade(const int32_t *mic, double *samples) {
	double level = version()->tx_level;
	int i;

	if (!is_open()) {
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
		atomic_store(&over_blocks, 0);
		atomic_store(&sent_ran_out, 0);
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
	if (!is_sending && ring_count(&sent) >= version()->sent_least)
		is_sending = 1;
	if (is_sending && ring_count(&sent) == 0) {
		is_sending = 0;
		if (atomic_load(&is_tail_made))
			atomic_store(&is_tail_sent, 1);
		else
			atomic_fetch_add(&sent_ran_out, 1);
	}
	atomic_fetch_add(&over_blocks, 1);
	for (i = 0; i < BLOCK; i++)
		samples[i] = is_sending ? level * rate_filter_up(&signal_up, &sent) : 0.0;
	sem_post(&work);
}

/* the thread that owns the controls */

static int apply_squelch(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	atomic_store(&is_squelched, !strcmp(value, "ON"));
	return 0;
}

static struct control squelch_control = {
	.group = "RADE", .name = "SQUELCH", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "ON", .apply = apply_squelch,
};

static struct control rade_status = {
	.group = "RADE", .name = "RADE_STATUS", .kind = CONTROL_TEXT,
	.value = STATUS_QUIET,
};

// what the terminal is told, by the thread that owns the controls, which
// calls this many times a second but not while the PTT is held: when an
// over has been sent, how long it was and
// whether the signal ever ran out; when a signal that was held is lost, how
// long it was held, and whether the speech ever ran out
static void say_what_happened(void) {
	static int was_over_on;
	static int was_synced;
	int is_on = atomic_load(&is_over_on);
	int synced = atomic_load(&is_synced);
	const char *said_as = version()->said_as;

	if (was_over_on && !is_on) {
		printf("%s: sent for %.1f s; the signal ran out %d times\n", said_as,
			atomic_exchange(&over_blocks, 0) * (double)BLOCK / AUDIO_RATE_HZ,
			atomic_exchange(&sent_ran_out, 0));
		fflush(stdout);
	}
	if (!was_synced && synced)
		atomic_store(&spoken_ran_out, 0);
	if (was_synced && !synced) {
		printf("%s: a signal was held and lost, last %d dB %+d Hz; the speech ran out %d times\n",
			said_as, atomic_load(&snr_db), atomic_load(&offset_hz),
			atomic_exchange(&spoken_ran_out, 0));
		fflush(stdout);
	}
	was_over_on = is_on;
	was_synced = synced;
}

static void show_status(void) {
	char status[STATUS_ROOM];

	say_what_happened();

	if (atomic_load(&is_synced))
		snprintf(status, sizeof(status), "SYNC %d dB %+d Hz", atomic_load(&snr_db),
			atomic_load(&offset_hz));
	else
		snprintf(status, sizeof(status), "%s", STATUS_QUIET);
	if (!strcmp(status, status_shown))
		return;
	snprintf(status_shown, sizeof(status_shown), "%s", status);
	control_update("RADE_STATUS", status);
}

// whether the radio is to go on sending: from when the PTT is let go until
// what ends the over has gone
static int rade_wants_tx(void) {
	show_status();
	return atomic_load(&is_over_on) && atomic_load(&tx_end_wanted)
		&& !atomic_load(&is_tail_sent);
}

// the PTT has been let go
static void rade_ptt_let_go(void) {
	if (is_open() && atomic_load(&is_over_on))
		atomic_store(&tx_end_wanted, 1);
}

// the receiver begins afresh, with the codec of the mode chosen
static void rade_begin(int which) {
	atomic_store(&version_in_use, which);
	atomic_store(&is_synced, 0);
	atomic_store(&rx_begin_wanted, 1);
	atomic_store(&tx_end_wanted, 0);
}

static void rade1_select(void) {
	set_passband(PASSBAND_LOW_HZ, PASSBAND_HIGH_HZ);
	rade_begin(VERSION_1);
}

static void rade2_select(void) {
	set_passband(PASSBAND_LOW_HZ, PASSBAND_HIGH_HZ);
	rade_begin(VERSION_2);
}

static void rade1_reset(void) {
	rade_begin(VERSION_1);
}

static void rade2_reset(void) {
	rade_begin(VERSION_2);
}

// what both modes have, made once, when the first of them starts
static void rade_start_shared(void) {
	static int is_made;

	if (is_made)
		return;
	is_made = 1;
	control_register(&rade_status);
	control_register(&squelch_control);
	atomic_store(&is_squelched, !strcmp(squelch_control.value, "ON"));
	rate_filter_make(&speech_up, SPEECH_STEP, SPEECH_CUTOFF_HZ, SPEECH_STEP);
	rate_filter_make(&signal_up, SIGNAL_STEP, SIGNAL_CUTOFF_HZ, SIGNAL_STEP);
	rate_filter_make(&mic_down, SPEECH_STEP, SPEECH_CUTOFF_HZ, 1.0);
	biquad_make_high_pass(&speech_cut[0], SPEECH_LOW_HZ, RADE_CODEC_SPEECH_RATE_HZ, BUTTERWORTH_Q);
	sem_init(&work, 0, 0);
	if (pthread_create(&rade_thread, NULL, rade_thread_function, NULL) != 0) {
		printf("rade: no thread could be started, and RADE1 and RADE2 will be plain SSB\n");
		return;
	}
	is_thread_started = 1;
}

static void rade_start(int which) {
	struct version *v = &versions[which];

	rade_start_shared();
	v->codec = rade_codec_open(v->codec_version);
	if (v->codec == NULL)
		printf("%s: the codec would not open, and the mode will be plain SSB\n", v->said_as);
}

static void rade1_start(void) {
	rade_start(VERSION_1);
}

static void rade2_start(void) {
	rade_start(VERSION_2);
}

const struct modem modem_rade1 = {
	.name = "RADE1",
	.is_spoken = 1,
	.order = 9,
	.start = rade1_start,
	.select = rade1_select,
	.reset = rade1_reset,
	.rx = rx_rade,
	.tx = tx_rade,
	.wants_tx = rade_wants_tx,
	.tx_end = rade_ptt_let_go,
};

const struct modem modem_rade2 = {
	.name = "RADE2",
	.is_spoken = 1,
	.order = 10,
	.start = rade2_start,
	.select = rade2_select,
	.reset = rade2_reset,
	.rx = rx_rade,
	.tx = tx_rade,
	.wants_tx = rade_wants_tx,
	.tx_end = rade_ptt_let_go,
};
