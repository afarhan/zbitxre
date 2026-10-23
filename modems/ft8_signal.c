// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// ft8_signal.c — the sound of an FT8 message, to be sent.
//
// sbitx_ft8_encode() is zbitxv2's function of that name. The message is
// packed into its 77 bits and coded into 79 tones by Kārlis Goba's ft8_lib,
// modems/ft8_lib, and the tones are made into sound by synth_gfsk() and
// gfsk_pulse() from the library's example, gen_ft8.c, by way of zbitxv2:
// the tone glides from one symbol to the next through a Gaussian filter,
// which is what keeps an FT8 signal 50 Hz wide. MIT licence, Copyright (c)
// 2018 Kārlis Goba; modems/ft8_lib/LICENSE.
//
// What is different from zbitxv2: the sound is made at the rate it is
// asked for, the sound card's 96000 a second, and not at 12000 to be
// stretched eight times, which put images of the signal every 12 kHz for
// the filter to take out; and it is only the 12.64 seconds of the message,
// without the silence zbitxv2 put around it to fill the slot. When it
// begins is modem_ft8.c's business.
//
// JTTY's tones glide in the same way, with the same filter, and
// modem_jtty.c makes its sound with synth_gfsk() too, given its own tones,
// its symbol of 32 ms and its pitch.
//
// A MESSAGE THAT CANNOT BE PACKED IS REFUSED
//
// The library packs every kind of message WSJT-X sends: /P and /R, CQ
// POTA and CQ DX, a nonstandard callsign with the other as a hash, and
// free text of up to 13 characters. A message that fits none of them is
// refused by the library. And to be sure nothing goes out that was not
// meant, the packed bits are unpacked again here; if what comes back is
// not the message, but for the angle brackets that mark a callsign sent as
// a hash, the message is refused.
//
// Which callsign goes as a hash is the library's choice, by WSJT-X's rules:
// it is given the callsigns written plainly, "W9XYZ PJ4/K1ABC -05", and
// sends PJ4/K1ABC as a hash, which is heard as "W9XYZ <PJ4/K1ABC> -05". A
// message written with the brackets, as one is heard, has them taken off
// before it is packed.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ft8_lib/constants.h"
#include "ft8_lib/encode.h"
#include "ft8_lib/message.h"
#include "ft8_callsign_hash.h"
#include "ft8_signal.h"

#define FT8_SYMBOL_BT 2.0f ///< symbol smoothing filter bandwidth factor (BT)
#define GFSK_CONST_K 5.336446f ///< == pi * sqrt(2 / log(2))
#define PULSE_SYMBOLS 3
#define RAMP_DIVISOR 8
#define SAMPLES_PER_SYMBOL_MAX 15360

static float pulse[PULSE_SYMBOLS * SAMPLES_PER_SYMBOL_MAX];

/// Computes a GFSK smoothing pulse.
/// The pulse is theoretically infinitely long, however, here it's truncated
/// at 3 times the symbol length.
static void gfsk_pulse(int n_spsym, float symbol_bt, float *pulse)
{
	for (int i = 0; i < PULSE_SYMBOLS * n_spsym; ++i)
	{
		float t = i / (float)n_spsym - 1.5f;
		float arg1 = GFSK_CONST_K * symbol_bt * (t + 0.5f);
		float arg2 = GFSK_CONST_K * symbol_bt * (t - 0.5f);
		pulse[i] = (erff(arg1) - erff(arg2)) / 2;
	}
}

