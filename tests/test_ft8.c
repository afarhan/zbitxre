// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_ft8.c — FT8 messages are made into sound, and the decoder is to find
// them in it. It needs no radio.
//
// A slot of 15 seconds is filled with noise, and into it are put messages
// as ft8_lib encodes them, each at its own pitch, beginning half a second
// into the slot as a station's does. modems/ft8_monitor.c is given the
// slot and has to give the messages back, each at the pitch it was sent
// at. A slot of noise alone has to give none.
//
// The sound of a message is made by modems/ft8_signal.c, which is what the
// radio sends: at 12000 samples a second, and at the sound card's 96000,
// kept one in eight as the receiver keeps it. The kinds of message the
// library could not pack before it was brought up to date are made and
// found: CQ POTA, /P, a nonstandard callsign, and that callsign sent as a
// hash, which is found by the callsign heard in full before it. What
// cannot be packed at all has to be refused.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_monitor.h"
#include "ft8_signal.h"

#define SLOT_SAMPLES (FT8_SAMPLE_RATE * 15)
#define STARTS_AT_SAMPLE (FT8_SAMPLE_RATE / 2)
#define FOUND_MAX 20
#define PITCH_WITHIN_HZ 4
#define NOISE_LEVEL 0.05f
#define STRONG 0.1f
#define WEAK 0.01f
#define NOWHERE (-1)

#define SOUND_CARD_RATE 96000
#define DECIMATION (SOUND_CARD_RATE / FT8_SAMPLE_RATE)
// 79 symbols of 1920 samples at 12000 a second, and eight times that
#define WAVE_ROOM 151680
#define WAVE_96K_ROOM (WAVE_ROOM * DECIMATION)

static float slot[SLOT_SAMPLES];
static float wave[WAVE_ROOM];
static float wave_96k[WAVE_96K_ROOM];
static struct ft8_decoded found[FOUND_MAX];
static int found_count;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static void fill_with_noise(void) {
	int i;

	srand(1);
	for (i = 0; i < SLOT_SAMPLES; i++)
		slot[i] = NOISE_LEVEL * (2.0f * rand() / RAND_MAX - 1.0f);
}

// a message is added to the slot, at a pitch and a strength
static int add_message(const char *text, int pitch_hz, float strength) {
	int samples = sbitx_ft8_encode(text, pitch_hz, FT8_SAMPLE_RATE, wave, WAVE_ROOM);
	int i;

	if (samples < 0)
		return -1;
	for (i = 0; i < samples; i++)
		slot[STARTS_AT_SAMPLE + i] += strength * wave[i];
	return 0;
}

// a message joined late: what is left of it after the seconds missed
static int add_message_late(const char *text, int pitch_hz, float strength, double missed_s) {
	int samples = sbitx_ft8_encode(text, pitch_hz, FT8_SAMPLE_RATE, wave, WAVE_ROOM);
	int i;

	if (samples < 0)
		return -1;
	for (i = (int)(missed_s * FT8_SAMPLE_RATE); i < samples; i++)
		slot[STARTS_AT_SAMPLE + i] += strength * wave[i];
	return 0;
}

// a message made at the sound card's rate, and kept one sample in eight,
// as the receiver does
static int add_message_at_96k(const char *text, int pitch_hz, float strength) {
	int samples = sbitx_ft8_encode(text, pitch_hz, SOUND_CARD_RATE, wave_96k, WAVE_96K_ROOM);
	int i;

	if (samples < 0)
		return -1;
	for (i = 0; i < samples / DECIMATION; i++)
		slot[STARTS_AT_SAMPLE + i] += strength * wave_96k[i * DECIMATION];
	return 0;
}

static int find_message(const char *text) {
	int i;

	for (i = 0; i < found_count; i++) {
		if (!strcmp(found[i].text, text))
			return i;
	}
	return NOWHERE;
}

static int is_found_at(const char *text, int pitch_hz) {
	int which = find_message(text);

	if (which == NOWHERE)
		return 0;
	return abs(found[which].freq_hz - pitch_hz) <= PITCH_WITHIN_HZ;
}

