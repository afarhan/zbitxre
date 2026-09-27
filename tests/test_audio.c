// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_audio.c — the sound card, on the radio.
//
// Stop the radio program first; this test owns the card while it runs.
//
// It checks two things.
//
// The levels. Each of the four is set to a different number and read back
// from the chip, and then each is moved alone to see that the other three
// stay where they were. That is what lets the radio leave the levels alone
// when it goes between receive and transmit.
//
// The stream. The card runs for some seconds with a block function that
// measures what comes in on each input and sends silence out. The test then
// asks how many blocks went through, how long the longest wait for one was,
// and whether the card ever ran dry or overflowed. A block is 10.67 ms, so a
// wait much longer than that means the thread was held up.
//
// A late block. The block function is then made to hold one block back for
// 25 ms, longer than anything queued to play. The card must carry on, the gap
// must be counted, the blocks that piled up must be dropped, and what is
// queued to play must come back to where it was, so that one hold-up does not
// leave the radio's delay longer for good.
//
// With "tone" the block function also plays a 700 Hz tone in the speaker for
// the first second of that run, which is the only check here that a person
// can hear. Nothing is ever sent to the transmitter.
//
//     test_audio [seconds]
//     test_audio tone [seconds]
//
// The test puts the four levels back to what they were when the audio layer
// had just opened. Exits 0 when every check passes.

#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"

#define DEFAULT_SECONDS     10
#define TONE_HZ             700.0
#define TONE_SECONDS        1
#define TONE_AMPLITUDE      200000000.0
#define TONE_SPEAKER_LEVEL  80
#define TWO_PI              6.283185307179586

#define SPEAKER_LEVEL       40
#define TRANSMITTER_LEVEL   95
#define RECEIVER_LEVEL      60
#define MICROPHONE_LEVEL    30
#define MOVED_BY            20
#define ONE_INPUT_STEP      4

#define BLOCK_US            (1000000L * AUDIO_BLOCK_FRAMES / AUDIO_RATE_HZ)
#define LONGEST_WAIT_OK_US  (BLOCK_US * 3 / 2)
#define BLOCKS_TOLERANCE    3
#define US_PER_SECOND       1000000L
#define NS_PER_US           1000L
#define TIMED_LEVEL_WRITES  20
#define HELD_BACK_US        25000
#define SECONDS_AFTER       2
#define MOST_QUEUED         (2 * AUDIO_BLOCK_FRAMES)

static atomic_long tone_frames_left;
static atomic_int hold_one_back;
static atomic_long receiver_peak;
static atomic_long microphone_peak;
static atomic_long transmitter_peak;
static long frames_played;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static long largest(const int32_t *samples, int count, long so_far) {
	long size;
	int i;

	for (i = 0; i < count; i++) {
		size = samples[i];
		if (size == INT32_MIN)
			size = INT32_MAX;
		if (size < 0)
			size = -size;
		if (size > so_far)
			so_far = size;
	}
	return so_far;
}

static void measure_and_play(struct audio_block *block) {
	double angle;
	int i;

	if (atomic_exchange(&hold_one_back, 0))
		usleep(HELD_BACK_US);

	atomic_store(&receiver_peak, largest(block->from_receiver,
		block->frames, atomic_load(&receiver_peak)));
	atomic_store(&microphone_peak, largest(block->from_microphone,
		block->frames, atomic_load(&microphone_peak)));

	if (atomic_load(&tone_frames_left) > 0) {
		for (i = 0; i < block->frames; i++) {
			angle = TWO_PI * TONE_HZ * (frames_played + i) / AUDIO_RATE_HZ;
			block->to_speaker[i] = (int32_t)(TONE_AMPLITUDE * sin(angle));
		}
		atomic_fetch_sub(&tone_frames_left, block->frames);
	}
	frames_played += block->frames;

	atomic_store(&transmitter_peak, largest(block->to_transmitter,
		block->frames, atomic_load(&transmitter_peak)));
}

static int near(int got, int wanted) {
	int difference;

	difference = got - wanted;
	if (difference < 0)
		difference = -difference;
	return difference <= ONE_INPUT_STEP;
}

static int levels_are(int speaker, int transmitter, int receiver,
		int microphone) {
	struct audio_levels now;

	if (audio_get_levels(&now) < 0)
		return 0;
	return near(now.speaker, speaker) && near(now.transmitter, transmitter)
		&& near(now.receiver, receiver) && near(now.microphone, microphone);
}

static void check_levels(void) {
	int ok;

	ok = audio_set_speaker(SPEAKER_LEVEL) == 0;
	ok = ok && audio_set_transmitter(TRANSMITTER_LEVEL) == 0;
	ok = ok && audio_set_receiver(RECEIVER_LEVEL) == 0;
	ok = ok && audio_set_microphone(MICROPHONE_LEVEL) == 0;
	expect(ok, "the chip accepts four levels");
	expect(levels_are(SPEAKER_LEVEL, TRANSMITTER_LEVEL, RECEIVER_LEVEL,
		MICROPHONE_LEVEL), "four different levels read back as set");

	audio_set_speaker(SPEAKER_LEVEL + MOVED_BY);
	expect(levels_are(SPEAKER_LEVEL + MOVED_BY, TRANSMITTER_LEVEL,
		RECEIVER_LEVEL, MICROPHONE_LEVEL),
		"moving the speaker leaves the transmitter alone");
	audio_set_speaker(SPEAKER_LEVEL);

	audio_set_transmitter(TRANSMITTER_LEVEL - MOVED_BY);
	expect(levels_are(SPEAKER_LEVEL, TRANSMITTER_LEVEL - MOVED_BY,
		RECEIVER_LEVEL, MICROPHONE_LEVEL),
		"moving the transmitter leaves the speaker alone");
	audio_set_transmitter(TRANSMITTER_LEVEL);

	audio_set_receiver(RECEIVER_LEVEL + MOVED_BY);
	expect(levels_are(SPEAKER_LEVEL, TRANSMITTER_LEVEL,
		RECEIVER_LEVEL + MOVED_BY, MICROPHONE_LEVEL),
		"moving the receiver leaves the microphone alone");
	audio_set_receiver(RECEIVER_LEVEL);

	audio_set_microphone(MICROPHONE_LEVEL + MOVED_BY);
	expect(levels_are(SPEAKER_LEVEL, TRANSMITTER_LEVEL, RECEIVER_LEVEL,
		MICROPHONE_LEVEL + MOVED_BY),
		"moving the microphone leaves the receiver alone");
	audio_set_microphone(MICROPHONE_LEVEL);

	expect(audio_set_speaker(AUDIO_LEVEL_MAX + 1) < 0,
		"a level above 100 is refused");
	expect(audio_set_speaker(-1) < 0, "a level below 0 is refused");
}

