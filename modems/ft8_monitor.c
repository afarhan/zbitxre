// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// ft8_monitor.c — the FT8 messages in one slot of sound.
//
// The decoding is Kārlis Goba's, YL3JG: his ft8_lib is in modems/ft8_lib,
// and the monitor and the waterfall here are from the library's example,
// decode_ft8.c, by way of zbitxv2's modem_ft8.c, with their names. MIT
// licence, Copyright (c) 2018 Kārlis Goba; modems/ft8_lib/LICENSE.
//
// sbitx_ft8_decode() is given the sound of a slot, as samples at 12000 a
// second, and gives back the messages it finds in it. It is zbitxv2's
// function of that name, which wrote what it found to the console; this
// one hands it back, and modem_ft8.c says what becomes of it.
//
// HOW A SLOT IS DECODED
//
// FT8 is sent in slots of 15 seconds. A message is 79 symbols, each 0.16
// second long and one of eight tones 6.25 Hz apart, so a signal is 50 Hz
// wide and lasts 12.64 seconds, and many of them fit side by side in the
// 3 kHz a receiver hears.
//
// The monitor turns the sound into a waterfall: for each symbol's length
// of sound a spectrum, a bin to each 6.25 Hz. It makes it twice as fine
// each way, kTime_osr and kFreq_osr: a spectrum every half symbol, and
// bins every 3.125 Hz, since a signal does not begin on the symbol nor sit
// on a bin. The transform is 3840 samples long, two symbols, with a Hann
// window. Each bin is kept as a byte, half a dB to a step.
//
// ft8_find_sync() looks in the waterfall for the Costas pattern, seven
// tones in a fixed order that every message has at its start, its middle
// and its end, at every time and frequency a signal could be, and gives
// the likeliest places, the candidates. ft8_decode() reads the symbols at
// each candidate, corrects the errors with the LDPC code, checks the CRC,
// and unpacks the 77 bits into the text. A signal is found at more than
// one candidate, so a message already found is left out.
//
// WHAT IS DIFFERENT FROM ZBITXV2
//
// The transform is the kiss_fft the radio already has, dsp/kiss_fft, its
// complex one given the samples as real parts. The library used kiss_fft's
// real transform, which the radio does not have; the bins are the same.
//
// The monitor, with its 360 KB of waterfall, is made once and kept, not
// made and freed for each slot. So this is for one thread only.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kiss_fft.h"
#include "ft8_lib/constants.h"
#include "ft8_lib/decode.h"
#include "ft8_monitor.h"

static const int kMin_score = 10; // Minimum sync score threshold for candidates
static const int kLDPC_iterations = 20;

#define kMax_candidates 120
#define kMax_decoded_messages 50

static const int kFreq_osr = 2; // Frequency oversampling rate (bin subdivision)
static const int kTime_osr = 2; // Time oversampling rate (symbol subdivision)

#define DB_FLOOR 1E-12f
#define DB_STEPS_PER_DB 2
#define DB_OFFSET 240
#define MAG_MAX 255

static float hann_i(int i, int N)
{
	float x = sinf((float)M_PI * i / N);
	return x * x;
}

static void waterfall_init(waterfall_t* me, int max_blocks, int num_bins, int time_osr, int freq_osr)
{
	size_t mag_size = max_blocks * time_osr * freq_osr * num_bins * sizeof(me->mag[0]);
	me->max_blocks = max_blocks;
	me->num_blocks = 0;
	me->num_bins = num_bins;
	me->time_osr = time_osr;
	me->freq_osr = freq_osr;
	me->block_stride = (time_osr * freq_osr * num_bins);
	me->mag = (uint8_t *)malloc(mag_size);
}

/// Configuration options for FT4/FT8 monitor
typedef struct
{
	int sample_rate;         ///< Sample rate in Hertz
	int time_osr;            ///< Number of time subdivisions
	int freq_osr;            ///< Number of frequency subdivisions
	ftx_protocol_t protocol; ///< Protocol: FT4 or FT8
} monitor_config_t;