int main(void) {
	int strong;
	int weak;
	int i;

	printf("ft8:\n");

	fill_with_noise();
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	expect(found_count == 0, "a slot of noise has no message in it");

	fill_with_noise();
	expect(add_message("CQ VU2ESE MK97", 1200, STRONG) == 0
		&& add_message("VU2XZ VU2ESE -12", 1750, STRONG) == 0
		&& add_message("K1ABC W9XYZ RR73", 437, WEAK) == 0,
		"three messages are made into sound");
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	for (i = 0; i < found_count; i++)
		printf("          %+3d dB %4d Hz %4.1f s  %s\n", found[i].snr, found[i].freq_hz,
			found[i].time_sec, found[i].text);
	expect(found_count == 3, "three are found, each once");
	expect(is_found_at("CQ VU2ESE MK97", 1200), "a CQ, at the pitch it was sent at");
	expect(is_found_at("VU2XZ VU2ESE -12", 1750), "a report, beside it");
	expect(is_found_at("K1ABC W9XYZ RR73", 437),
		"and one a tenth as strong, a fifth of the noise");
	strong = find_message("CQ VU2ESE MK97");
	weak = find_message("K1ABC W9XYZ RR73");
	expect(strong != NOWHERE && weak != NOWHERE && found[strong].snr > found[weak].snr,
		"the stronger is given the better signal to noise");

	fill_with_noise();
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	expect(found_count == 0, "the next slot, of noise, has nothing left over in it");

	fill_with_noise();
	expect(add_message_at_96k("G0XYZ VU2ESE R-07", 1500, STRONG) == 0,
		"a message is made at the sound card's rate");
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	expect(found_count == 1 && is_found_at("G0XYZ VU2ESE R-07", 1500),
		"and is found in what the receiver keeps of it");

	fill_with_noise();
	expect(add_message("CQ POTA VU2ESE MK97", 600, STRONG) == 0
		&& add_message("G0XYZ VU2ESE/P R-05", 1100, STRONG) == 0
		&& add_message("CQ PJ4/K1ABC", 1600, STRONG) == 0,
		"a CQ POTA, a /P and a nonstandard callsign are made into sound");
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	for (i = 0; i < found_count; i++)
		printf("          %+3d dB %4d Hz %4.1f s  %s\n", found[i].snr, found[i].freq_hz,
			found[i].time_sec, found[i].text);
	expect(is_found_at("CQ POTA VU2ESE MK97", 600) && is_found_at("G0XYZ VU2ESE/P R-05", 1100)
		&& is_found_at("CQ PJ4/K1ABC", 1600), "and are found as they were sent");

	fill_with_noise();
	expect(add_message("W9XYZ PJ4/K1ABC -05", 900, STRONG) == 0,
		"a report from the nonstandard callsign is made, the callsign as a hash");
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	expect(found_count == 1 && !strcmp(found[0].text, "W9XYZ <PJ4/K1ABC> -05"),
		"and its hash is turned back into the callsign heard before");

	expect(add_message("HELLO THERE FRIEND", 1000, STRONG) < 0,
		"a text longer than free text allows is refused, not sent cut short");
	expect(add_message("G0XYZ VU2ESE RR73", 1000, STRONG) == 0
		&& add_message("CQ VU2ESE MK97", 1000, STRONG) == 0
		&& add_message("G0XYZ VU2ESE +05", 1000, STRONG) == 0,
		"and the messages of a standard contact are made");

	// how late a message may be joined, which ft8_qso.c's LATE_START_S rests on
	fill_with_noise();
	add_message_late("G0XYZ VU2ESE MK97", 1500, WEAK, 3.5);
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	expect(is_found_at("G0XYZ VU2ESE MK97", 1500),
		"a weak message with its first 3.5 s missing is still read");
	fill_with_noise();
	add_message_late("G0XYZ VU2ESE MK97", 1500, STRONG, 6.0);
	found_count = sbitx_ft8_decode(slot, SLOT_SAMPLES, found, FOUND_MAX);
	expect(find_message("G0XYZ VU2ESE MK97") == NOWHERE,
		"with its first 6 s missing it is not, however strong: it is sent once, not thrice");

	if (checks_failed) {
		printf("ft8: %d checks FAILED\n", checks_failed);
		return 1;
	}
	printf("ft8: all checks passed\n");
	return 0;
}
