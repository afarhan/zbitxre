// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// signal_block.c — a block of signal processing, and chains of them.
//
// The radio's signal goes through a chain of blocks, as a flow graph does in
// GNU Radio or the audio library of the Teensy. On receive, the demodulator
// feeds the first block and the last block feeds the speaker. Between them
// there may be none, as for SSB, or as many as a mode needs: a decimator, the
// FreeDV decoder, an interpolator.
//
// A BLOCK
//
// A block is a struct signal_block with five things of its own:
//
//   init      is called once, when the chain is first started. It allocates
//             what the block needs, puts it in data, and returns 0, or -1 if
//             it cannot.
//   reset     clears what the block remembers of the signal so far: its
//             filters' history, samples it holds back, a half-made frame. It
//             allocates nothing and frees nothing. It is called after init,
//             and again whenever the signal the chain is given breaks off and
//             starts afresh: on going between receive and transmit, on a new
//             band or a new mode. It may be left out by a block that
//             remembers nothing.
//   process   is given samples, as many as the block before it has, and does
//             its work on them.
//   free      releases what init allocated.
//   data      is the block's own. The block casts it to a struct of its own
//             and keeps its buffers and its state there. Nothing else looks
//             at it, so two blocks of the same kind each have their own.
//
// PARAMETERS
//
// A block is told how to work by a string of parameters, a name and a value
// to each, separated by spaces: "sideband=LSB bw=2400". init and reset are
// both given it. So one demodulator, defined once, is USB in one chain and
// LSB in another, and a new setting, a wider filter or a new pitch, is given
// to a running block by resetting it with new parameters.
//
// A block keeps the string it was defined with in params, and chain_start()
// and chain_reset() hand each block its own. block_reset() resets one block
// with a string given there and then, and makes that the block's string
// from then on. The string must stay as it is for as long as the block may
// be reset with it. param_int() and param_text() read a value out of a
// string; a name that is not there gives what the caller says it should be.
//
// A block passes samples on by calling block_pass(). It may pass on as many
// as it likes, whenever it likes, or none at all. A decimator passes on one
// sample for every eight it is given. The FreeDV decoder takes in 960 samples
// before it passes on 320 of speech. A block that only looks at the signal,
// such as one that decodes FT8 into a list, passes on what it was given.
//
// Samples are floats. A full-scale signal is from -1 to 1.
//
// RATES
//
// rate_in is the rate of the samples a block takes and rate_out the rate of
// those it passes on, in samples a second. They differ only for a block that
// changes the rate. chain_start() checks that each block takes what the one
// before it gives, so that a mistake is found when the chain is made and not
// heard on the air.
//
// THE AUDIO THREAD
//
// A chain is run by the audio thread, which must finish each block of the
// sound card in about 10 ms. So a block's process() must never wait: no
// locks, no files, no allocating. A block whose work takes longer, the FT8
// decoder or FreeDV, hands its samples to a thread of its own through a
// buffer, and takes the results back the same way.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "signal_block.h"

void block_pass(struct signal_block *b, const float *samples, int count) {
	struct signal_block *next = b->next;

	if (next == NULL || count <= 0)
		return;
	next->process(next, samples, count);
}

static int rates_agree(const struct signal_block *first) {
	const struct signal_block *b;

	for (b = first; b != NULL && b->next != NULL; b = b->next) {
		if (b->rate_out != b->next->rate_in)
			return 0;
	}
	return 1;
}

int chain_start(struct signal_block *first) {
	struct signal_block *b;

	if (!rates_agree(first))
		return -1;
	for (b = first; b != NULL; b = b->next)
		b->data = NULL;
	for (b = first; b != NULL; b = b->next) {
		if (b->init != NULL && b->init(b, b->params) < 0) {
			chain_stop(first);
			return -1;
		}
	}
	chain_reset(first);
	return 0;
}

void chain_reset(struct signal_block *first) {
	struct signal_block *b;

	for (b = first; b != NULL; b = b->next) {
		if (b->reset != NULL)
			b->reset(b, b->params);
	}
}

void block_reset(struct signal_block *b, const char *params) {
	b->params = params;
	if (b->reset != NULL)
		b->reset(b, params);
}

static const char *value_of(const char *params, const char *name) {
	size_t length = strlen(name);
	const char *at = params;

	while (at != NULL && *at != 0) {
		while (*at == ' ')
			at++;
		if (strncmp(at, name, length) == 0 && at[length] == '=')
			return at + length + 1;
		at = strchr(at, ' ');
	}
	return NULL;
}

int param_text(const char *params, const char *name, char *value, int room) {
	const char *found = value_of(params, name);
	int length = 0;

	if (found == NULL || room <= 0)
		return -1;
	while (found[length] != 0 && found[length] != ' ' && length < room - 1) {
		value[length] = found[length];
		length++;
	}
	value[length] = 0;
	return 0;
}

int param_int(const char *params, const char *name, int otherwise) {
	char value[PARAMS_MAX];

	if (param_text(params, name, value, sizeof(value)) < 0)
		return otherwise;
	return atoi(value);
}

void chain_stop(struct signal_block *first) {
	struct signal_block *b;

	for (b = first; b != NULL; b = b->next) {
		if (b->free != NULL && b->data != NULL)
			b->free(b);
		b->data = NULL;
	}
}