static void time_a_level_write(void) {
	struct timespec before;
	struct timespec after;
	long took;
	int i;

	clock_gettime(CLOCK_MONOTONIC, &before);
	for (i = 0; i < TIMED_LEVEL_WRITES; i++)
		audio_set_speaker(SPEAKER_LEVEL + i % 2);
	clock_gettime(CLOCK_MONOTONIC, &after);
	took = (after.tv_sec - before.tv_sec) * US_PER_SECOND
		+ (after.tv_nsec - before.tv_nsec) / NS_PER_US;
	printf("          writing one level takes %ld us\n",
		took / TIMED_LEVEL_WRITES);
	audio_set_speaker(SPEAKER_LEVEL);
}

static void check_stream(int seconds) {
	struct audio_report before;
	struct audio_report after;
	long expected;
	long went_through;

	audio_get_report(&before);
	sleep(seconds);
	audio_get_report(&after);

	went_through = (long)(after.blocks - before.blocks);
	expected = (long)seconds * AUDIO_RATE_HZ / AUDIO_BLOCK_FRAMES;
	printf("          %ld blocks in %d seconds, %ld expected\n",
		went_through, seconds, expected);
	printf("          the longest wait for a block was %ld us; a block is %ld us\n",
		after.longest_wait_us, BLOCK_US);
	printf("          largest sample: receiver %ld, microphone %ld\n",
		atomic_load(&receiver_peak), atomic_load(&microphone_peak));

	expect(after.is_running, "the audio thread is still running");
	expect(after.is_realtime, "the audio thread has real-time priority");
	expect(labs(went_through - expected) <= BLOCKS_TOLERANCE,
		"the card delivered the blocks its clock promises");
	printf("          queued to play after a write: %ld frames; a block is %d\n",
		after.queued_frames, AUDIO_BLOCK_FRAMES);
	expect(after.capture_overruns == 0, "the card never overflowed");
	expect(after.playback_gaps == 0, "the card never ran dry");
	expect(after.blocks_dropped == 0, "no block was dropped");
	expect(after.queued_frames <= MOST_QUEUED,
		"no more than two blocks are queued to play");
	expect(after.longest_wait_us < LONGEST_WAIT_OK_US,
		"no block was more than half a block late");
	expect(atomic_load(&receiver_peak) > 0, "the receiver's input is alive");
	expect(atomic_load(&microphone_peak) > 0, "the microphone's input is alive");
	expect(atomic_load(&transmitter_peak) == 0,
		"nothing was sent to the transmitter");
}

static void check_a_late_block(void) {
	struct audio_report before;
	struct audio_report after;

	audio_get_report(&before);
	atomic_store(&hold_one_back, 1);
	sleep(SECONDS_AFTER);
	audio_get_report(&after);

	printf("          one block held back %d us: %lu gaps, %lu blocks dropped, "
		"%ld frames queued afterwards\n", HELD_BACK_US,
		after.playback_gaps - before.playback_gaps,
		after.blocks_dropped - before.blocks_dropped, after.queued_frames);
	expect(after.is_running, "the card carried on through a late block");
	expect(after.playback_gaps > before.playback_gaps,
		"the gap was noticed and counted");
	expect(after.blocks_dropped > before.blocks_dropped,
		"the blocks that piled up were dropped");
	expect(after.capture_overruns == before.capture_overruns,
		"the card did not overflow");
	expect(after.queued_frames <= MOST_QUEUED,
		"what is queued to play came back to two blocks or fewer");
}

static void put_back(const struct audio_levels *levels) {
	audio_set_speaker(levels->speaker);
	audio_set_transmitter(levels->transmitter);
	audio_set_receiver(levels->receiver);
	audio_set_microphone(levels->microphone);
}

int main(int argc, char **argv) {
	struct audio_levels at_open;
	int with_tone;
	int seconds;
	int next;

	next = 1;
	with_tone = 0;
	if (argc > next && strcmp(argv[next], "tone") == 0) {
		with_tone = 1;
		next++;
	}
	seconds = DEFAULT_SECONDS;
	if (argc > next)
		seconds = atoi(argv[next]);

	if (audio_open(measure_and_play) < 0) {
		printf("cannot open the sound card\n");
		return 1;
	}
	audio_get_levels(&at_open);

	check_levels();
	time_a_level_write();
	if (with_tone) {
		audio_set_speaker(TONE_SPEAKER_LEVEL);
		atomic_store(&tone_frames_left, (long)TONE_SECONDS * AUDIO_RATE_HZ);
	}
	check_stream(seconds);
	check_a_late_block();

	put_back(&at_open);
	audio_close();

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return 1;
	}
	printf("sound card: all checks passed\n");
	return 0;
}