/// FT4/FT8 monitor object that manages DSP processing of incoming audio data
/// and prepares a waterfall object
typedef struct
{
	float symbol_period; ///< FT4/FT8 symbol period in seconds
	int block_size;      ///< Number of samples per symbol (block)
	int subblock_size;   ///< Analysis shift size (number of samples)
	int nfft;            ///< FFT size
	float fft_norm;      ///< FFT normalization factor
	float* window;       ///< Window function for STFT analysis (nfft samples)
	float* last_frame;   ///< Current STFT analysis frame (nfft samples)
	waterfall_t wf;      ///< Waterfall object
	float max_mag;       ///< Maximum detected magnitude (debug stats)

	// KISS FFT housekeeping variables
	kiss_fft_cfg fft_cfg;   ///< Kiss FFT housekeeping object
	kiss_fft_cpx* timedata; ///< the windowed frame, as real parts
	kiss_fft_cpx* freqdata; ///< its transform
} monitor_t;

static void monitor_init(monitor_t* me, const monitor_config_t* cfg)
{
	float slot_time = FT8_SLOT_TIME;
	float symbol_period = FT8_SYMBOL_PERIOD;
	int i;

	if (cfg->protocol == PROTO_FT4) {
		slot_time = FT4_SLOT_TIME;
		symbol_period = FT4_SYMBOL_PERIOD;
	}
	// Compute DSP parameters that depend on the sample rate
	me->block_size = (int)(cfg->sample_rate * symbol_period); // samples corresponding to one FSK symbol
	me->subblock_size = me->block_size / cfg->time_osr;
	me->nfft = me->block_size * cfg->freq_osr;
	me->fft_norm = 2.0f / me->nfft;

	me->window = (float *)malloc(me->nfft * sizeof(me->window[0]));
	for (i = 0; i < me->nfft; ++i)
		me->window[i] = hann_i(i, me->nfft);
	me->last_frame = (float *)calloc(me->nfft, sizeof(me->last_frame[0]));

	me->fft_cfg = kiss_fft_alloc(me->nfft, 0, NULL, NULL);
	me->timedata = (kiss_fft_cpx *)malloc(me->nfft * sizeof(me->timedata[0]));
	me->freqdata = (kiss_fft_cpx *)malloc(me->nfft * sizeof(me->freqdata[0]));

	const int max_blocks = (int)(slot_time / symbol_period);
	const int num_bins = (int)(cfg->sample_rate * symbol_period / 2);
	waterfall_init(&me->wf, max_blocks, num_bins, cfg->time_osr, cfg->freq_osr);
	me->wf.protocol = cfg->protocol;
	me->symbol_period = symbol_period;

	me->max_mag = -120.0f;
}

// Compute FFT magnitudes (log wf) for a frame in the signal and update waterfall data
static void monitor_process(monitor_t* me, const float* frame)
{
	// Check if we can still store more waterfall data
	if (me->wf.num_blocks >= me->wf.max_blocks)
		return;

	int offset = me->wf.num_blocks * me->wf.block_stride;
	int frame_pos = 0;

	// Loop over block subdivisions
	for (int time_sub = 0; time_sub < me->wf.time_osr; ++time_sub)
	{
		// Shift the new data into analysis frame
		for (int pos = 0; pos < me->nfft - me->subblock_size; ++pos)
		{
			me->last_frame[pos] = me->last_frame[pos + me->subblock_size];
		}
		for (int pos = me->nfft - me->subblock_size; pos < me->nfft; ++pos)
		{
			me->last_frame[pos] = frame[frame_pos];
			++frame_pos;
		}

		// Compute windowed analysis frame
		for (int pos = 0; pos < me->nfft; ++pos)
		{
			me->timedata[pos].r = me->fft_norm * me->window[pos] * me->last_frame[pos];
			me->timedata[pos].i = 0;
		}

		kiss_fft(me->fft_cfg, me->timedata, me->freqdata);

		// Loop over two possible frequency bin offsets (for averaging)
		for (int freq_sub = 0; freq_sub < me->wf.freq_osr; ++freq_sub)
		{
			for (int bin = 0; bin < me->wf.num_bins; ++bin)
			{
				int src_bin = (bin * me->wf.freq_osr) + freq_sub;
				float mag2 = (me->freqdata[src_bin].i * me->freqdata[src_bin].i)
					+ (me->freqdata[src_bin].r * me->freqdata[src_bin].r);
				float db = 10.0f * log10f(DB_FLOOR + mag2);
				// Scale decibels to unsigned 8-bit range and clamp the value
				// Range 0-240 covers -120..0 dB in 0.5 dB steps
				int scaled = (int)(DB_STEPS_PER_DB * db + DB_OFFSET);

				if (scaled < 0)
					scaled = 0;
				if (scaled > MAG_MAX)
					scaled = MAG_MAX;
				me->wf.mag[offset] = scaled;
				++offset;

				if (db > me->max_mag)
					me->max_mag = db;
			}
		}
	}

	++me->wf.num_blocks;
}

