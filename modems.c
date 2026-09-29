// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modems.c — what lies between the demodulator and the speaker.
//
// On receive the radio's signal goes:
//
//     antenna -> RF to baseband -> WM8731 in -> audio thread
//             -> demodulator -> modem -> WM8731 out, to the speaker
//
// The demodulator is rx_linear() in core_sdr.c. It turns the IF into audio,
// filtered and levelled, and at its end it calls modem_rx(), as zbitxv2's
// did, with the audio it has made for the speaker.
//
// The modem is a chain of signal blocks (signal_block.c). What the chain is
// depends on the mode:
//
//   SSB, AM   no chain. The speaker gets the demodulated audio as it is.
//   FT8       the audio goes on to the speaker as it is, and is also decoded
//             into a list of stations for the screen.
//   FreeDV    the audio goes to the FreeDV decoder, and the speaker gets the
//             speech the decoder makes, not the audio.
//
// A chain ends with speaker_block. Whatever reaches it is what the speaker
// plays. A chain may pass on fewer samples than it was given, or more, or
// pass them on later, as FreeDV does, so speaker_block keeps what reaches it
// in a queue, and modem_rx() takes from that queue exactly as many samples as
// the sound card wants. If the queue runs short the rest is silence; the
// shortfall is counted.
//
// With no chain, modem_rx() leaves the samples as they are, and the radio is
// exactly zbitxv2's receiver.
//
// SAMPLES
//
// The demodulator's samples are 32-bit whole numbers. The chain's are floats,
// where the largest 32-bit number is 1. A float carries 24 bits, so on the
// way into a chain and out of it the lowest 8 bits of a sample are lost, some
// 140 dB below full scale.
//
// modem_reset() clears what the chain remembers, so that the signal can start
// afresh. It is for the audio thread, between two blocks, when the radio goes
// between receive and transmit or changes band or mode.
//
// modem_set_rx_chain() is to be called before the sound card is started.
// Changing the chain while the radio runs, as a change of mode will need, is
// not done yet.

#include <stddef.h>
#include <stdlib.h>

#include "modems.h"

#define FULL_SCALE 2147483648.0f
#define LARGEST_SAMPLE 2147483647.0f
#define SMALLEST_SAMPLE (-2147483648.0f)
#define BLOCK_MAX 1024
#define SPEAKER_QUEUE (8 * BLOCK_MAX)

struct speaker_queue {
	float sample[SPEAKER_QUEUE];
	int head;
	int count;
	unsigned long short_by;
};

static struct signal_block *rx_chain;

static int speaker_init(struct signal_block *b, const char *params) {
	(void)params;
	b->data = calloc(1, sizeof(struct speaker_queue));
	if (b->data == NULL)
		return -1;
	return 0;
}

static void speaker_reset(struct signal_block *b, const char *params) {
	struct speaker_queue *q = (struct speaker_queue *)b->data;

	(void)params;
	q->head = 0;
	q->count = 0;
}

static void speaker_process(struct signal_block *b, const float *samples, int count) {
	struct speaker_queue *q = (struct speaker_queue *)b->data;
	int at;
	int i;

	for (i = 0; i < count; i++) {
		if (q->count == SPEAKER_QUEUE) {
			q->head = (q->head + 1) % SPEAKER_QUEUE;
			q->count--;
		}
		at = (q->head + q->count) % SPEAKER_QUEUE;
		q->sample[at] = samples[i];
		q->count++;
	}
}

static void speaker_free(struct signal_block *b) {
	free(b->data);
}

struct signal_block speaker_block = {
	.name = "speaker",
	.rate_in = MODEM_SAMPLE_RATE,
	.rate_out = MODEM_SAMPLE_RATE,
	.init = speaker_init,
	.reset = speaker_reset,
	.process = speaker_process,
	.free = speaker_free,
};

int modem_set_rx_chain(struct signal_block *first) {
	if (rx_chain != NULL)
		chain_stop(rx_chain);
	rx_chain = NULL;
	if (first == NULL)
		return 0;
	if (chain_start(first) < 0)
		return -1;
	rx_chain = first;
	return 0;
}

static int32_t as_whole(float sample) {
	float scaled = sample * FULL_SCALE;

	if (scaled > LARGEST_SAMPLE)
		return (int32_t)LARGEST_SAMPLE;
	if (scaled < SMALLEST_SAMPLE)
		return (int32_t)SMALLEST_SAMPLE;
	return (int32_t)scaled;
}

static void take_from_the_speaker_queue(int32_t *samples, int count) {
	struct speaker_queue *q = (struct speaker_queue *)speaker_block.data;
	int i;

	for (i = 0; i < count; i++) {
		if (q->count == 0) {
			samples[i] = 0;
			q->short_by++;
			continue;
		}
		samples[i] = as_whole(q->sample[q->head]);
		q->head = (q->head + 1) % SPEAKER_QUEUE;
		q->count--;
	}
}

void modem_reset(void) {
	if (rx_chain != NULL)
		chain_reset(rx_chain);
}

void modem_rx(int mode, int32_t *samples, int count) {
	float block[BLOCK_MAX];
	int i;

	(void)mode;
	if (rx_chain == NULL || speaker_block.data == NULL)
		return;
	if (count <= 0 || count > BLOCK_MAX)
		return;
	for (i = 0; i < count; i++)
		block[i] = (float)samples[i] / FULL_SCALE;
	rx_chain->process(rx_chain, block, count);
	take_from_the_speaker_queue(samples, count);
}
