// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_rade2.c — the mode RADE2, sent and received through the whole radio.
//
// It needs no radio: the hardware is stood in for, as in test_am.c, and the
// radio is opened as the radio program opens it. Twelve seconds of FreeDV's
// test speech, tests/rade_speech.wav, are spoken into the microphone with
// the radio sending in RADE2, and what the radio gives its transmitter is
// kept. That is then played into its receiver, as if a second radio had
// sent it, and what comes out of the speaker is compared with what was
// said.
//
// The blocks are given at the pace a sound card gives them, one each 10.7
// ms, and no faster: the modem's own thread has to keep up with a radio,
// and this is where it is seen to.
//
// What is asked: that the radio sends a signal, and goes on sending after
// the PTT is let go until the over has been ended, and not for long; that
// the receiver finds the signal within two seconds and says so in
// RADE2_STATUS; that speech comes out of the speaker whose loudness rises
// and falls as the speech that went in, which is how it is known to be that
// speech and not noise, a vocoder not giving back the samples it was given
// (what is played before the signal is found, the receiver's own audio of
// it, a loud buzz, is left out of the comparing);
// and that when the over ends the receiver lets go.
//
// What the speaker played is left in build/rade2_heard.wav, to be listened
// to.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "board.h"
#include "sdr.h"
#include "control.h"
#include "modem.h"
#include "radio.h"
#include "hw_stand_ins.h"

#define SPEECH_FILE "tests/rade_speech.wav"
#define HEARD_FILE "build/rade2_heard.wav"
#define BLOCK 1024
#define SAMPLE_RATE 96000
#define SPEECH_RATE 16000
#define SPEECH_STEP (SAMPLE_RATE / SPEECH_RATE)
#define SPOKEN_S 12
#define QUIET_AFTER_S 2
#define BLOCK_NS (1000000000L * BLOCK / SAMPLE_RATE)
#define NS_PER_S 1000000000L
#define MIC_A_SAMPLE 65536.0
#define RECEIVED_PEAK 2.0e6
#define ENVELOPE_SAMPLES (SAMPLE_RATE / 50)
#define LAG_MOST 100
#define ALIKE_LEAST 0.8
// the speech begins to be played a little after the signal is found
#define SPEAKING_AFTER_FRAMES 30
#define SYNC_WITHIN_S 2.0
#define TAIL_MOST_S 1.0
#define WAV_HEADER_MOST 4096
#define WHY_ROOM 100
#define SPEAKER_TO_16_BITS 32000.0

static int checks_failed;
static struct timespec next_block;
static char why[WHY_ROOM];

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static int16_t *read_speech(const char *path, long *count) {
	unsigned char head[WAV_HEADER_MOST];
	int16_t *samples;
	FILE *file = fopen(path, "rb");
	long got;
	long at;
	long bytes = 0;

	if (file == NULL)
		return NULL;
	got = (long)fread(head, 1, sizeof(head), file);
	for (at = 12; at + 8 <= got; at += 8 + bytes) {
		bytes = head[at + 4] | head[at + 5] << 8 | head[at + 6] << 16 | (long)head[at + 7] << 24;
		if (!memcmp(head + at, "data", 4))
			break;
	}
	if (at + 8 > got) {
		fclose(file);
		return NULL;
	}
	samples = malloc(bytes);
	fseek(file, at + 8, SEEK_SET);
	*count = (long)fread(samples, sizeof(int16_t), bytes / sizeof(int16_t), file);
	fclose(file);
	return samples;
}

static void write_wav(const char *path, const int32_t *samples, long count) {
	FILE *file = fopen(path, "wb");
	uint32_t bytes = (uint32_t)(count * 2);
	uint32_t rate = SAMPLE_RATE;
	uint32_t word;
	uint16_t half;
	int16_t sample;
	double scaled;
	long i;

	if (file == NULL)
		return;
	fwrite("RIFF", 1, 4, file);
	word = 36 + bytes;
	fwrite(&word, 4, 1, file);
	fwrite("WAVEfmt ", 1, 8, file);
	word = 16;
	fwrite(&word, 4, 1, file);
	half = 1;
	fwrite(&half, 2, 1, file);
	fwrite(&half, 2, 1, file);
	fwrite(&rate, 4, 1, file);
	word = rate * 2;
	fwrite(&word, 4, 1, file);
	half = 2;
	fwrite(&half, 2, 1, file);
	half = 16;
	fwrite(&half, 2, 1, file);
	fwrite("data", 1, 4, file);
	fwrite(&bytes, 4, 1, file);
	for (i = 0; i < count; i++) {
		scaled = samples[i] / SPEAKER_TO_16_BITS;
		if (scaled > 32767)
			scaled = 32767;
		if (scaled < -32767)
			scaled = -32767;
		sample = (int16_t)scaled;
		fwrite(&sample, 2, 1, file);
	}
	fclose(file);
}

