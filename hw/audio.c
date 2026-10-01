// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// audio.c — the sound card: samples in and out, and the four levels.
//
// Thanks to hermes-radio-daemon (Rafael Diniz, Rhizomatica). This file follows
// its sbitx/sbitx_alsa.c, Copyright (C) 2023-2024 Rhizomatica, in four things:
// a period no larger than a block, a level for each channel, playing that
// starts only when silence and the first real block are both queued, and
// playing that carries on through a late block instead of stopping.
//
// The WM8731 has two inputs and two outputs and the radio uses each for a
// different thing:
//
//     left in      from the receiver
//     right in     from the microphone
//     left out     to the speaker and the earphone
//     right out    to the transmitter
//
// Being separate channels, each has a level of its own, and none of them has
// to change when the radio goes between receive and transmit. A level is
// written when its control moves and at no other time. In receive the radio
// sends silence to the transmitter, and in transmit it ignores the receiver.
//
// A level is 0 to 100. For the transmitter and the two inputs it is spread
// over the chip's whole range of steps, rounding down, as zbitxv2 counts
// levels, so its settings for those carry over unchanged. An input has only
// 31 steps, so several levels share a step; a level read back is the lowest
// that gives the step the chip is on, and writing it back changes nothing.
//
// The speaker's level is counted differently, because spread over the
// whole range most of it is of no use. An output has 127 steps of 1 dB, of
// which the chip plays nothing below step 48, and what the receiver sends
// is well under full scale: the author found the speaker almost silent at
// 85 of zbitxv2's 100, and overdriven at 100. So for the speaker 0 is
// silence, and 1 to 100 are spread evenly over the top SPEAKER_SPAN_STEPS
// steps of the chip, 40 dB, with 100 the chip's highest step as before.
// That is 0.4 dB to a level, so two or three levels share a step. What
// was 85 is now about 52, and what was 95, the chip's 0 dB, about 85. The
// top 6 dB are above the chip's 0 dB and overdrive a little; the author
// keeps them, since a little overdrive is no harm when the volume has to
// be topped off.
//
// One thread does the work. It waits for the card to gather a block, hands
// that to the radio's function together with two empty blocks to fill, and
// queues what comes back. The card's clock paces everything: a block is 1024
// frames at 96000 a second, which is 10.67 ms.
//
// What is heard is late by the block being gathered and by what is queued to
// play, which stays between one block and two.
//
// The thread asks for real-time priority and a processor to itself. The
// priority is under 95, the most the system allows the audio group; a request
// above the limit is refused outright and leaves the thread an ordinary one.
//
// A late block does not stop the card. The card is told never to stop and to
// play silence where nothing was written, so a block that comes late is heard
// as a gap of exactly its lateness. hermes-radio-daemon keeps four periods
// queued ahead; this keeps one block, which is half the delay and half the
// margin.
//
// After a hold-up the delay through the radio must come back to what it was.
// A thread that was held up finds blocks waiting for it, and working through
// them would leave that much more queued to play for good. So it drops every
// block that was waiting and waits for the next one, which arrives on the
// card's own beat. Then, before writing, it moves its place in the card's
// buffer to exactly one block ahead of what is being played, forward if the
// card has run past it and back if too much is queued. That is the state the
// thread starts in. Gaps and dropped blocks are counted.
//
// If the card fails in some other way it is prepared again. If it cannot be
// prepared the thread stops, and the report says so.
//
// A snapshot is for the times the receiver's sound and spectrum go wrong
// and a restart puts them right. When one is asked for, the thread copies
// the next AUDIO_SNAPSHOT_BLOCKS blocks just as the card gave them, both
// channels, before anything is done to them. audio_get_snapshot() gives
// them once they are all there. With them it can be told whether the
// samples were already wrong as they came from the card.
//
// The radio's block function runs on the audio thread. It must not wait for
// anything. The level functions are for one other thread.
//
// Every function that can fail returns 0, or -1.

