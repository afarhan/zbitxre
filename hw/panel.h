// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel.h — the radio's end of the link to the front panel. See panel.c.

#pragma once

#include <stdint.h>

#include "link.h"

struct panel_report {
	unsigned long polls;
	unsigned long answers;
	unsigned long no_answers;
	unsigned long words;
	unsigned long words_not_taken;
	unsigned long frames_taken;
	unsigned long frames_repeated;
	unsigned long frames_spoiled;
	int is_present;
	int is_polling;
};

void panel_open(void);
void panel_set(int control, int32_t value);
void panel_set_bytes(int control, const uint8_t *bytes, int count);
void panel_set_kept_bytes(int control, const uint8_t *bytes, int count);
int panel_is_waiting(int control);
void panel_say(const char *text);
void panel_warn(const char *text);
void panel_also_tell(void (*said)(const char *text), void (*warned)(const char *text),
		void (*qso)(const char *text));
void panel_show_qso(const char *text);
void panel_set_polling(int on);
int panel_poll(long now_ms, link_took_fn took, link_took_bytes_fn took_bytes);
void panel_get_report(struct panel_report *report);
