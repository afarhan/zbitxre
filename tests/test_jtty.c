// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_jtty.c — the mode JTTY, sent and read through the whole radio, and
// a recording of WSJT-X's read by the library.
//
// It needs no radio: the hardware is stood in for, as in test_rade.c, and
// the radio is opened as the radio program opens it.
//
// First the controls: JTTY_TX follows JTTY_RX while JTTY_SPLIT is OFF,
// stays where it is while it is ON, and comes back to JTTY_RX when it is
// turned OFF again.
//
// Then a message is typed, JTTY_TEXT, and the radio is let send it, for as
// long as the modem wants to; what it gives its transmitter is kept. It
// must be a signal, last as long as its frames do, and be in its bin in
// small letters. What was sent is then played into the receiver, as if a
// second radio had sent it, with noise added, at the pace a sound card
// gives the blocks, one each 10.7 ms, so that the modem's thread is seen
// to keep up; and the message must be read, in capitals, into the bin of
// the pitch it was sent at. A message typed and then stopped with
// JTTY_DO ESC must not be sent.
//
// Then typing live, as the window's KBD sends with PLAY on: the first part
// of a line is given, JTTY_LIVE, "~" for a space, and sent; after a while
// the rest is typed a character every 0.6 s, more slowly than JTTY sends,
// so that the radio runs out of whole words and sends filler frames; and
// the radio is left to end the message itself when the typing has stopped. What was sent is played
// into the receiver: the line must be read whole in its bin, each word
// whole, the fillers being spaces; the radio must have said, JTTY_SENT,
// every character it took, so that the window's box empties; and it must
// have gone back to receive by itself.
//
// Last, tests/jtty_wsjtx_sample.wav, the sample WSJT-X 3.3 comes with,
// samples/JTTY/260807_134110.wav, is given to the library's receiver as it
// is, at 12000 a second, and must read as WSJT-X reads it, which its user
// guide shows: "RAN ALL NIGHT ON BAND NOISE - NO FALSE DECODES!" at 1507 Hz.

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
#include "modems/jtty_lib/jtty.h"
#include "modems/modem_jtty.h"

#define SAMPLE_FILE "tests/jtty_wsjtx_sample.wav"
#define SAMPLE_SAYS "RAN ALL NIGHT ON BAND NOISE - NO FALSE DECODES!"
#define SAMPLE_HZ 1507
#define SAMPLE_HZ_NEAR 5
#define BLOCK 1024
#define SAMPLE_RATE 96000
#define BLOCK_NS ((long)(1000000000LL * BLOCK / SAMPLE_RATE))
#define NS_PER_S 1000000000L
#define TYPED "CQ VU2ESE VU2ESE"
#define SENT_AT "1500"
#define SENT_AT_HZ 1500
#define SENDING_MOST_S 35
#define QUIET_AFTER_S 3
#define FRAME_S ((double)JTTY_FRAME_SAMPLES / JTTY_SAMPLE_RATE)
#define LEAD_MOST_S 0.5
#define RECEIVED_PEAK 2.0e6
// noise in the 48 kHz the receiver is given, four times the signal's
// strength: some 2 dB under it in the 2500 Hz JTTY is measured in
#define NOISE_RMS 5.6e6
#define NOISE_SEED 4242u
#define WAV_HEADER_MOST 4096
#define WHY_ROOM 100
#define SAID_ROOM 300
#define SHOWN_MOST 120
#define BIN_LOWEST_HZ 200
#define BIN_WIDTH_HZ 125
#define SAMPLE_CHUNK 1000
#define LIVE_FIRST "CQ~CQ~DE~VU2ESE~"
#define LIVE_REST "VU2ESE~PSE~K"
#define LIVE_READ "CQ CQ DE VU2ESE VU2ESE PSE K | "
#define LIVE_HZ "2500"
#define LIVE_PAUSE_S 3
// the rest is typed a character at a time, more slowly than it is sent, so
// that the radio runs out of whole words and sends filler
#define LIVE_TYPING_BLOCKS ((long)SAMPLE_RATE * 6 / 10 / BLOCK)
#define LIVE_MOST_BLOCKS ((long)60 * SAMPLE_RATE / BLOCK)

