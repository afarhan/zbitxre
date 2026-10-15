// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rade_codec.c — speech to RADE's signal, versions 1 and 2, and back.
//
// WHAT IT IS
//
// RADE is FreeDV's digital voice for HF, by David Rowe VK5DGR: speech is
// sent as an OFDM signal that fits inside an SSB transmitter's passband,
// and comes out at the far end as speech, at signal levels where SSB is
// hard to copy. The work is done by code that is carried here as its
// authors wrote it: FreeDV's C port, in modems/rade_c, and the part of Opus
// it runs on, with the FARGAN vocoder of Jean-Marc Valin, in
// modems/opus_dnn. Their READMEs say what each is.
//
// This file is the radio's one way in to them. Their headers bring in
// Opus's, in which there is a kiss_fft.h that is not the radio's; so this
// file alone is built with their include paths, and the rest of the radio
// sees only rade_codec.h. The Makefile says how.
//
// TWO VERSIONS
//
// Version 1 is the one FreeDV has released, that operators use on the air
// with freedv-gui 2; its signal carries pilots, some 1500 Hz wide.
// Version 2 is FreeDV's next, still under test, without pilots and some
// 900 Hz wide. The library takes the version when a session is opened, and
// every call after is the same for both; so a codec here is opened for one
// version, and the radio has one of each. modems/modem_rade.c says which
// mode uses which.
//
// THE FOUR STAGES
//
//     speech, 16000 a second, 160 samples at a time
//         rade_codec_tx_analyse()    36 numbers, the features of 10 ms of it
//         rade_codec_tx_modulate()   now and then, a frame of the signal:
//                                    every twelfth call, 960 samples, in
//                                    version 1; every fourth, 320, in 2
//     the signal, 8000 a second, real: the audio an SSB transmitter is given
//         rade_codec_rx_demodulate() frames of features, when it has some
//         rade_codec_rx_synthesise() 160 samples of speech for each
//     speech
//
// The analysing and the synthesising are Rafael Diniz's wrapper for
// Rhizomatica, modems/opus_dnn/radae_vocoder.c.
//
// HOW LARGE THE SIGNAL IS
//
// What rade_codec_tx_modulate() gives is in the library's own measure, in
// which the signal's level is about one, its peaks under two. The receiver
// takes the same measure: what is given to rade_codec_rx_demodulate() is
// doubled here, as the library asks of a real signal, of which its
// receiver, in both versions, filters out one side of the spectrum. The
// library's receiver then levels what it is given itself.
//
// AN OVER
//
// rade_codec_tx_begin() starts one: the transmitter is made afresh, as the
// library's authors do, there being no call that clears it.
// rade_codec_tx_end() gives the frame that says the over has ended, which
// the far receiver reports, here through is_end. rade_codec_rx_begin()
// makes the receiver afresh. The vocoder has to be given five frames before
// it speaks, at the start and after rade_codec_rx_synthesis_begin();
// rade_codec_rx_synthesise() gives nothing for those.
//
// The transmitter and the receiver are apart, and one thread may use the
// one while another uses the other. Neither is for two threads at once.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rade_api.h"
#include "radae_vocoder.h"
#include "rade_codec.h"

#define REAL_INPUT_GAIN 2.0f
#define NO_MODEL_FILE "the weights are built in"

#if RADE_CODEC_SPEECH_FRAME != RADAE_VOCODER_FRAME || RADE_CODEC_FEATURES != RADAE_VOCODER_FEATURES
#error "rade_codec.h and the vocoder disagree about a frame"
#endif

struct rade_codec {
	int flags;
	struct rade *tx;
	struct rade *rx;
	radae_analysis *analysis;
	radae_synthesis *synthesis;
	float *tx_features;
	int tx_features_wanted;
	int tx_features_held;
	RADE_COMP *tx_signal;
	RADE_COMP *rx_signal;
	float *rx_features;
	// the bits version 1's sender puts in the end of an over
	float *rx_eoo_bits;
};

// the library is begun once, for however many codecs are open
static int codecs_open;

static struct rade *session_open(struct rade_codec *c) {
	return rade_open(NO_MODEL_FILE, c->flags);
}

// a codec of the version asked for, or NULL if the library's sizes are not
// those this file was written for or there is no memory
struct rade_codec *rade_codec_open(int version) {
	struct rade_codec *c = calloc(1, sizeof(*c));

	if (c == NULL)
		return NULL;
	if (codecs_open++ == 0)
		rade_initialize();
	c->flags = RADE_VERBOSE_0 | (version == RADE_CODEC_V2 ? RADE_MODE_V2 : 0);
	c->tx = session_open(c);
	c->rx = session_open(c);
	c->analysis = radae_analysis_new();
	c->synthesis = radae_synthesis_new();
	if (c->tx == NULL || c->rx == NULL || c->analysis == NULL || c->synthesis == NULL)
		goto failed;
	c->tx_features_wanted = rade_n_features_in_out(c->tx);
	if (rade_n_tx_out(c->tx) > RADE_CODEC_MODEM_MOST
			|| rade_n_tx_eoo_out(c->tx) > RADE_CODEC_MODEM_MOST
			|| rade_nin_max(c->rx) > RADE_CODEC_MODEM_MOST
			|| rade_n_features_in_out(c->rx) > RADE_CODEC_FRAMES_MOST * RADE_CODEC_FEATURES)
		goto failed;
	c->tx_features = calloc(c->tx_features_wanted, sizeof(float));
	c->tx_signal = calloc(RADE_CODEC_MODEM_MOST, sizeof(RADE_COMP));
	c->rx_signal = calloc(RADE_CODEC_MODEM_MOST, sizeof(RADE_COMP));
	c->rx_features = calloc(RADE_CODEC_FRAMES_MOST * RADE_CODEC_FEATURES, sizeof(float));
	c->rx_eoo_bits = calloc(rade_n_eoo_bits(c->rx) + 1, sizeof(float));
	if (c->tx_features == NULL || c->tx_signal == NULL || c->rx_signal == NULL
			|| c->rx_features == NULL || c->rx_eoo_bits == NULL)
		goto failed;
	return c;
failed:
	rade_codec_close(c);
	return NULL;
}

