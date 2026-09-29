// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_chain.c — chains of signal blocks, between the demodulator and the
// speaker, with no radio.
//
// It builds chains of small blocks made here and hands modem_rx() blocks of
// samples as the demodulator would. The checks:
//
//   with no chain, the samples come back untouched;
//   a block that scales passes on its samples, scaled;
//   two blocks of the same kind each keep their own data;
//   a block that passes on a quarter of its samples, followed by one that
//   passes each on four times, gives back as many as went in;
//   a block that holds samples back, as FreeDV does, gives silence at first
//   and then its samples, one block late, none lost;
//   chain_start() refuses a chain whose rates do not agree;
//   a block whose init fails stops the chain from starting, and the blocks
//   already made ready are freed.
//
// Exits 0 when every check passes.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modems.h"
#include "signal_block.h"

#define BLOCK 1024
#define QUARTER_RATE (MODEM_SAMPLE_RATE / 4)
#define HELD_BACK 2000
#define TEST_LEVEL 1000000
#define LOST_BITS 256

static int checks_failed;
static int frees;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

struct scale_data {
	float by;
};

static int scale_init(struct signal_block *b) {
	struct scale_data *d = malloc(sizeof(struct scale_data));

	if (d == NULL)
		return -1;
	d->by = 1.0f;
	b->data = d;
	return 0;
}

static void scale_process(struct signal_block *b, const float *samples, int count) {
	struct scale_data *d = (struct scale_data *)b->data;
	float out[BLOCK];
	int i;

	for (i = 0; i < count; i++)
		out[i] = samples[i] * d->by;
	block_pass(b, out, count);
}

static void counted_free(struct signal_block *b) {
	free(b->data);
	frees++;
}

static void quarter_process(struct signal_block *b, const float *samples, int count) {
	float out[BLOCK];
	int kept = 0;
	int i;

	for (i = 0; i < count; i += 4)
		out[kept++] = samples[i];
	block_pass(b, out, kept);
}

static void fourfold_process(struct signal_block *b, const float *samples, int count) {
	float out[BLOCK * 4];
	int i;
	int j;

	for (i = 0; i < count; i++) {
		for (j = 0; j < 4; j++)
			out[i * 4 + j] = samples[i];
	}
	block_pass(b, out, count * 4);
}

struct hold_data {
	float held[HELD_BACK + BLOCK];
	int count;
};

static int hold_init(struct signal_block *b) {
	b->data = calloc(1, sizeof(struct hold_data));
	if (b->data == NULL)
		return -1;
	return 0;
}

static void hold_process(struct signal_block *b, const float *samples, int count) {
	struct hold_data *d = (struct hold_data *)b->data;

	memcpy(d->held + d->count, samples, count * sizeof(float));
	d->count += count;
	if (d->count < HELD_BACK)
		return;
	block_pass(b, d->held, d->count);
	d->count = 0;
}

static int failing_init(struct signal_block *b) {
	(void)b;
	return -1;
}

static struct signal_block block_of(const char *name, int rate_in, int rate_out,
		int (*init)(struct signal_block *),
		void (*process)(struct signal_block *, const float *, int)) {
	struct signal_block b;

	memset(&b, 0, sizeof(b));
	b.name = name;
	b.rate_in = rate_in;
	b.rate_out = rate_out;
	b.init = init;
	b.process = process;
	b.free = counted_free;
	return b;
}

static int32_t value(int k) {
	return (int32_t)((long)k * TEST_LEVEL / BLOCK);
}

static void fill(int32_t *samples, int first) {
	int i;

	for (i = 0; i < BLOCK; i++)
		samples[i] = value(first + i);
}

static int close_to(int32_t got, int32_t wanted) {
	return labs((long)got - (long)wanted) <= LOST_BITS;
}

static void check_no_chain(void) {
	int32_t samples[BLOCK];
	int32_t before[BLOCK];

	modem_set_rx_chain(NULL);
	fill(samples, 1);
	memcpy(before, samples, sizeof(samples));
	modem_rx(0, samples, BLOCK);
	expect(memcmp(samples, before, sizeof(samples)) == 0,
		"with no chain the samples come back untouched");
}