#define _GNU_SOURCE

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "audio.h"

#define CARD                 "hw:audioinjectorpi"
#define CHANNELS             2
#define SPEAKER_SPAN_STEPS   40
#define CAPTURE_PERIODS      4
#define PLAYBACK_PERIODS     4
#define PRIMED_PERIODS       1
#define SAMPLES_PER_BLOCK    (AUDIO_BLOCK_FRAMES * CHANNELS)

#define PLAYBACK_FRAMES      (AUDIO_BLOCK_FRAMES * PLAYBACK_PERIODS)
#define PRIMED_FRAMES        (AUDIO_BLOCK_FRAMES * PRIMED_PERIODS)

#define AUDIO_PRIORITY       90
#define AUDIO_PROCESSOR      3
#define FAILURES_TO_STOP     50

#define LEFT                 SND_MIXER_SCHN_FRONT_LEFT
#define RIGHT                SND_MIXER_SCHN_FRONT_RIGHT

#define SPEAKER_AT_OPEN      10
#define TRANSMITTER_AT_OPEN  95
#define LINE_IN              0
#define NO_BOOST             0
#define NO_SIDETONE          0

#define US_PER_SECOND        1000000L
#define NS_PER_US            1000L

static snd_pcm_t *capture;
static snd_pcm_t *playback;
static snd_mixer_t *mixer;
static pthread_t thread;
static audio_block_fn block_fn;

static int opened;
static atomic_int running;
static atomic_int is_realtime;
static atomic_ulong blocks;
static atomic_ulong blocks_dropped;
static atomic_ulong capture_overruns;
static atomic_ulong playback_gaps;
static atomic_long longest_wait_us;
static atomic_long queued_frames;

static int fell_behind;

static int32_t snapshot[AUDIO_SNAPSHOT_BLOCKS * SAMPLES_PER_BLOCK];
static atomic_int snapshot_blocks_to_take;
static atomic_int snapshot_is_ready;

static int32_t captured[SAMPLES_PER_BLOCK];
static int32_t to_play[SAMPLES_PER_BLOCK];
static int32_t from_receiver[AUDIO_BLOCK_FRAMES];
static int32_t from_microphone[AUDIO_BLOCK_FRAMES];
static int32_t to_speaker[AUDIO_BLOCK_FRAMES];
static int32_t to_transmitter[AUDIO_BLOCK_FRAMES];

static snd_mixer_elem_t *control(const char *name) {
	snd_mixer_selem_id_t *id;

	snd_mixer_selem_id_alloca(&id);
	snd_mixer_selem_id_set_index(id, 0);
	snd_mixer_selem_id_set_name(id, name);
	return snd_mixer_find_selem(mixer, id);
}

static int set_switch(const char *name, int on) {
	snd_mixer_elem_t *found;

	found = control(name);
	if (found == NULL)
		return -1;
	if (snd_mixer_selem_has_capture_switch(found))
		return snd_mixer_selem_set_capture_switch_all(found, on);
	return snd_mixer_selem_set_playback_switch_all(found, on);
}

static int set_choice(const char *name, unsigned int item) {
	snd_mixer_elem_t *found;

	found = control(name);
	if (found == NULL)
		return -1;
	return snd_mixer_selem_set_enum_item(found, LEFT, item);
}

static int set_joined_volume(const char *name, long value) {
	snd_mixer_elem_t *found;

	found = control(name);
	if (found == NULL)
		return -1;
	return snd_mixer_selem_set_playback_volume_all(found, value);
}

// the chip's step for a speaker level
static long speaker_step(int level, long highest) {
	if (level == 0)
		return 0;
	return highest - (long)(AUDIO_LEVEL_MAX - level) * SPEAKER_SPAN_STEPS
		/ (AUDIO_LEVEL_MAX - 1);
}

