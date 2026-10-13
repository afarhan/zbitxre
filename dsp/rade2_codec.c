// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rade2_codec.c — speech to RADE version 2's signal, and back.
//
// WHAT IT IS
//
// RADE is FreeDV's digital voice for HF, by David Rowe VK5DGR: speech is
// sent as an OFDM signal some 900 Hz wide that fits inside an SSB
// transmitter's passband, and comes out at the far end as speech, at
// signal levels where SSB is hard to copy. The work is done by code that
// is carried here as its authors wrote it: FreeDV's C port, in
// modems/rade_c, and the part of Opus it runs on, with the FARGAN vocoder
// of Jean-Marc Valin, in modems/opus_dnn. Their READMEs say what each is.
//
// This file is the radio's one way in to them. Their headers bring in
// Opus's, in which there is a kiss_fft.h that is not the radio's; so this
// file alone is built with their include paths, and the rest of the radio
// sees only rade2_codec.h. The Makefile says how.
//
// THE FOUR STAGES
//
//     speech, 16000 a second, 160 samples at a time
//         rade2_tx_analyse()      36 numbers, the features of 10 ms of it
//         rade2_tx_modulate()     every fourth call, 320 samples of signal
//     the signal, 8000 a second, real: the audio an SSB transmitter is given
//         rade2_rx_demodulate()   frames of features, when it has some
//         rade2_rx_synthesise()   160 samples of speech for each
//     speech
//
// The analysing and the synthesising are Rafael Diniz's wrapper for
// Rhizomatica, modems/opus_dnn/radae_vocoder.c.
//
// HOW LARGE THE SIGNAL IS
//
// What rade2_tx_modulate() gives is in the library's own measure, in which
// the signal's level is about one, its peaks under two. The receiver takes
// the same measure: what is given to rade2_rx_demodulate() is doubled
// here, as the library asks of a real signal, of which it works on one
// side of the spectrum. The library's receiver then levels what it is
// given itself, within 20 dB either way.
//
// AN OVER
//
// rade2_tx_begin() starts one: the transmitter is made afresh, as the
// library's authors do, there being no call that clears it.
// rade2_tx_end() gives the frame that says the over has ended, which the
// far receiver reports, here through is_end. rade2_rx_begin() makes the
// receiver afresh. The vocoder has to be given five frames before it
// speaks, at the start and after rade2_rx_synthesis_begin();
// rade2_rx_synthesise() gives nothing for those.
//
// The transmitter and the receiver are apart, and one thread may use the
// one while another uses the other. Neither is for two threads at once.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rade_api.h"
#include "radae_vocoder.h"
#include "rade2_codec.h"

#define REAL_INPUT_GAIN 2.0f
#define NO_MODEL_FILE "the weights are built in"

#if RADE2_SPEECH_FRAME != RADAE_VOCODER_FRAME || RADE2_FEATURES != RADAE_VOCODER_FEATURES
#error "rade2_codec.h and the vocoder disagree about a frame"
#endif

static struct rade *tx;
static struct rade *rx;
static radae_analysis *analysis;
static radae_synthesis *synthesis;
static float *tx_features;
static int tx_features_wanted;
static int tx_features_held;
static RADE_COMP *tx_signal;
static RADE_COMP *rx_signal;
static float *rx_features;

static struct rade *session_open(void) {
	return rade_open(NO_MODEL_FILE, RADE_MODE_V2 | RADE_VERBOSE_0);
}

// 0, or -1 if the library's sizes are not those this file was written for
// or there is no memory
int rade2_codec_open(void) {
	if (tx != NULL)
		return 0;
	rade_initialize();
	tx = session_open();
	rx = session_open();
	analysis = radae_analysis_new();
	synthesis = radae_synthesis_new();
	if (tx == NULL || rx == NULL || analysis == NULL || synthesis == NULL)
		return -1;
	tx_features_wanted = rade_n_features_in_out(tx);
	if (rade_n_tx_out(tx) > RADE2_MODEM_MOST || rade_n_tx_eoo_out(tx) > RADE2_MODEM_MOST
			|| rade_nin_max(rx) > RADE2_MODEM_MOST
			|| rade_n_features_in_out(rx) > RADE2_FRAMES_MOST * RADE2_FEATURES)
		return -1;
	tx_features = calloc(tx_features_wanted, sizeof(float));
	tx_signal = calloc(RADE2_MODEM_MOST, sizeof(RADE_COMP));
	rx_signal = calloc(RADE2_MODEM_MOST, sizeof(RADE_COMP));
	rx_features = calloc(RADE2_FRAMES_MOST * RADE2_FEATURES, sizeof(float));
	if (tx_features == NULL || tx_signal == NULL || rx_signal == NULL || rx_features == NULL)
		return -1;
	return 0;
}

