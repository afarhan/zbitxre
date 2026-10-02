// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_ft8.c — FT8. So far it only receives.
//
// FT8 is heard as USB is, 50 to 4000 Hz above the dial, which is the width
// zbitxv2 gave it, and goes to the speaker as USB does. The same sound is
// kept, a slot at a time, and each slot is searched for messages. What is
// found is added to the radio's messages, with message_add(); messages.c
// says who reads them from there.
//
// The names are zbitxv2's: ft8_rx() takes the sound, ft8_thread_function()
// is the thread that decodes it, ft8_init() starts it. The decoding itself
// is in ft8_monitor.c, which says how it is done and whose work it is.
//
// THE SLOT
//
// Every station sends in slots of 15 seconds that begin at 0, 15, 30 and
// 45 seconds past the minute, by the clock, and a message takes 12.64
// seconds of its slot, beginning half a second in. So the radio has to
// know the time to about a second, and it is the radio's clock that says
// it, hw/radio_clock.c: the kernel's when that can be trusted, and what
// the operator set on the panel when it cannot. With the clock not set at
// all, nothing can be expected to be found, and the terminal is told so.
//
// ft8_rx() is called by the audio thread with each block of sound, 1024
// samples at 96000 a second. Every eighth is kept, which makes 12000 a
// second, the rate the decoder works at; the receiver's filter has
// already cut off everything above 4 kHz, so nothing folds back. When the
// clock's second changes, it looks where in the slot that is, as zbitxv2
// did:
//
//   at second 0   the keeping starts again from nothing, so that what is
//                 kept begins where the slot does, to within a block.
//   at second 14  if 13 seconds or more have been kept, the slot is given
//                 to the decoder. Every message in it had ended a second
//                 before. A slot that was joined late, after a change of
//                 mode, is not long enough and is let go.
//
// THE THREAD
//
// Searching a slot takes far longer than the 10 ms a block allows, so it
// is done by a thread of its own, as modem.h asks. The audio thread must
// never wait, so nothing is locked between them. There are two buffers,
// one being filled and one being searched; the audio thread exchanges
// them and wakes the decoder with a semaphore, and it does so only while
// the decoder is idle, which is_decoding says. Should the decoder still
// be at the last slot when the next is ready, that next one is let go.
//
// The clock is read by the decoder's thread, once as it starts and again
// after every slot, since reading it means asking the kernel and opening a
// file, which the audio thread must not do. What it keeps is how far the
// radio's clock is ahead of the kernel's, and the audio thread adds that
// to the kernel's time, which costs it nothing to read.

#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sdr.h"
#include "modem.h"
#include "messages.h"
#include "radio_clock.h"
#include "ft8_monitor.h"

#define FT8_LOW_HZ 50
#define FT8_HIGH_HZ 4000
#define FT8_MAX_BUFF (FT8_SAMPLE_RATE * 18)
#define FT8_FOUND_MAX 50
#define SOUND_CARD_RATE 96000
#define SLOT_SECONDS 15
#define DECODE_AT_SECOND 14
#define ENOUGH_SECONDS 13
// zbitxv2's scaling of the receiver's samples for the decoder
#define FT8_FULL_SCALE 200000000.0f
#define MS_PER_S 1000
#define NS_PER_MS 1000000

static int32_t baseband_output[MAX_BINS/2];

static float buffer_a[FT8_MAX_BUFF];
static float buffer_b[FT8_MAX_BUFF];
static float *ft8_rx_buffer = buffer_a;
static float *ft8_decode_buffer = buffer_b;
static int ft8_rx_buff_index;
static int ft8_decode_count;
static time_t ft8_decode_slot;
static time_t wallclock;

static pthread_t ft8_thread;
static sem_t slot_is_ready;
static atomic_int is_decoding;
static atomic_long clock_ahead_s;

static struct ft8_decoded found[FT8_FOUND_MAX];