// the lowest speaker level that gives a step of the chip
static int speaker_level(long step, long highest) {
	int level;

	for (level = 0; level < AUDIO_LEVEL_MAX; level++) {
		if (speaker_step(level, highest) >= step)
			return level;
	}
	return AUDIO_LEVEL_MAX;
}

static int percent_ok(int percent) {
	return percent >= 0 && percent <= AUDIO_LEVEL_MAX;
}

static int set_output(snd_mixer_selem_channel_id_t channel, int percent) {
	snd_mixer_elem_t *found;
	long lowest;
	long highest;

	if (mixer == NULL || !percent_ok(percent))
		return -1;
	found = control("Master");
	if (found == NULL)
		return -1;
	snd_mixer_selem_get_playback_volume_range(found, &lowest, &highest);
	if (snd_mixer_selem_set_playback_volume(found, channel,
			percent * highest / AUDIO_LEVEL_MAX) < 0)
		return -1;
	return 0;
}

static int set_input(snd_mixer_selem_channel_id_t channel, int percent) {
	snd_mixer_elem_t *found;
	long lowest;
	long highest;

	if (mixer == NULL || !percent_ok(percent))
		return -1;
	found = control("Capture");
	if (found == NULL)
		return -1;
	snd_mixer_selem_get_capture_volume_range(found, &lowest, &highest);
	if (snd_mixer_selem_set_capture_volume(found, channel,
			percent * highest / AUDIO_LEVEL_MAX) < 0)
		return -1;
	return 0;
}

static int as_percent(long value, long highest) {
	return (int)((value * AUDIO_LEVEL_MAX + highest - 1) / highest);
}

int audio_set_speaker(int level) {
	snd_mixer_elem_t *found;
	long lowest;
	long highest;

	if (mixer == NULL || !percent_ok(level))
		return -1;
	found = control("Master");
	if (found == NULL)
		return -1;
	snd_mixer_selem_get_playback_volume_range(found, &lowest, &highest);
	if (snd_mixer_selem_set_playback_volume(found, LEFT,
			speaker_step(level, highest)) < 0)
		return -1;
	return 0;
}

int audio_set_transmitter(int percent) {
	return set_output(RIGHT, percent);
}

int audio_set_receiver(int percent) {
	return set_input(LEFT, percent);
}

int audio_set_microphone(int percent) {
	return set_input(RIGHT, percent);
}

int audio_get_levels(struct audio_levels *levels) {
	snd_mixer_elem_t *output;
	snd_mixer_elem_t *input;
	long lowest;
	long highest;
	long value;

	if (mixer == NULL)
		return -1;
	output = control("Master");
	input = control("Capture");
	if (output == NULL || input == NULL)
		return -1;
	snd_mixer_handle_events(mixer);

	snd_mixer_selem_get_playback_volume_range(output, &lowest, &highest);
	snd_mixer_selem_get_playback_volume(output, LEFT, &value);
	levels->speaker = speaker_level(value, highest);
	snd_mixer_selem_get_playback_volume(output, RIGHT, &value);
	levels->transmitter = as_percent(value, highest);

	snd_mixer_selem_get_capture_volume_range(input, &lowest, &highest);
	snd_mixer_selem_get_capture_volume(input, LEFT, &value);
	levels->receiver = as_percent(value, highest);
	snd_mixer_selem_get_capture_volume(input, RIGHT, &value);
	levels->microphone = as_percent(value, highest);
	return 0;
}

static int open_mixer(void) {
	if (snd_mixer_open(&mixer, 0) < 0)
		return -1;
	if (snd_mixer_attach(mixer, CARD) < 0)
		return -1;
	if (snd_mixer_selem_register(mixer, NULL, NULL) < 0)
		return -1;
	if (snd_mixer_load(mixer) < 0)
		return -1;
	return 0;
}