/// Synthesize waveform data using GFSK phase shaping.
/// The output waveform will contain n_sym symbols. The frequency of each
/// sample is worked out as it is needed, from the symbols around it, where
/// the library kept them all in an array as long as the sound.
void synth_gfsk(const uint8_t *symbols, int n_sym, float f0, float symbol_bt,
	float symbol_period, int signal_rate, float *signal)
{
	int n_spsym = (int)(0.5f + signal_rate * symbol_period); // Samples per symbol
	int n_wave = n_sym * n_spsym;                            // Number of output samples
	float hmod = 1.0f;
	float dphi_peak = 2 * M_PI * hmod / n_spsym;
	float dphi_f0 = 2 * M_PI * f0 / signal_rate;
	float phi = 0;

	gfsk_pulse(n_spsym, symbol_bt, pulse);

	// Each sample's phase step is f0's and the pulses of the symbols whose
	// pulse reaches it, which are three long: its own symbol's and the two
	// before. In the library the sound is extended by a symbol at each end,
	// a dummy with the first symbol's tone before and one with the last's
	// after; j is where sample k is in that, and symbol i's pulse begins at
	// i symbols into it, i from -1, the dummy before, to n_sym, the one after
	for (int k = 0; k < n_wave; ++k)
	{
		int j = k + n_spsym;
		float dphi = dphi_f0;

		for (int i = j / n_spsym - 2; i <= j / n_spsym; ++i)
		{
			int symbol = i;
			int at = j - i * n_spsym;

			if (symbol < 0)
				symbol = 0;
			if (symbol > n_sym - 1)
				symbol = n_sym - 1;
			if (at >= 0 && at < PULSE_SYMBOLS * n_spsym)
				dphi += dphi_peak * symbols[symbol] * pulse[at];
		}
		signal[k] = sinf(phi);
		phi = fmodf(phi + dphi, 2 * M_PI);
	}

	// Apply envelope shaping to the first and last symbols
	int n_ramp = n_spsym / RAMP_DIVISOR;
	for (int i = 0; i < n_ramp; ++i)
	{
		float env = (1 - cosf(2 * M_PI * i / (2 * n_ramp))) / 2;
		signal[i] *= env;
		signal[n_wave - 1 - i] *= env;
	}
}

// a text without the angle brackets of hashed callsigns
static void without_brackets(char *bare, const char *text) {
	while (*text != 0) {
		if (*text != '<' && *text != '>')
			*bare++ = *text;
		text++;
	}
	*bare = 0;
}

// whether the bits packed are the message, but for the brackets
static int is_the_message(const ftx_message_t *packed, const char *message) {
	char unpacked[FTX_MAX_MESSAGE_LENGTH];
	char a[FTX_MAX_MESSAGE_LENGTH];
	char b[FTX_MAX_MESSAGE_LENGTH];
	ftx_message_offsets_t offsets;

	if (strlen(message) >= sizeof(b))
		return 0;
	if (ftx_message_decode(packed, ft8_callsign_hashes(), unpacked, &offsets)
			!= FTX_MESSAGE_RC_OK)
		return 0;
	without_brackets(a, unpacked);
	without_brackets(b, message);
	return !strcmp(a, b);
}

// how many samples a message takes at the rate given
int ft8_signal_samples(int sample_rate) {
	return FT8_NN * (int)(0.5f + sample_rate * FT8_SYMBOL_PERIOD);
}

// the message is made into sound at its pitch: as many samples as
// ft8_signal_samples() says, between -1 and 1. Says how many, or -1 if the
// message cannot be sent as it is written, or there is not room for it
int sbitx_ft8_encode(const char *message, int pitch_hz, int sample_rate,
		float *signal, int room) {
	ftx_message_t packed;
	char plain[FTX_MAX_MESSAGE_LENGTH];
	uint8_t tones[FT8_NN];
	int samples = ft8_signal_samples(sample_rate);

	if (samples > room || samples / FT8_NN > SAMPLES_PER_SYMBOL_MAX)
		return -1;
	if (strlen(message) >= sizeof(plain))
		return -1;
	without_brackets(plain, message);
	if (ftx_message_encode(&packed, ft8_callsign_hashes(), plain) != FTX_MESSAGE_RC_OK)
		return -1;
	if (!is_the_message(&packed, message))
		return -1;
	ft8_encode(packed.payload, tones);
	synth_gfsk(tones, FT8_NN, (float)pitch_hz, FT8_SYMBOL_BT, FT8_SYMBOL_PERIOD,
		sample_rate, signal);
	return samples;
}
