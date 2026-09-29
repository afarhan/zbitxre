// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// signal_block.h — a block of signal processing, and chains of them. See
// signal_block.c.

#pragma once

#define PARAMS_MAX 128

struct signal_block {
	const char *name;
	int rate_in;
	int rate_out;
	int (*init)(struct signal_block *b, const char *params);
	void (*reset)(struct signal_block *b, const char *params);
	void (*process)(struct signal_block *b, const float *samples, int count);
	void (*free)(struct signal_block *b);
	const char *params;
	void *data;
	struct signal_block *next;
};

void block_pass(struct signal_block *b, const float *samples, int count);

int chain_start(struct signal_block *first);
void chain_reset(struct signal_block *first);
void block_reset(struct signal_block *b, const char *params);

int param_int(const char *params, const char *name, int otherwise);
int param_text(const char *params, const char *name, char *value, int room);
void chain_stop(struct signal_block *first);