static int route_the_chip(void) {
	if (set_switch("Line", 1) < 0)
		return -1;
	if (set_switch("Mic", 0) < 0)
		return -1;
	if (set_joined_volume("Mic Boost", NO_BOOST) < 0)
		return -1;
	if (set_switch("Playback Deemphasis", 0) < 0)
		return -1;
	if (set_choice("Input Mux", LINE_IN) < 0)
		return -1;
	if (set_switch("ADC High Pass Filter", 0) < 0)
		return -1;
	if (set_switch("Output Mixer HiFi", 1) < 0)
		return -1;
	if (set_switch("Master Playback ZC", 0) < 0)
		return -1;
	if (set_joined_volume("Sidetone", NO_SIDETONE) < 0)
		return -1;
	if (set_switch("Output Mixer Mic Sidetone", 0) < 0)
		return -1;
	if (set_switch("Output Mixer Line Bypass", 0) < 0)
		return -1;
	if (set_switch("Store DC Offset", 0) < 0)
		return -1;
	if (audio_set_speaker(SPEAKER_AT_OPEN) < 0)
		return -1;
	return audio_set_transmitter(TRANSMITTER_AT_OPEN);
}

static int open_stream(snd_pcm_t **stream, snd_pcm_stream_t direction,
		int periods) {
	snd_pcm_hw_params_t *wanted;
	snd_pcm_t *opened;

	if (snd_pcm_open(&opened, CARD, direction, 0) < 0)
		return -1;
	*stream = opened;
	snd_pcm_hw_params_alloca(&wanted);
	if (snd_pcm_hw_params_any(opened, wanted) < 0)
		return -1;
	if (snd_pcm_hw_params_set_access(opened, wanted,
			SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
		return -1;
	if (snd_pcm_hw_params_set_format(opened, wanted,
			SND_PCM_FORMAT_S32_LE) < 0)
		return -1;
	if (snd_pcm_hw_params_set_rate(opened, wanted, AUDIO_RATE_HZ, 0) < 0)
		return -1;
	if (snd_pcm_hw_params_set_channels(opened, wanted, CHANNELS) < 0)
		return -1;
	if (snd_pcm_hw_params_set_period_size(opened, wanted,
			AUDIO_BLOCK_FRAMES, 0) < 0)
		return -1;
	if (snd_pcm_hw_params_set_buffer_size(opened, wanted,
			AUDIO_BLOCK_FRAMES * periods) < 0)
		return -1;
	if (snd_pcm_hw_params(opened, wanted) < 0)
		return -1;
	return 0;
}

static int play_without_stopping(void) {
	snd_pcm_sw_params_t *wanted;
	snd_pcm_uframes_t never;

	snd_pcm_sw_params_alloca(&wanted);
	if (snd_pcm_sw_params_current(playback, wanted) < 0)
		return -1;
	if (snd_pcm_sw_params_get_boundary(wanted, &never) < 0)
		return -1;
	if (snd_pcm_sw_params_set_start_threshold(playback, wanted,
			PRIMED_FRAMES + AUDIO_BLOCK_FRAMES) < 0)
		return -1;
	if (snd_pcm_sw_params_set_avail_min(playback, wanted,
			AUDIO_BLOCK_FRAMES) < 0)
		return -1;
	if (snd_pcm_sw_params_set_stop_threshold(playback, wanted, never) < 0)
		return -1;
	if (snd_pcm_sw_params_set_silence_threshold(playback, wanted, 0) < 0)
		return -1;
	if (snd_pcm_sw_params_set_silence_size(playback, wanted, never) < 0)
		return -1;
	if (snd_pcm_sw_params(playback, wanted) < 0)
		return -1;
	return 0;
}

static void make_ready(snd_pcm_t *stream) {
	snd_pcm_prepare(stream);
	snd_pcm_drop(stream);
	snd_pcm_prepare(stream);
}

static void prime(void) {
	int period;

	memset(to_play, 0, sizeof(to_play));
	for (period = 0; period < PRIMED_PERIODS; period++)
		snd_pcm_writei(playback, to_play, AUDIO_BLOCK_FRAMES);
}

static void become_realtime(void) {
	struct sched_param priority;
	cpu_set_t processors;

	priority.sched_priority = AUDIO_PRIORITY;
	if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority) == 0)
		atomic_store(&is_realtime, 1);

	CPU_ZERO(&processors);
	CPU_SET(AUDIO_PROCESSOR, &processors);
	pthread_setaffinity_np(pthread_self(), sizeof(processors), &processors);
}

