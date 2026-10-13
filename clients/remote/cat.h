// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// cat.h — what another program asks of the radio in Hamlib's words, answered.
// See cat.c.

#pragma once

#define CAT_MODE_ROOM 16
#define CAT_ANSWER_ROOM 1024
#define CAT_FOR_RADIO_ROOM 64

#define CAT_ANSWERED 0
#define CAT_QUIT 1

// what is known of the radio, as its program last said it
struct cat_radio {
	long hz;
	char mode[CAT_MODE_ROOM];
	char vfo;
	int is_sending;
};

int cat_answer(const char *asked, struct cat_radio *radio, char *answer, char *for_radio);