// one block through the radio, when a sound card would give it
static void one_block(int32_t *from_receiver, int32_t *from_microphone, int32_t *to_speaker,
		int32_t *to_transmitter) {
	struct audio_block block;

	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_block, NULL);
	next_block.tv_nsec += BLOCK_NS;
	if (next_block.tv_nsec >= NS_PER_S) {
		next_block.tv_nsec -= NS_PER_S;
		next_block.tv_sec++;
	}
	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = BLOCK;
	hw_stand_in_block(&block);
}

// how loud a run of samples is, fifty times a second
static long envelope_of(const double *samples, long count, long step, double *envelope) {
	long frames = count / step;
	double sum;
	long f;
	long i;

	for (f = 0; f < frames; f++) {
		sum = 0;
		for (i = 0; i < step; i++)
			sum += samples[f * step + i] * samples[f * step + i];
		envelope[f] = sqrt(sum / step);
	}
	return frames;
}

// how alike two envelopes are, 1 the same and 0 nothing in common: the
// second from its frame b_from on, against the first from wherever within
// LAG_MOST frames of there they are most alike. lag_found is how many
// frames the second is behind the first
static double alike(const double *a, long a_frames, const double *b, long b_frames, long b_from,
		long *lag_found) {
	double best = -1;
	long lag;
	long f;

	for (lag = 0; lag < LAG_MOST && lag <= b_from; lag++) {
		const double *a_at = a + b_from - lag;
		const double *b_at = b + b_from;
		double mean_a = 0, mean_b = 0, ab = 0, aa = 0, bb = 0;
		long n = a_frames - (b_from - lag);

		if (n > b_frames - b_from)
			n = b_frames - b_from;
		if (n < a_frames / 2)
			continue;
		for (f = 0; f < n; f++) {
			mean_a += a_at[f];
			mean_b += b_at[f];
		}
		mean_a /= n;
		mean_b /= n;
		for (f = 0; f < n; f++) {
			ab += (a_at[f] - mean_a) * (b_at[f] - mean_b);
			aa += (a_at[f] - mean_a) * (a_at[f] - mean_a);
			bb += (b_at[f] - mean_b) * (b_at[f] - mean_b);
		}
		if (aa > 0 && bb > 0 && ab / sqrt(aa * bb) > best) {
			best = ab / sqrt(aa * bb);
			*lag_found = lag;
		}
	}
	return best;
}