static long us_since(const struct timespec *earlier) {
	struct timespec now;
	long seconds;

	clock_gettime(CLOCK_MONOTONIC, &now);
	seconds = now.tv_sec - earlier->tv_sec;
	return seconds * US_PER_SECOND
		+ (now.tv_nsec - earlier->tv_nsec) / NS_PER_US;
}

static void note_the_wait(struct timespec *last_block) {
	long waited;

	if (atomic_load(&blocks) > 0) {
		waited = us_since(last_block);
		if (waited > atomic_load(&longest_wait_us))
			atomic_store(&longest_wait_us, waited);
	}
	clock_gettime(CLOCK_MONOTONIC, last_block);
}

static int read_a_block(void) {
	snd_pcm_sframes_t got;

	got = snd_pcm_readi(capture, captured, AUDIO_BLOCK_FRAMES);
	if (got == AUDIO_BLOCK_FRAMES)
		return 0;
	atomic_fetch_add(&capture_overruns, 1);
	snd_pcm_prepare(capture);
	return -1;
}

static int wait_for_a_fresh_block(void) {
	atomic_fetch_add(&blocks_dropped, 1);
	while (snd_pcm_avail_update(capture) >= AUDIO_BLOCK_FRAMES) {
		if (read_a_block() < 0)
			return -1;
		atomic_fetch_add(&blocks_dropped, 1);
	}
	fell_behind = 1;
	return read_a_block();
}

static void copy_to_snapshot(void) {
	int to_take = atomic_load(&snapshot_blocks_to_take);

	if (to_take == 0)
		return;
	memcpy(snapshot + (AUDIO_SNAPSHOT_BLOCKS - to_take) * SAMPLES_PER_BLOCK,
		captured, sizeof(captured));
	atomic_store(&snapshot_blocks_to_take, to_take - 1);
	if (to_take == 1)
		atomic_store(&snapshot_is_ready, 1);
}

static int gather(void) {
	int frame;

	if (read_a_block() < 0)
		return -1;
	if (snd_pcm_avail_update(capture) >= AUDIO_BLOCK_FRAMES) {
		if (wait_for_a_fresh_block() < 0)
			return -1;
	}
	copy_to_snapshot();
	for (frame = 0; frame < AUDIO_BLOCK_FRAMES; frame++) {
		from_receiver[frame] = captured[frame * CHANNELS];
		from_microphone[frame] = captured[frame * CHANNELS + 1];
	}
	return 0;
}

static void stay_one_block_ahead(void) {
	snd_pcm_sframes_t room;
	snd_pcm_sframes_t queued;
	int card_ran_dry;

	room = snd_pcm_avail_update(playback);
	if (room < 0)
		return;
	card_ran_dry = room > PLAYBACK_FRAMES;
	if (card_ran_dry)
		atomic_fetch_add(&playback_gaps, 1);
	if (!card_ran_dry && !fell_behind)
		return;
	fell_behind = 0;

	queued = PLAYBACK_FRAMES - room;
	if (queued < PRIMED_FRAMES)
		snd_pcm_forward(playback, PRIMED_FRAMES - queued);
	if (queued > PRIMED_FRAMES)
		snd_pcm_rewind(playback, queued - PRIMED_FRAMES);
}

static void note_what_is_queued(void) {
	snd_pcm_sframes_t queued;

	if (snd_pcm_delay(playback, &queued) == 0 && queued >= 0)
		atomic_store(&queued_frames, queued);
}