void rade_codec_close(struct rade_codec *c) {
	if (c == NULL)
		return;
	if (c->tx != NULL)
		rade_close(c->tx);
	if (c->rx != NULL)
		rade_close(c->rx);
	radae_analysis_free(c->analysis);
	radae_synthesis_free(c->synthesis);
	free(c->tx_features);
	free(c->tx_signal);
	free(c->rx_signal);
	free(c->rx_features);
	free(c->rx_eoo_bits);
	free(c);
	if (--codecs_open == 0)
		rade_finalize();
}

/* sending */

void rade_codec_tx_begin(struct rade_codec *c) {
	rade_close(c->tx);
	c->tx = session_open(c);
	radae_analysis_reset(c->analysis);
	c->tx_features_held = 0;
}

void rade_codec_tx_analyse(struct rade_codec *c, const int16_t speech[RADE_CODEC_SPEECH_FRAME],
		float features[RADE_CODEC_FEATURES]) {
	radae_analysis_frame(c->analysis, speech, features);
}

static int take_real(const RADE_COMP *signal, int count, float *modem) {
	int i;

	for (i = 0; i < count; i++)
		modem[i] = signal[i].real;
	return count;
}

// a frame of features is taken; when there are enough for a frame of the
// signal, that is given, and its length; otherwise 0
int rade_codec_tx_modulate(struct rade_codec *c, const float features[RADE_CODEC_FEATURES],
		float modem[RADE_CODEC_MODEM_MOST]) {
	memcpy(c->tx_features + c->tx_features_held, features, RADE_CODEC_FEATURES * sizeof(float));
	c->tx_features_held += RADE_CODEC_FEATURES;
	if (c->tx_features_held < c->tx_features_wanted)
		return 0;
	c->tx_features_held = 0;
	return take_real(c->tx_signal, rade_tx(c->tx, c->tx_signal, c->tx_features), modem);
}

// the frame that ends an over, and its length. Features that had not yet
// made a frame of the signal are let go: under 40 ms of speech in version 2,
// under 120 ms in version 1
int rade_codec_tx_end(struct rade_codec *c, float modem[RADE_CODEC_MODEM_MOST]) {
	c->tx_features_held = 0;
	return take_real(c->tx_signal, rade_tx_eoo(c->tx, c->tx_signal), modem);
}

/* receiving */

void rade_codec_rx_begin(struct rade_codec *c) {
	rade_close(c->rx);
	c->rx = session_open(c);
	rade_codec_rx_synthesis_begin(c);
}

// how many samples the next rade_codec_rx_demodulate() is to be given: it
// changes from call to call, as the receiver follows the sender's clock
int rade_codec_rx_wanted(struct rade_codec *c) {
	return rade_nin(c->rx);
}

// rade_codec_rx_wanted() samples are taken. What is returned is how many
// frames of features came of them, often none; is_end is set if the sender
// said its over has ended
int rade_codec_rx_demodulate(struct rade_codec *c, const float *modem,
		float features[RADE_CODEC_FRAMES_MOST * RADE_CODEC_FEATURES], int *is_end) {
	int wanted = rade_nin(c->rx);
	int floats;
	int i;

	for (i = 0; i < wanted; i++) {
		c->rx_signal[i].real = REAL_INPUT_GAIN * modem[i];
		c->rx_signal[i].imag = 0;
	}
	*is_end = 0;
	// version 1 gives the bits its sender put in the end of an over, which
	// freedv-gui fills with the sender's callsign; they are kept and not
	// yet used
	floats = rade_rx(c->rx, c->rx_features, is_end, c->rx_eoo_bits, c->rx_signal);
	if (floats <= 0)
		return 0;
	memcpy(features, c->rx_features, floats * sizeof(float));
	return floats / RADE_CODEC_FEATURES;
}

void rade_codec_rx_synthesis_begin(struct rade_codec *c) {
	radae_synthesis_reset(c->synthesis);
}

// RADE_CODEC_SPEECH_FRAME, with that much speech, or 0 while the vocoder is
// still being primed
int rade_codec_rx_synthesise(struct rade_codec *c, const float features[RADE_CODEC_FEATURES],
		int16_t speech[RADE_CODEC_SPEECH_FRAME]) {
	return radae_synthesis_frame(c->synthesis, features, speech);
}

int rade_codec_rx_is_synced(struct rade_codec *c) {
	return rade_sync(c->rx) != 0;
}

float rade_codec_rx_snr_db(struct rade_codec *c) {
	return rade_snrdB_3k_est(c->rx);
}

float rade_codec_rx_offset_hz(struct rade_codec *c) {
	return rade_freq_offset(c->rx);
}