static void monitor_reset(monitor_t* me)
{
	me->wf.num_blocks = 0;
	me->max_mag = 0;
	memset(me->last_frame, 0, me->nfft * sizeof(me->last_frame[0]));
}

int sbitx_ft8_decode(const float *signal, int num_samples,
		struct ft8_decoded *found, int room)
{
	static monitor_t mon;
	static bool is_mon_made;
	monitor_config_t mon_cfg = {
		.sample_rate = FT8_SAMPLE_RATE,
		.time_osr = kTime_osr,
		.freq_osr = kFreq_osr,
		.protocol = PROTO_FT8,
	};

	if (!is_mon_made) {
		monitor_init(&mon, &mon_cfg);
		is_mon_made = true;
	}
	monitor_reset(&mon);

	// Process the waveform data frame by frame
	for (int frame_pos = 0; frame_pos + mon.block_size <= num_samples; frame_pos += mon.block_size)
		monitor_process(&mon, signal + frame_pos);

	// Find top candidates by Costas sync score and localize them in time and frequency
	candidate_t candidate_list[kMax_candidates];
	int num_candidates = ft8_find_sync(&mon.wf, kMax_candidates, candidate_list, kMin_score);

	// Hash table for decoded messages (to check for duplicates)
	message_t decoded[kMax_decoded_messages];
	message_t* decoded_hashtable[kMax_decoded_messages];

	// Initialize hash table pointers
	for (int i = 0; i < kMax_decoded_messages; ++i)
	{
		decoded_hashtable[i] = NULL;
	}

	int n_decodes = 0;
	// Go over candidates and attempt to decode messages
	for (int idx = 0; idx < num_candidates; ++idx)
	{
		const candidate_t* cand = &candidate_list[idx];
		if (cand->score < kMin_score)
			continue;

		float freq_hz = (cand->freq_offset + (float)cand->freq_sub / mon.wf.freq_osr) / mon.symbol_period;
		float time_sec = (cand->time_offset + (float)cand->time_sub / mon.wf.time_osr) * mon.symbol_period;

		message_t message;
		decode_status_t status;
		if (!ft8_decode(&mon.wf, cand, &message, kLDPC_iterations, &status))
			continue;

		int idx_hash = message.hash % kMax_decoded_messages;
		bool found_empty_slot = false;
		bool found_duplicate = false;
		do {
			if (decoded_hashtable[idx_hash] == NULL) {
				found_empty_slot = true;
			}
			else if ((decoded_hashtable[idx_hash]->hash == message.hash) && (0 == strcmp(decoded_hashtable[idx_hash]->text, message.text))) {
				found_duplicate = true;
			}
			else {
				// Move on to check the next entry in hash table
				idx_hash = (idx_hash + 1) % kMax_decoded_messages;
			}
		} while (!found_empty_slot && !found_duplicate);

		if (found_empty_slot && n_decodes < room && n_decodes < kMax_decoded_messages - 1) {
			// Fill the empty hashtable slot
			memcpy(&decoded[idx_hash], &message, sizeof(message));
			decoded_hashtable[idx_hash] = &decoded[idx_hash];

			found[n_decodes].score = cand->score;
			found[n_decodes].snr = cand->snr;
			found[n_decodes].freq_hz = (int)(freq_hz + 0.5f);
			found[n_decodes].time_sec = time_sec;
			snprintf(found[n_decodes].text, sizeof(found[n_decodes].text), "%s", message.text);
			n_decodes++;
		}
	}

	return n_decodes;
}