static void play(void) {
	snd_pcm_sframes_t put;
	int frame;

	for (frame = 0; frame < AUDIO_BLOCK_FRAMES; frame++) {
		to_play[frame * CHANNELS] = to_speaker[frame];
		to_play[frame * CHANNELS + 1] = to_transmitter[frame];
	}
	stay_one_block_ahead();
	put = snd_pcm_writei(playback, to_play, AUDIO_BLOCK_FRAMES);
	if (put == AUDIO_BLOCK_FRAMES) {
		note_what_is_queued();
		return;
	}
	atomic_fetch_add(&playback_gaps, 1);
	snd_pcm_prepare(playback);
	prime();
}

static void *audio_thread(void *unused) {
	struct audio_block block;
	struct timespec last_block;
	int failures;

	(void)unused;
	become_realtime();

	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = AUDIO_BLOCK_FRAMES;

	failures = 0;
	make_ready(capture);
	make_ready(playback);
	prime();
	while (atomic_load(&running)) {
		if (gather() < 0) {
			failures++;
			if (failures >= FAILURES_TO_STOP)
				atomic_store(&running, 0);
			continue;
		}
		failures = 0;
		note_the_wait(&last_block);
		memset(to_speaker, 0, sizeof(to_speaker));
		memset(to_transmitter, 0, sizeof(to_transmitter));
		block_fn(&block);
		play();
		atomic_fetch_add(&blocks, 1);
	}
	return NULL;
}

static void close_everything(void) {
	if (capture != NULL)
		snd_pcm_close(capture);
	if (playback != NULL)
		snd_pcm_close(playback);
	if (mixer != NULL)
		snd_mixer_close(mixer);
	capture = NULL;
	playback = NULL;
	mixer = NULL;
}

int audio_open(audio_block_fn radio_block) {
	if (radio_block == NULL || opened)
		return -1;
	block_fn = radio_block;

	atomic_store(&blocks, 0);
	atomic_store(&blocks_dropped, 0);
	atomic_store(&capture_overruns, 0);
	atomic_store(&playback_gaps, 0);
	atomic_store(&longest_wait_us, 0);
	atomic_store(&queued_frames, 0);
	atomic_store(&is_realtime, 0);

	if (open_mixer() < 0 || route_the_chip() < 0) {
		close_everything();
		return -1;
	}
	if (open_stream(&capture, SND_PCM_STREAM_CAPTURE, CAPTURE_PERIODS) < 0
			|| open_stream(&playback, SND_PCM_STREAM_PLAYBACK,
				PLAYBACK_PERIODS) < 0
			|| play_without_stopping() < 0) {
		close_everything();
		return -1;
	}

	atomic_store(&running, 1);
	if (pthread_create(&thread, NULL, audio_thread, NULL) != 0) {
		atomic_store(&running, 0);
		close_everything();
		return -1;
	}
	opened = 1;
	return 0;
}

void audio_close(void) {
	if (!opened)
		return;
	atomic_store(&running, 0);
	pthread_join(thread, NULL);
	close_everything();
	opened = 0;
}

void audio_get_report(struct audio_report *report) {
	report->blocks = atomic_load(&blocks);
	report->blocks_dropped = atomic_load(&blocks_dropped);
	report->capture_overruns = atomic_load(&capture_overruns);
	report->playback_gaps = atomic_load(&playback_gaps);
	report->longest_wait_us = atomic_load(&longest_wait_us);
	report->queued_frames = atomic_load(&queued_frames);
	report->is_realtime = atomic_load(&is_realtime);
	report->is_running = atomic_load(&running);
}

void audio_take_snapshot(void) {
	atomic_store(&snapshot_is_ready, 0);
	atomic_store(&snapshot_blocks_to_take, AUDIO_SNAPSHOT_BLOCKS);
}

// the snapshot asked for, once all of it is there, and NULL until then; it
// is given once
const int32_t *audio_get_snapshot(void) {
	if (!atomic_exchange(&snapshot_is_ready, 0))
		return NULL;
	return snapshot;
}