static int checks_failed;
static struct timespec next_block;
static char why[WHY_ROOM];
static char bin_text[JTTY_BINS][JTTY_BIN_TEXT_ROOM];
static int bin_hz[JTTY_BINS];
static unsigned long told[JTTY_BINS];
static char sample_read[JTTY_BIN_TEXT_ROOM * 2];
static int sample_frames;
static double sample_frequency;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static int16_t *read_wav(const char *path, long *count) {
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

// one block through the radio; paced, when a sound card would give it
static void one_block(int32_t *from_receiver, int32_t *from_microphone, int32_t *to_speaker,
		int32_t *to_transmitter, int is_paced) {
	struct audio_block block;

	if (is_paced) {
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_block, NULL);
		next_block.tv_nsec += BLOCK_NS;
		if (next_block.tv_nsec >= NS_PER_S) {
			next_block.tv_nsec -= NS_PER_S;
			next_block.tv_sec++;
		}
	}
	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = BLOCK;
	hw_stand_in_block(&block);
}

static int keep_bin(int bin, int hz, const char *text) {
	bin_hz[bin] = hz;
	snprintf(bin_text[bin], sizeof(bin_text[bin]), "%s", text);
	return 1;
}

static int is_set_to(const char *name, const char *value) {
	return !strcmp(control_get(name), value);
}

static void test_controls(void) {
	printf(" the controls\n");
	control_set("JTTY_SPLIT", "OFF", ORIGIN_NONE, why, sizeof(why));
	control_set("JTTY_RX", "1800", ORIGIN_NONE, why, sizeof(why));
	expect(is_set_to("JTTY_TX", "1800"), "JTTY_TX follows JTTY_RX while JTTY_SPLIT is OFF");
	control_set("JTTY_SPLIT", "ON", ORIGIN_NONE, why, sizeof(why));
	control_set("JTTY_RX", "900", ORIGIN_NONE, why, sizeof(why));
	expect(is_set_to("JTTY_TX", "1800"), "and stays where it was while it is ON");
	control_set("JTTY_SPLIT", "OFF", ORIGIN_NONE, why, sizeof(why));
	expect(is_set_to("JTTY_TX", "900"), "and comes back to JTTY_RX when it is turned OFF");
	control_set("JTTY_RX", SENT_AT, ORIGIN_NONE, why, sizeof(why));
}

