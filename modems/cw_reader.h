// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// cw_reader.h — CW read from the spectrum, the author's way. See
// cw_reader.c.

#pragma once

// the bins the reader is given each tick: the pitch's, and so many each side
#define CW_READER_SPAN 5
#define CW_READER_BINS (2 * CW_READER_SPAN + 3)
#define CW_READER_RUNS_MAX 1024
#define CW_READER_TEXT_ROOM 256
#define CW_READER_CODE_ROOM 12
#define CW_READER_WORD_MARKS 48
#define CW_READER_WORD_TEXT 24
#define CW_READER_LEARNED_MAX 32
#define CW_READER_LEARNED_ROOM 12

struct cw_reader_run {
	int is_mark;
	double seconds;
};

// a word: its marks, the gap after each, in dots, and its letters
struct cw_reader_word {
	char marks[CW_READER_WORD_MARKS];
	double gaps[CW_READER_WORD_MARKS];
	int mark_count;
	char text[CW_READER_WORD_TEXT];
	int text_count;
};

struct cw_reader {
	double tick_s;
	int wpm;
	// the station followed: each bin's ratio to the grass, averaged
	float top_db[CW_READER_BINS];
	float bottom_db[CW_READER_BINS];
	int station;
	// the envelope, in dB over the grass, smoothed, and its two levels
	float recent_db[4];
	int recent_count;
	float previous_db;
	float noise_db;
	float pedestal_db;
	int is_levels_started;
	// the marks and spaces of the over, and where the last edge was
	int is_mark;
	double now_s;
	double edge_s;
	struct cw_reader_run runs[CW_READER_RUNS_MAX];
	int run_count;
	int marks;
	int marks_at_fit;
	double unit_s;
	int is_trained;
	int emitted;
	int is_letter_said;
	int is_word_said;
	char text[CW_READER_TEXT_ROOM];
	int text_count;
	// the word being read, and the one before it, held until this one ends
	struct cw_reader_word word;
	struct cw_reader_word held;
	// callsigns read cleanly, kept as known words
	char learned[CW_READER_LEARNED_MAX][CW_READER_LEARNED_ROOM];
	int learned_seen[CW_READER_LEARNED_MAX];
	int learned_count;
	int learned_next;
};

void cw_reader_init(struct cw_reader *r, int wpm, double tick_s);
void cw_reader_tick(struct cw_reader *r, const float *ratio);
int cw_reader_take(struct cw_reader *r, char *text, int room);