// how far the radio's clock is ahead of the kernel's, for the audio thread
static void read_the_clock(void) {
	static int was_told;
	time_t now;

	if (radio_clock_now(&now) == RADIO_CLOCK_NOT_SET) {
		if (!was_told)
			printf("ft8: the radio's clock is not set, and FT8 needs the time: "
				"set it on the panel, SET, Date/Time\n");
		was_told = 1;
		atomic_store(&clock_ahead_s, 0);
		return;
	}
	was_told = 0;
	atomic_store(&clock_ahead_s, (long)(now - time(NULL)));
}

static long elapsed_ms(const struct timespec *since) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - since->tv_sec) * MS_PER_S
		+ (now.tv_nsec - since->tv_nsec) / NS_PER_MS;
}

static void *ft8_thread_function(void *ptr) {
	struct timespec began;
	struct tm utc;
	int count;
	int i;

	(void)ptr;
	while (1) {
		read_the_clock();
		sem_wait(&slot_is_ready);
		clock_gettime(CLOCK_MONOTONIC, &began);
		count = sbitx_ft8_decode(ft8_decode_buffer, ft8_decode_count, found, FT8_FOUND_MAX);
		for (i = 0; i < count; i++)
			message_add("FT8", ft8_decode_slot, found[i].snr, found[i].freq_hz,
				found[i].text);
		gmtime_r(&ft8_decode_slot, &utc);
		printf("ft8: slot %02d:%02d:%02d, found %d, searched in %ld ms\n",
			utc.tm_hour, utc.tm_min, utc.tm_sec, count, elapsed_ms(&began));
		fflush(stdout);
		atomic_store(&is_decoding, 0);
	}
	return NULL;
}

// the slot that has been kept goes to the decoder, if the decoder is idle,
// and the keeping goes on in the other buffer
static void give_the_slot(time_t slot_start) {
	float *kept = ft8_rx_buffer;

	if (atomic_load(&is_decoding))
		return;
	ft8_rx_buffer = ft8_decode_buffer;
	ft8_decode_buffer = kept;
	ft8_decode_count = ft8_rx_buff_index;
	ft8_decode_slot = slot_start;
	atomic_store(&is_decoding, 1);
	sem_post(&slot_is_ready);
}

// the ft8 sampling is at 12000, the incoming samples are at
// 96000 samples/sec
static void ft8_rx(int32_t *samples, int count) {
	int decimation_ratio = SOUND_CARD_RATE / FT8_SAMPLE_RATE;
	int slot_second;
	time_t now;
	int i;

	//if there is an overflow, then reset to the begining
	if (ft8_rx_buff_index + (count / decimation_ratio) >= FT8_MAX_BUFF)
		ft8_rx_buff_index = 0;

	//down convert to 12000 Hz sampling rate
	for (i = 0; i < count; i += decimation_ratio)
		ft8_rx_buffer[ft8_rx_buff_index++] = samples[i] / FT8_FULL_SCALE;

	now = time(NULL) + atomic_load(&clock_ahead_s);
	if (now == wallclock)
		return;
	wallclock = now;

	slot_second = (int)(wallclock % SLOT_SECONDS);
	if (slot_second == 0)
		ft8_rx_buff_index = 0;

	//we should have atleast 13 seconds of samples to decode
	if (ft8_rx_buff_index >= ENOUGH_SECONDS * FT8_SAMPLE_RATE
			&& slot_second == DECODE_AT_SECOND) {
		give_the_slot(wallclock - slot_second);
		ft8_rx_buff_index = 0;
	}
}

static void rx_ft8(complex float *bins) {
	demod_ssb(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
	ft8_rx(baseband_output, MAX_BINS/2);
}

static void ft8_select(void) {
	set_passband(FT8_LOW_HZ, FT8_HIGH_HZ);
}

// a slot that was being kept when the mode was left is not one to decode
static void ft8_reset(void) {
	ft8_rx_buff_index = 0;
}

static void ft8_init(void) {
	sem_init(&slot_is_ready, 0, 0);
	pthread_create(&ft8_thread, NULL, ft8_thread_function, NULL);
}

const struct modem modem_ft8 = {
	.name = "FT8",
	.order = 6,
	.start = ft8_init,
	.select = ft8_select,
	.reset = ft8_reset,
	.rx = rx_ft8,
};