void rade2_codec_close(void) {
	if (tx != NULL)
		rade_close(tx);
	if (rx != NULL)
		rade_close(rx);
	radae_analysis_free(analysis);
	radae_synthesis_free(synthesis);
	free(tx_features);
	free(tx_signal);
	free(rx_signal);
	free(rx_features);
	tx = NULL;
	rx = NULL;
	analysis = NULL;
	synthesis = NULL;
	tx_features = NULL;
	tx_signal = NULL;
	rx_signal = NULL;
	rx_features = NULL;
	rade_finalize();
}

/* sending */

void rade2_tx_begin(void) {
	rade_close(tx);
	tx = session_open();
	radae_analysis_reset(analysis);
	tx_features_held = 0;
}

void rade2_tx_analyse(const int16_t speech[RADE2_SPEECH_FRAME], float features[RADE2_FEATURES]) {
	radae_analysis_frame(analysis, speech, features);
}

static int take_real(const RADE_COMP *signal, int count, float *modem) {
	int i;

	for (i = 0; i < count; i++)
		modem[i] = signal[i].real;
	return count;
}

// a frame of features is taken; when there are enough for a frame of the
// signal, that is given, and its length; otherwise 0
int rade2_tx_modulate(const float features[RADE2_FEATURES], float modem[RADE2_MODEM_MOST]) {
	memcpy(tx_features + tx_features_held, features, RADE2_FEATURES * sizeof(float));
	tx_features_held += RADE2_FEATURES;
	if (tx_features_held < tx_features_wanted)
		return 0;
	tx_features_held = 0;
	return take_real(tx_signal, rade_tx(tx, tx_signal, tx_features), modem);
}

// the frame that ends an over, and its length. Features that had not yet
// made a frame of the signal are let go: under 40 ms of speech
int rade2_tx_end(float modem[RADE2_MODEM_MOST]) {
	tx_features_held = 0;
	return take_real(tx_signal, rade_tx_eoo(tx, tx_signal), modem);
}

/* receiving */

void rade2_rx_begin(void) {
	rade_close(rx);
	rx = session_open();
	rade2_rx_synthesis_begin();
}

// how many samples the next rade2_rx_demodulate() is to be given: it
// changes from call to call, as the receiver follows the sender's clock
int rade2_rx_wanted(void) {
	return rade_nin(rx);
}

// rade2_rx_wanted() samples are taken. What is returned is how many frames
// of features came of them, often none; is_end is set if the sender said
// its over has ended
int rade2_rx_demodulate(const float *modem, float features[RADE2_FRAMES_MOST * RADE2_FEATURES],
		int *is_end) {
	int wanted = rade_nin(rx);
	int floats;
	int i;

	for (i = 0; i < wanted; i++) {
		rx_signal[i].real = REAL_INPUT_GAIN * modem[i];
		rx_signal[i].imag = 0;
	}
	*is_end = 0;
	floats = rade_rx(rx, rx_features, is_end, NULL, rx_signal);
	if (floats <= 0)
		return 0;
	memcpy(features, rx_features, floats * sizeof(float));
	return floats / RADE2_FEATURES;
}

void rade2_rx_synthesis_begin(void) {
	radae_synthesis_reset(synthesis);
}

// RADE2_SPEECH_FRAME, with that much speech, or 0 while the vocoder is
// still being primed
int rade2_rx_synthesise(const float features[RADE2_FEATURES], int16_t speech[RADE2_SPEECH_FRAME]) {
	return radae_synthesis_frame(synthesis, features, speech);
}

int rade2_rx_is_synced(void) {
	return rade_sync(rx) != 0;
}

float rade2_rx_snr_db(void) {
	return rade_snrdB_3k_est(rx);
}

float rade2_rx_offset_hz(void) {
	return rade_freq_offset(rx);
}
