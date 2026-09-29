// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modems.h — what lies between the demodulator and the speaker. See modems.c.

#pragma once

#include <stdint.h>

#include "signal_block.h"

#define MODEM_SAMPLE_RATE 96000

extern struct signal_block speaker_block;

int modem_set_rx_chain(struct signal_block *first);
void modem_rx(int mode, int32_t *samples, int count);