static double gaussian(unsigned *seed) {
	double u = (rand_r(seed) + 1.0) / ((double)RAND_MAX + 2.0);
	double v = (rand_r(seed) + 1.0) / ((double)RAND_MAX + 2.0);

	return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

static void test_sending_and_reading(void) {
	static int32_t silence[BLOCK];
	static int32_t to_speaker[BLOCK];
	static int32_t to_transmitter[BLOCK];
	static int32_t from_receiver[BLOCK];
	const struct modem *modem = modem_find("JTTY");
	long blocks_most = (long)(SENDING_MOST_S + QUIET_AFTER_S) * SAMPLE_RATE / BLOCK;
	long transmitted_blocks = 0;
	long first_sent = -1;
	long last_sent = -1;
	long b;
	long i;
	int32_t *transmitted;
	double peak = 0;
	double scale;
	double lasted_s;
	unsigned seed = NOISE_SEED;
	char said[SAID_ROOM];
	char wanted[SAID_ROOM];
	int bin = (SENT_AT_HZ - BIN_LOWEST_HZ) / BIN_WIDTH_HZ;

	printf(" a message sent, and read back\n");
	expect(modem != NULL, "there is a mode JTTY");
	if (modem == NULL)
		return;
	control_set("FREQ", "14090000", ORIGIN_NONE, why, sizeof(why));
	expect(control_set("MODE", "JTTY", ORIGIN_NONE, why, sizeof(why)) == 0, "the radio takes it");
	test_controls();
	clock_gettime(CLOCK_MONOTONIC, &next_block);
	for (b = 0; b < 20; b++)
		one_block(silence, silence, to_speaker, to_transmitter, 1);
	expect(rx_list->mode == MODE_FT8, "it is heard on the upper sideband, as FT8 is");

	expect(!modem->wants_tx(), "with nothing typed it does not want to send");
	expect(control_set("JTTY_TEXT", TYPED, ORIGIN_NONE, why, sizeof(why)) == 0,
		"it takes a line typed, " TYPED);
	expect(modem->wants_tx(), "and wants to send it");
	transmitted = calloc(blocks_most * BLOCK, sizeof(int32_t));
	control_set("TX", "ON", ORIGIN_NONE, why, sizeof(why));
	while (modem->wants_tx() && transmitted_blocks < blocks_most) {
		one_block(silence, silence, to_speaker, transmitted + transmitted_blocks * BLOCK, 0);
		transmitted_blocks++;
	}
	control_set("TX", "OFF", ORIGIN_NONE, why, sizeof(why));
	for (i = 0; i < transmitted_blocks * BLOCK; i++) {
		if (fabs((double)transmitted[i]) > peak)
			peak = fabs((double)transmitted[i]);
		if (transmitted[i] != 0 && first_sent < 0)
			first_sent = i;
		if (transmitted[i] != 0)
			last_sent = i;
	}
	lasted_s = (double)(last_sent - first_sent) / SAMPLE_RATE;
	if (peak > 0)
		snprintf(said, sizeof(said),
			"a signal is sent, its peak %.0f dB under the transmitter's full scale",
			-20 * log10(peak / 2147483648.0));
	else
		snprintf(said, sizeof(said), "a signal is sent");
	expect(peak > 0 && peak < 2147483648.0 * 0.9, said);
	snprintf(said, sizeof(said), "it begins %.2f s after the radio goes to transmit",
		(double)first_sent / SAMPLE_RATE);
	expect(first_sent >= 0 && (double)first_sent / SAMPLE_RATE < LEAD_MOST_S, said);
	snprintf(said, sizeof(said), "it lasts %.3f s, a whole number of frames of %.3f s",
		lasted_s, FRAME_S);
	expect(lasted_s > FRAME_S * 0.95
		&& fabs(lasted_s / FRAME_S - floor(lasted_s / FRAME_S + 0.5)) < 0.02, said);
	expect(!modem->wants_tx(), "and when it has gone the modem wants to send no more");
	jtty_poll_bins(told, keep_bin);
	snprintf(said, sizeof(said), "it is in its bin, %d, in small letters: \"%.*s\"", bin,
		SHOWN_MOST, bin_text[bin]);
	expect(strstr(bin_text[bin], "cq vu2ese vu2ese") != NULL, said);

	// read back, as if from another radio, with noise
	clock_gettime(CLOCK_MONOTONIC, &next_block);
	scale = 0;
	if (peak > 0)
		scale = RECEIVED_PEAK / peak;
	for (b = 0; b < transmitted_blocks + (long)QUIET_AFTER_S * SAMPLE_RATE / BLOCK; b++) {
		for (i = 0; i < BLOCK; i++) {
			from_receiver[i] = (int32_t)(NOISE_RMS * gaussian(&seed));
			if (b < transmitted_blocks)
				from_receiver[i] += (int32_t)(scale * transmitted[b * BLOCK + i]);
		}
		one_block(from_receiver, silence, to_speaker, to_transmitter, 1);
		modem->wants_tx();
	}
	jtty_poll_bins(told, keep_bin);
	snprintf(wanted, sizeof(wanted), "%s | ", TYPED);
	snprintf(said, sizeof(said), "with noise, it is read into the same bin, at %d Hz: \"%.*s\"",
		bin_hz[bin], SHOWN_MOST, bin_text[bin]);
	expect(strlen(bin_text[bin]) >= strlen(wanted)
		&& !strcmp(bin_text[bin] + strlen(bin_text[bin]) - strlen(wanted), wanted)
		&& abs(bin_hz[bin] - SENT_AT_HZ) <= SAMPLE_HZ_NEAR, said);
	for (i = 0; i < JTTY_BINS; i++) {
		if (i != bin && bin_text[i][0] != 0)
			break;
	}
	expect(i == JTTY_BINS, "and nothing is read in any other bin");

	// stopped before it went
	control_set("JTTY_TEXT", "TNX FER QSO", ORIGIN_NONE, why, sizeof(why));
	expect(modem->wants_tx(), "a second line is to be sent");
	control_set("JTTY_DO", "ESC", ORIGIN_NONE, why, sizeof(why));
	expect(!modem->wants_tx(), "and JTTY_DO ESC stops it");
	jtty_poll_bins(told, keep_bin);
	snprintf(said, sizeof(said), "and its bin says so: \"%.*s\"", SHOWN_MOST, bin_text[bin]);
	expect(strstr(bin_text[bin], " (stopped) | ") != NULL, said);
	free(transmitted);
}

static void type_live(const char *text) {
	char one[2] = { 0, 0 };

	for (; *text != 0; text++) {
		one[0] = *text;
		control_set("JTTY_LIVE", one, ORIGIN_NONE, why, sizeof(why));
	}
}

static void test_live(void) {
	static int32_t silence[BLOCK];
	static int32_t to_speaker[BLOCK];
	static int32_t to_transmitter[BLOCK];
	static int32_t from_receiver[BLOCK];
	const struct modem *modem = modem_find("JTTY");
	int32_t *sent = calloc(LIVE_MOST_BLOCKS * BLOCK, sizeof(int32_t));
	char said_sent[SAID_ROOM] = "";
	const char *rest = LIVE_REST;
	char one[2] = { 0, 0 };
	char taken[SAID_ROOM];
	char said[SAID_ROOM];
	char typed[SAID_ROOM];
	long blocks = 0;
	long pause_blocks = (long)LIVE_PAUSE_S * SAMPLE_RATE / BLOCK;
	long b;
	long i;
	double peak = 0;
	double scale = 0;
	unsigned seed = NOISE_SEED;
	int bin = (atoi(LIVE_HZ) - BIN_LOWEST_HZ) / BIN_WIDTH_HZ;
	int is_wanted;

	printf(" typed live, with a pause in the middle\n");
	if (modem == NULL || sent == NULL)
		return;
	control_set("JTTY_RX", LIVE_HZ, ORIGIN_NONE, why, sizeof(why));
	type_live(LIVE_FIRST);
	expect(modem->wants_tx(), "the first character typed begins an over");
	control_set("TX", "ON", ORIGIN_NONE, why, sizeof(why));
	do {
		if (blocks >= pause_blocks && *rest != 0
				&& (blocks - pause_blocks) % LIVE_TYPING_BLOCKS == 0) {
			one[0] = *rest++;
			type_live(one);
		}
		one_block(silence, silence, to_speaker, sent + blocks * BLOCK, 1);
		blocks++;
		is_wanted = modem->wants_tx();
		while (jtty_take_sent(taken, sizeof(taken)) > 0
				&& strlen(said_sent) + strlen(taken) < sizeof(said_sent))
			memcpy(said_sent + strlen(said_sent), taken, strlen(taken) + 1);
	} while (is_wanted && blocks < LIVE_MOST_BLOCKS);
	control_set("TX", "OFF", ORIGIN_NONE, why, sizeof(why));
	snprintf(said, sizeof(said), "it went back to receive by itself, after %.1f s",
		(double)blocks * BLOCK / SAMPLE_RATE);
	expect(!is_wanted, said);
	snprintf(typed, sizeof(typed), "%s%s", LIVE_FIRST, LIVE_REST);
	for (i = 0; typed[i] != 0; i++) {
		if (typed[i] == '~')
			typed[i] = ' ';
	}
	snprintf(said, sizeof(said), "and said it took every character: \"%s\"", said_sent);
	expect(!strcmp(said_sent, typed), said);

	for (i = 0; i < blocks * BLOCK; i++) {
		if (fabs((double)sent[i]) > peak)
			peak = fabs((double)sent[i]);
	}
	if (peak > 0)
		scale = RECEIVED_PEAK / peak;
	clock_gettime(CLOCK_MONOTONIC, &next_block);
	for (b = 0; b < blocks + (long)QUIET_AFTER_S * SAMPLE_RATE / BLOCK; b++) {
		for (i = 0; i < BLOCK; i++) {
			from_receiver[i] = (int32_t)(NOISE_RMS * gaussian(&seed));
			if (b < blocks)
				from_receiver[i] += (int32_t)(scale * sent[b * BLOCK + i]);
		}
		one_block(from_receiver, silence, to_speaker, to_transmitter, 1);
		modem->wants_tx();
	}
	jtty_poll_bins(told, keep_bin);
	snprintf(said, sizeof(said), "it is read whole in its bin, %d: \"%.*s\"", bin, SHOWN_MOST,
		bin_text[bin]);
	expect(strlen(bin_text[bin]) >= strlen(LIVE_READ)
		&& !strcmp(bin_text[bin] + strlen(bin_text[bin]) - strlen(LIVE_READ), LIVE_READ), said);
	free(sent);
}

static void take_sample_frame(const jtty_frame *frame, void *user) {
	size_t length = strlen(sample_read);

	(void)user;
	snprintf(sample_read + length, sizeof(sample_read) - length, "%s", frame->text);
	sample_frequency = frame->frequency;
	sample_frames++;
}

static void test_wsjtx_sample(void) {
	jtty_rx_config config = { 200.0, 3000.0, 256, 128 };
	jtty_rx *rx = jtty_rx_create(&config);
	float chunk[SAMPLE_CHUNK];
	int16_t *samples;
	long count = 0;
	long at;
	long i;
	char said[SAID_ROOM];

	printf(" WSJT-X's sample, %s\n", SAMPLE_FILE);
	samples = read_wav(SAMPLE_FILE, &count);
	expect(samples != NULL && rx != NULL, "it is read, and a receiver made");
	if (samples == NULL || rx == NULL)
		return;
	for (at = 0; at < count; at += SAMPLE_CHUNK) {
		for (i = 0; i < SAMPLE_CHUNK && at + i < count; i++)
			chunk[i] = samples[at + i] / 32768.0f;
		jtty_rx_process(rx, chunk, (size_t)i, take_sample_frame, NULL);
	}
	jtty_rx_flush(rx, take_sample_frame, NULL);
	jtty_rx_destroy(rx);
	free(samples);
	// the last frame's five characters are filled out with spaces
	i = (long)strlen(sample_read);
	while (i > 0 && sample_read[i - 1] == ' ')
		sample_read[--i] = 0;
	snprintf(said, sizeof(said), "%d frames read at %.0f Hz: \"%.*s\"", sample_frames,
		sample_frequency, SHOWN_MOST, sample_read);
	expect(!strcmp(sample_read, SAMPLE_SAYS)
		&& fabs(sample_frequency - SAMPLE_HZ) <= SAMPLE_HZ_NEAR, said);
}

int main(void) {
	printf("test_jtty\n");
	hw_stand_in_radio(board_zbitx_1.radio);
	if (setup() < 0) {
		printf("could not set up\n");
		return EXIT_FAILURE;
	}
	radio_open();
	test_sending_and_reading();
	test_live();
	teardown();
	test_wsjtx_sample();
	if (checks_failed) {
		printf("test_jtty: %d FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("test_jtty: all passed\n");
	return EXIT_SUCCESS;
}
