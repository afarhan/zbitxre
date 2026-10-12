// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// linking.h — getting the radio its key from my.sbitx.net, and keeping it.
// See linking.c.

#pragma once

#define LINKING_KEY_ID_ROOM 24
#define LINKING_KEY_ROOM 96
#define LINKING_STATUS_ROOM 32

enum linking_result {
	LINKING_IDLE,
	LINKING_UNDER_WAY,
	LINKING_LINKED,
	LINKING_FAILED,
};

void linking_open(const char *web_url, const char *key_file, const char *name,
	const char *radio_or_client);
int linking_read_key(char *key_id, char *key);
void linking_forget_key(void);
void linking_begin(void);
void linking_stop(void);
enum linking_result linking_poll(long now_ms, char *status);
