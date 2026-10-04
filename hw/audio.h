// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// audio.h — the sound card: samples in and out, and the four levels.
// See audio.c.

#pragma once

#include <stdint.h>

#define AUDIO_RATE_HZ      96000
#define AUDIO_BLOCK_FRAMES 1024
#define AUDIO_LEVEL_MAX    100

struct audio_block {
	const int32_t *from_receiver;
	const int32_t *from_microphone;
	int32_t *to_speaker;
	int32_t *to_transmitter;
	int frames;
};

typedef void (*audio_block_fn)(struct audio_block *block);

struct audio_levels {
	int speaker;
	int transmitter;
	int receiver;
	int microphone;
};

struct audio_report {
	unsigned long blocks;
	unsigned long blocks_dropped;
	unsigned long capture_overruns;
	unsigned long playback_gaps;
	long longest_wait_us;
	long queued_frames;
	int is_realtime;
	int is_running;
};

int audio_open(audio_block_fn radio_block);
void audio_close(void);
int audio_reopen(void);

int audio_set_speaker(int percent);
int audio_set_transmitter(int percent);
int audio_set_receiver(int percent);
int audio_set_microphone(int percent);
int audio_get_levels(struct audio_levels *levels);

void audio_get_report(struct audio_report *report);

// a snapshot is the samples of some blocks as the card gave them, both
// channels, a frame being the receiver's sample and then the microphone's
#define AUDIO_SNAPSHOT_BLOCKS 16
#define AUDIO_SNAPSHOT_FRAMES (AUDIO_SNAPSHOT_BLOCKS * AUDIO_BLOCK_FRAMES)

void audio_take_snapshot(void);
const int32_t *audio_get_snapshot(void);

// a recording is every block as the card gave it, as a snapshot is, for as
// long as it is wanted; the blocks wait in a ring for the thread that
// writes them out
#define AUDIO_RECORD_RING_BLOCKS 1024

void audio_record(int on);

// a recording played back: each block in the ring takes the place of what
// the card captured, both channels, until the ring runs dry
void audio_play(int on);
int audio_play_has_room(void);
void audio_play_put(const int32_t *block);
int audio_play_is_dry(void);
long audio_play_underruns(void);
const int32_t *audio_record_next(void);
void audio_record_taken(void);
long audio_record_dropped(void);
