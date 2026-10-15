// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// telephone.c — the radio's sound made into the telephone's, and back.
//
// WHY
//
// On the radio the sound is whole: 16-bit samples, 48000 a second, as the
// radio program sends it and takes it; rtp_audio.c and rtp_microphone.c
// say how. Across the internet it is the telephone's: G.711 mu-law, a byte
// a sample, 8000 a second, a sixth of the samples and half the bits, 64
// kbit/s where the whole would be 768. The author's design is that the
// radio program knows only the whole, so that other programs on the radio
// can have it, and that the gateway does the turning of one into the
// other, which is this.
//
// A packet either way is twenty milliseconds: 960 samples of the whole in
// 1920 bytes, the more significant byte first, or 160 bytes of mu-law.
//
// FROM THE WHOLE
//
// One sample in six is kept, after a low-pass filter of 96 taps, a
// windowed sinc 3.6 kHz wide; without it whatever is above 4 kHz would
// come out folded down among the speech. It is the filter the radio
// program had, when it made the telephone's sound itself from 96000 a
// second with 192 taps: half the rate, half the taps, the same shape.
//
// TO THE WHOLE
//
// Six samples are made of each one, through a filter of the same shape, so
// that the copies of the speech that lie above 4 kHz are not there.
//
// Each way keeps the last samples it was given, so that one packet runs on
// into the next. The gateway has more than one stream each way: the
// radio's sound for those listening and the owner's speech for VoIP go
// from the whole, the owner's speech to transmit and another's over VoIP
// come to it. So what is kept is a struct telephone, one for each stream,
// begun with telephone_begin(); the filters themselves, made by
// telephone_open(), are the same for all.

#include <math.h>
#include <string.h>

#include "telephone.h"

#define STEP (WHOLE_RATE_HZ / TELEPHONE_RATE_HZ)
#define TAPS TELEPHONE_TAPS
#define TAPS_A_PHASE TELEPHONE_TAPS_A_PHASE
#define CUTOFF_HZ 3600.0
#define HAMMING_A 0.54
#define HAMMING_B 0.46
#define ULAW_BIAS 0x84
#define ULAW_CLIP 32635
#define ULAW_SEGMENTS 8
#define INT16_MOST 32767

static float taps[TAPS];
// the same taps for going up: each of the six samples made of one has its
// own sixteen, and each sixteen add up to one, so that a steady level comes
// up the level it was, the same in all six
static float taps_up[STEP][TAPS_A_PHASE];
static int16_t linear[256];

// a sample as G.711 writes it, mu-law: the sign, three bits for which of
// eight segments its size falls in, and four for where in the segment
static uint8_t ulaw_of(int size) {
	int sign = 0;
	int segment = 0;
	int top;

	if (size < 0) {
		size = -size;
		sign = 0x80;
	}
	if (size > ULAW_CLIP)
		size = ULAW_CLIP;
	size += ULAW_BIAS;
	top = size >> 7;
	while (segment < ULAW_SEGMENTS - 1 && top > 1) {
		top >>= 1;
		segment++;
	}
	return (uint8_t)~(sign | (segment << 4) | ((size >> (segment + 3)) & 0x0f));
}

void telephone_open(void) {
	double centre = (TAPS - 1) / 2.0;
	double wide = 2.0 * CUTOFF_HZ / WHOLE_RATE_HZ;
	double sum = 0;
	double x;
	int value;
	int size;
	int i;

	for (i = 0; i < TAPS; i++) {
		x = i - centre;
		taps[i] = (float)(sin(M_PI * wide * x) / (M_PI * x)
			* (HAMMING_A - HAMMING_B * cos(2.0 * M_PI * i / (TAPS - 1))));
		sum += taps[i];
	}
	for (i = 0; i < TAPS; i++)
		taps[i] = (float)(taps[i] / sum);
	for (i = 0; i < STEP; i++) {
		sum = 0;
		for (value = 0; value < TAPS_A_PHASE; value++)
			sum += taps[i + STEP * value];
		for (value = 0; value < TAPS_A_PHASE; value++)
			taps_up[i][value] = (float)(taps[i + STEP * value] / sum);
	}
	for (i = 0; i < 256; i++) {
		value = ~i & 0xff;
		size = (((value & 0x0f) << 3) + ULAW_BIAS) << ((value & 0x70) >> 4);
		linear[i] = (int16_t)((value & 0x80) ? ULAW_BIAS - size : size - ULAW_BIAS);
	}
}

// a stream begins with nothing kept, as if silence had come before it
void telephone_begin(struct telephone *t) {
	memset(t, 0, sizeof(*t));
	t->until_kept = STEP;
}

// 960 samples of the whole, 1920 bytes, become 160 bytes of mu-law
void telephone_from_whole(struct telephone *t, const uint8_t *whole, uint8_t *telephone) {
	float sum;
	int kept = 0;
	int i;
	int tap;
	int at;

	for (i = 0; i < WHOLE_FRAME_SAMPLES; i++) {
		t->past_whole[t->past_whole_at] = (float)(int16_t)(whole[2 * i] << 8 | whole[2 * i + 1]);
		t->past_whole_at++;
		if (t->past_whole_at == TAPS)
			t->past_whole_at = 0;
		t->until_kept--;
		if (t->until_kept > 0)
			continue;
		t->until_kept = STEP;
		sum = 0;
		at = t->past_whole_at;
		for (tap = 0; tap < TAPS; tap++) {
			sum += taps[tap] * t->past_whole[at];
			at++;
			if (at == TAPS)
				at = 0;
		}
		if (kept < TELEPHONE_FRAME_SAMPLES)
			telephone[kept++] = ulaw_of((int)lrintf(sum));
	}
}

// 160 bytes of mu-law become 960 samples of the whole, 1920 bytes
void telephone_to_whole(struct telephone *t, const uint8_t *telephone, uint8_t *whole) {
	float sum;
	int sample;
	int i;
	int phase;
	int tap;
	int at;

	for (i = 0; i < TELEPHONE_FRAME_SAMPLES; i++) {
		t->past_telephone_at++;
		if (t->past_telephone_at == TAPS_A_PHASE)
			t->past_telephone_at = 0;
		t->past_telephone[t->past_telephone_at] = (float)linear[telephone[i]];
		for (phase = 0; phase < STEP; phase++) {
			// taps_up[phase][j] multiplies the sample j back from the newest
			sum = 0;
			at = t->past_telephone_at;
			for (tap = 0; tap < TAPS_A_PHASE; tap++) {
				sum += taps_up[phase][tap] * t->past_telephone[at];
				at--;
				if (at < 0)
					at = TAPS_A_PHASE - 1;
			}
			sample = (int)lrintf(sum);
			if (sample > INT16_MOST)
				sample = INT16_MOST;
			if (sample < -INT16_MOST)
				sample = -INT16_MOST;
			whole[2 * (i * STEP + phase)] = (uint8_t)((uint16_t)sample >> 8);
			whole[2 * (i * STEP + phase) + 1] = (uint8_t)sample;
		}
	}
}
