// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// script.c — puts one receiver through a fixed script and writes down
// everything it produces.
//
// It is built twice. With OLD defined it is built with zbitxv2's sbitx.c,
// and without it, with zbitxre's core_sdr.c. The two cannot be in one program
// because they have the same names. Each program writes a file, and the two
// files must be the same but for what single precision loses; run.sh says
// how it is measured, and does all of it.
//
// The script goes through USB, LSB, CW and CWR, the four speeds of the AGC
// and four passbands. AM is left out because there the two differ, and are
// meant to: zbitxv2 filters AM around 24 kHz whatever bin the radio works
// at, and zbitxre around the tuned bin. tests/test_am.c looks after AM.
// For each it plays a quiet, a usual and a very loud stretch of a made-up
// band: three carriers, an AM station and noise, all between 24 and 30 kHz,
// where the radio's IF is.
//
// What is written down: the oscillator's frequency after every request, and
// after every block the samples for the speaker, the samples for the
// transmitter, and the S meter. These are what run.sh compares.
//
// The spectrum is written to a file of its own, the name given second. It
// is not the same, and is not meant to be: zbitxre puts the Hann window on
// the bins where zbitxv2 put it on the samples. run.sh says by how many dB
// the two spectra differ at most.
//
// zbitxv2's receiver takes what FFTW knows from two files, w.wis and wf.wis
// in the directory the program is run from. run.sh runs it once to make
// them before the run it compares.
//
// zbitxv2's sound loop halves the samples before sound_process() sees them.
// zbitxre's sound_block() does the halving itself. So the old receiver is
// given halved samples here and the new one is given them whole.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <complex.h>

#define BLOCK 1024
#define SPECTRUM_FIRST 1269
#define SPECTRUM_COUNT 534
#define SAMPLE_RATE 96000.0
#define ANSWER_MAX 1000
#define REQUEST_MAX 200

#define MODES 4
#define SPEEDS 4
#define PASSBANDS 4

#define USUAL_BLOCKS 40
#define QUIET_BLOCKS 20
#define LOUD_BLOCKS 20
#define LAST_BLOCKS 100
#define USUAL_LEVEL 1.0e8
#define QUIET_LEVEL 2.0e6
#define LOUD_LEVEL 9.0e8

#define CARRIER_ABOVE_HZ 29125.0
#define CARRIER_BELOW_HZ 26625.0
#define CW_NOTE_HZ 28825.0
#define AM_CARRIER_HZ 24000.0
#define AM_TONE_HZ 400.0
#define CARRIER_ABOVE_LEVEL 0.5
#define CARRIER_BELOW_LEVEL 0.3
#define CW_NOTE_LEVEL 0.2
#define AM_LEVEL 0.4
#define AM_DEPTH 0.5
#define NOISE_LEVEL 0.05

#define NOISE_FIRST 12345UL
#define NOISE_MULTIPLIER 1103515245UL
#define NOISE_STEP 12345UL
#define NOISE_SHIFT 8
#define NOISE_MASK 0xffffUL
#define NOISE_HALF 32768.0

extern int spectrum_plot[];
void sdr_request(char *request, char *response);

#ifdef OLD

#include <fftw3.h>

extern char wisdom_file[];
extern char wisdom_file_f[];
void setup(char *device);
void sound_process(int32_t *input_rx, int32_t *input_mic,
	int32_t *output_speaker, int32_t *output_tx, int n_samples);
extern unsigned int oscillator_asked_hz;

static unsigned int oscillator(void) {
	return oscillator_asked_hz;
}

static void open_the_receiver(void) {
	setup("none");
}

static void give_a_block(int32_t *from_receiver, int32_t *from_microphone,
		int32_t *to_speaker, int32_t *to_transmitter) {
	static int32_t halved[BLOCK];
	int i;

	for (i = 0; i < BLOCK; i++)
		halved[i] = from_receiver[i] / 2;
	sound_process(halved, from_microphone, to_speaker, to_transmitter, BLOCK);
}

#else

#include "audio.h"
#include "board.h"
#include "sdr.h"
#include "hw_stand_ins.h"

static unsigned int oscillator(void) {
	return oscillator_frequency();
}

static void open_the_receiver(void) {
	hw_stand_in_radio(board_zbitx_1.radio);
	if (setup() < 0) {
		printf("the receiver did not open\n");
		exit(EXIT_FAILURE);
	}
}

static void give_a_block(int32_t *from_receiver, int32_t *from_microphone,
		int32_t *to_speaker, int32_t *to_transmitter) {
	struct audio_block block;

	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = BLOCK;
	hw_stand_in_block(&block);
}

#endif

static FILE *written;
static FILE *spectrum_written;
static long blocks;
static long loudest;
static unsigned long noise_state = NOISE_FIRST;
static double seconds;

static double noise(void) {
	noise_state = noise_state * NOISE_MULTIPLIER + NOISE_STEP;
	return (double)((noise_state >> NOISE_SHIFT) & NOISE_MASK) / NOISE_HALF - 1.0;
}