static void check_scaling(void) {
	struct signal_block half = block_of("half", MODEM_SAMPLE_RATE,
		MODEM_SAMPLE_RATE, scale_init, scale_process);
	struct signal_block third = block_of("third", MODEM_SAMPLE_RATE,
		MODEM_SAMPLE_RATE, scale_init, scale_process);
	int32_t samples[BLOCK];
	int ok = 1;
	int i;

	half.next = &third;
	third.next = &speaker_block;
	expect(modem_set_rx_chain(&half) == 0, "a chain of three blocks starts");
	((struct scale_data *)half.data)->by = 0.5f;
	((struct scale_data *)third.data)->by = 1.0f / 3.0f;
	fill(samples, 1);
	modem_rx(0, samples, BLOCK);
	for (i = 0; i < BLOCK; i++) {
		if (!close_to(samples[i], (int32_t)((1 + i) * (TEST_LEVEL / BLOCK) / 6.0)))
			ok = 0;
	}
	expect(ok, "two scaling blocks, each with its own data, scale by a sixth");
	modem_set_rx_chain(NULL);
}

static void check_rates(void) {
	struct signal_block down = block_of("quarter", MODEM_SAMPLE_RATE,
		QUARTER_RATE, NULL, quarter_process);
	struct signal_block up = block_of("fourfold", QUARTER_RATE,
		MODEM_SAMPLE_RATE, NULL, fourfold_process);
	int32_t samples[BLOCK];
	int ok = 1;
	int i;

	down.next = &up;
	up.next = &speaker_block;
	expect(modem_set_rx_chain(&down) == 0,
		"a chain that goes down to a quarter of the rate and back starts");
	fill(samples, 0);
	modem_rx(0, samples, BLOCK);
	for (i = 0; i < BLOCK; i++) {
		if (!close_to(samples[i], value(i / 4 * 4)))
			ok = 0;
	}
	expect(ok, "as many samples come back as went in, every fourth repeated");
	modem_set_rx_chain(NULL);

	down.next = &speaker_block;
	expect(modem_set_rx_chain(&down) < 0,
		"a chain whose rates do not agree does not start");
}

static void check_holding_back(void) {
	struct signal_block hold = block_of("hold", MODEM_SAMPLE_RATE,
		MODEM_SAMPLE_RATE, hold_init, hold_process);
	int32_t samples[BLOCK];
	int silent_first = 1;
	int then_sound = 1;
	int round;
	int i;

	hold.next = &speaker_block;
	modem_set_rx_chain(&hold);
	fill(samples, 0);
	modem_rx(0, samples, BLOCK);
	for (i = 0; i < BLOCK; i++) {
		if (samples[i] != 0)
			silent_first = 0;
	}
	expect(silent_first, "a block that holds samples back gives silence at first");

	for (round = 1; round < 4; round++) {
		fill(samples, round * BLOCK);
		modem_rx(0, samples, BLOCK);
		for (i = 0; i < BLOCK; i++) {
			if (!close_to(samples[i], value((round - 1) * BLOCK + i)))
				then_sound = 0;
		}
	}
	expect(then_sound, "and then its samples, one block late, none lost");
	modem_set_rx_chain(NULL);
}

static void check_failing_init(void) {
	struct signal_block made = block_of("made", MODEM_SAMPLE_RATE,
		MODEM_SAMPLE_RATE, scale_init, scale_process);
	struct signal_block broken = block_of("broken", MODEM_SAMPLE_RATE,
		MODEM_SAMPLE_RATE, failing_init, scale_process);

	made.next = &broken;
	broken.next = &speaker_block;
	frees = 0;
	expect(modem_set_rx_chain(&made) < 0, "a block that cannot start stops the chain");
	expect(frees == 1 && made.data == NULL,
		"and the block already started is freed");
}

int main(void) {
	check_no_chain();
	check_scaling();
	check_rates();
	check_holding_back();
	check_failing_init();

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("signal blocks: all checks passed\n");
	return EXIT_SUCCESS;
}