int main(void) {
	static int32_t silence[BLOCK];
	static int32_t microphone[BLOCK];
	static int32_t to_speaker[BLOCK];
	static int32_t to_transmitter[BLOCK];
	static int32_t from_receiver[BLOCK];
	const struct modem *modem;
	int16_t *speech;
	int32_t *transmitted;
	int32_t *heard;
	double *as_doubles;
	double *said_envelope;
	double *heard_envelope;
	long speech_count = 0;
	long spoken_blocks = (long)SPOKEN_S * SAMPLE_RATE / BLOCK;
	long blocks_most = spoken_blocks + (long)(TAIL_MOST_S + QUIET_AFTER_S + 2) * SAMPLE_RATE / BLOCK;
	long transmitted_blocks = 0;
	long tail_blocks = 0;
	long sent_blocks = 0;
	long synced_at = -1;
	long lost_at = -1;
	long said_frames;
	long heard_frames;
	long lag = 0;
	long b;
	long i;
	double peak = 0;
	double scale;
	double likeness;
	char said[160];

	printf("test_rade2\n");
	speech = read_speech(SPEECH_FILE, &speech_count);
	if (speech == NULL || speech_count < (long)SPOKEN_S * SPEECH_RATE) {
		printf("the test speech could not be read\n");
		return EXIT_FAILURE;
	}
	hw_stand_in_radio(board_zbitx_1.radio);
	if (setup() < 0) {
		printf("could not set up\n");
		return EXIT_FAILURE;
	}
	radio_open();
	modem = modem_find("RADE2");
	expect(modem != NULL, "there is a mode RADE2");
	if (modem == NULL)
		return EXIT_FAILURE;
	control_set("FREQ", "14236000", ORIGIN_NONE, why, sizeof(why));
	expect(control_set("MODE", "RADE2", ORIGIN_NONE, why, sizeof(why)) == 0, "the radio takes it");
	clock_gettime(CLOCK_MONOTONIC, &next_block);
	one_block(silence, silence, to_speaker, to_transmitter);
	expect(rx_list->mode == MODE_USB, "it is on the upper sideband at 14 MHz");
	control_set("FREQ", "7180000", ORIGIN_NONE, why, sizeof(why));
	one_block(silence, silence, to_speaker, to_transmitter);
	expect(rx_list->mode == MODE_LSB, "and on the lower at 7 MHz");
	control_set("FREQ", "14236000", ORIGIN_NONE, why, sizeof(why));

	transmitted = calloc(blocks_most * BLOCK, sizeof(int32_t));
	heard = calloc(blocks_most * BLOCK, sizeof(int32_t));
	as_doubles = calloc(blocks_most * BLOCK, sizeof(double));
	said_envelope = calloc(blocks_most * BLOCK / ENVELOPE_SAMPLES + 1, sizeof(double));
	heard_envelope = calloc(blocks_most * BLOCK / ENVELOPE_SAMPLES + 1, sizeof(double));
	clock_gettime(CLOCK_MONOTONIC, &next_block);
	for (b = 0; b < 20; b++)
		one_block(silence, silence, to_speaker, to_transmitter);

	// sending: the speech into the microphone, each sample six times
	expect(control_set("TX", "ON", ORIGIN_NONE, why, sizeof(why)) == 0, "the mode transmits");
	for (b = 0; b < spoken_blocks; b++) {
		for (i = 0; i < BLOCK; i++)
			microphone[i] = (int32_t)(MIC_A_SAMPLE * speech[(b * BLOCK + i) / SPEECH_STEP]);
		one_block(silence, microphone, to_speaker, transmitted + transmitted_blocks * BLOCK);
		transmitted_blocks++;
		modem->wants_tx();
	}
	// the PTT is let go: the radio program tells the modem, and goes on
	// sending for as long as the modem wants
	modem->tx_end();
	while (modem->wants_tx() && tail_blocks < (long)(2 * TAIL_MOST_S * SAMPLE_RATE / BLOCK)) {
		one_block(silence, silence, to_speaker, transmitted + transmitted_blocks * BLOCK);
		transmitted_blocks++;
		tail_blocks++;
	}
	control_set("TX", "OFF", ORIGIN_NONE, why, sizeof(why));
	for (i = 0; i < transmitted_blocks * BLOCK; i++) {
		if (fabs((double)transmitted[i]) > peak)
			peak = fabs((double)transmitted[i]);
		if (transmitted[i] != 0)
			sent_blocks = i / BLOCK;
	}
	snprintf(said, sizeof(said), "a signal is sent, its peak %.0f dB under the transmitter's full scale",
		-20 * log10((peak > 0 ? peak : 1) / 2147483648.0));
	expect(peak > 0 && peak < 2147483648.0 * 0.9, said);
	snprintf(said, sizeof(said), "after the PTT is let go it sends for %.2f s more, to end the over",
		(double)tail_blocks * BLOCK / SAMPLE_RATE);
	expect(tail_blocks > 0 && tail_blocks < (long)(TAIL_MOST_S * SAMPLE_RATE / BLOCK), said);
	expect(sent_blocks > spoken_blocks - 20, "and the signal lasts as long as the speech");

	// receiving: what was sent, as if from another radio, and quiet after
	for (b = 0; b < 30; b++)
		one_block(silence, silence, to_speaker, to_transmitter);
	scale = RECEIVED_PEAK / (peak > 0 ? peak : 1);
	for (b = 0; b < transmitted_blocks + (long)QUIET_AFTER_S * SAMPLE_RATE / BLOCK; b++) {
		for (i = 0; i < BLOCK; i++)
			from_receiver[i] = b < transmitted_blocks
				? (int32_t)(scale * transmitted[b * BLOCK + i]) : 0;
		one_block(from_receiver, silence, heard + b * BLOCK, to_transmitter);
		modem->wants_tx();
		if (synced_at < 0 && !strncmp(control_get("RADE2_STATUS"), "SYNC", 4)) {
			synced_at = b;
			printf("          found after %.2f s: %s\n", (double)b * BLOCK / SAMPLE_RATE,
				control_get("RADE2_STATUS"));
		}
		if (synced_at >= 0 && lost_at < 0 && strncmp(control_get("RADE2_STATUS"), "SYNC", 4))
			lost_at = b;
	}
	heard_frames = b * BLOCK;
	write_wav(HEARD_FILE, heard, heard_frames);
	expect(synced_at >= 0 && (double)synced_at * BLOCK / SAMPLE_RATE < SYNC_WITHIN_S,
		"the receiver finds the signal within two seconds, and says so");
	snprintf(said, sizeof(said), "it lets go when the over ends (%.2f s after the signal began)",
		(double)lost_at * BLOCK / SAMPLE_RATE);
	expect(lost_at > spoken_blocks - 50 && lost_at < transmitted_blocks + 100, said);

	// is what the speaker played the speech that was said?
	for (i = 0; i < (long)SPOKEN_S * SPEECH_RATE; i++)
		as_doubles[i] = speech[i];
	said_frames = envelope_of(as_doubles, (long)SPOKEN_S * SPEECH_RATE,
		ENVELOPE_SAMPLES / SPEECH_STEP, said_envelope);
	for (i = 0; i < heard_frames; i++)
		as_doubles[i] = heard[i];
	heard_frames = envelope_of(as_doubles, heard_frames, ENVELOPE_SAMPLES, heard_envelope);
	likeness = synced_at < 0 ? 0 : alike(said_envelope, said_frames, heard_envelope, heard_frames,
		synced_at * BLOCK / ENVELOPE_SAMPLES + SPEAKING_AFTER_FRAMES, &lag);
	snprintf(said, sizeof(said),
		"the speaker's loudness follows the speech that was said (alike %.2f, %.2f s late)",
		likeness, lag * 0.02);
	expect(likeness >= ALIKE_LEAST, said);

	teardown();
	if (checks_failed) {
		printf("test_rade2: %d FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("test_rade2: all passed\n");
	return EXIT_SUCCESS;
}