static void ask(const char *request) {
	char copy[REQUEST_MAX];
	char answer[ANSWER_MAX] = "";
	unsigned int hz;

	strcpy(copy, request);
	sdr_request(copy, answer);
	hz = oscillator();
	fwrite(&hz, sizeof(hz), 1, written);
}

static void ask_two(const char *key, const char *value) {
	char request[REQUEST_MAX];

	snprintf(request, sizeof(request), "%s=%s", key, value);
	ask(request);
}

static void one_block(int32_t *from_receiver) {
	static int32_t from_microphone[BLOCK];
	static int32_t to_speaker[BLOCK];
	static int32_t to_transmitter[BLOCK];
	char answer[ANSWER_MAX] = "";
	char request[] = "smeter=0";
	int meter;
	int i;

	memset(from_microphone, 0, sizeof(from_microphone));
	give_a_block(from_receiver, from_microphone, to_speaker, to_transmitter);
	blocks++;
	for (i = 0; i < BLOCK; i++) {
		if (labs((long)to_speaker[i]) > loudest)
			loudest = labs((long)to_speaker[i]);
	}

	sdr_request(request, answer);
	meter = atoi(answer);
	fwrite(to_speaker, sizeof(to_speaker), 1, written);
	fwrite(to_transmitter, sizeof(to_transmitter), 1, written);
	fwrite(spectrum_plot + SPECTRUM_FIRST, sizeof(int), SPECTRUM_COUNT, spectrum_written);
	fwrite(&meter, sizeof(meter), 1, written);
}

static double the_band(double t) {
	double am;
	double sum;

	am = 1.0 + AM_DEPTH * sin(2 * M_PI * AM_TONE_HZ * t);
	sum = CARRIER_ABOVE_LEVEL * sin(2 * M_PI * CARRIER_ABOVE_HZ * t);
	sum += CARRIER_BELOW_LEVEL * sin(2 * M_PI * CARRIER_BELOW_HZ * t);
	sum += CW_NOTE_LEVEL * sin(2 * M_PI * CW_NOTE_HZ * t);
	sum += AM_LEVEL * sin(2 * M_PI * AM_CARRIER_HZ * t) * am;
	sum += NOISE_LEVEL * noise();
	return sum;
}

static void play(int count, double level) {
	static int32_t from_receiver[BLOCK];
	int b;
	int i;

	for (b = 0; b < count; b++) {
		for (i = 0; i < BLOCK; i++) {
			from_receiver[i] = (int32_t)(the_band(seconds) * level);
			seconds += 1.0 / SAMPLE_RATE;
		}
		one_block(from_receiver);
	}
}

static void every_setting(void) {
	static const char *modes[MODES] = { "USB", "LSB", "CW", "CWR" };
	static const char *speeds[SPEEDS] = { "SLOW", "MED", "FAST", "OFF" };
	static const char *lows[PASSBANDS] = { "300", "50", "400", "100" };
	static const char *highs[PASSBANDS] = { "3000", "2400", "900", "4500" };
	int m;
	int s;
	int p;

	for (m = 0; m < MODES; m++) {
		ask_two("r1:mode", modes[m]);
		ask("r1:freq=7074000");
		ask("r1:freq=14200000");
		for (s = 0; s < SPEEDS; s++) {
			ask_two("r1:agc", speeds[s]);
			for (p = 0; p < PASSBANDS; p++) {
				ask_two("r1:low", lows[p]);
				ask_two("r1:high", highs[p]);
				play(USUAL_BLOCKS, USUAL_LEVEL);
				play(QUIET_BLOCKS, QUIET_LEVEL);
				play(LOUD_BLOCKS, LOUD_LEVEL);
			}
		}
	}
}

int main(int argc, char **argv) {
	if (argc < 3) {
		printf("script <file to write> <file for the spectrum>\n");
		return EXIT_FAILURE;
	}
	written = fopen(argv[1], "wb");
	spectrum_written = fopen(argv[2], "wb");
	if (written == NULL || spectrum_written == NULL) {
		printf("cannot write %s\n", argv[1]);
		return EXIT_FAILURE;
	}
#ifdef OLD
	strcpy(wisdom_file, "w.wis");
	strcpy(wisdom_file_f, "wf.wis");
#endif
	open_the_receiver();

	ask("r1:freq=7040000");
	ask("r1:gain=60");
	ask("rx_pitch=700");
	every_setting();

	ask("rx_pitch=600");
	ask("r1:mode=CW");
	ask("r1:freq=7030000");
	ask("r1:gain=80");
	play(LAST_BLOCKS, USUAL_LEVEL);

	fclose(written);
	fclose(spectrum_written);
	printf("%ld blocks, %ld samples, the loudest %ld\n",
		blocks, blocks * BLOCK, loudest);
	return EXIT_SUCCESS;
}
