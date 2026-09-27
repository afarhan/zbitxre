// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// sdr.c — the radio itself: tuning, modes, filters and the receiver.
//
// This is the receiver of zbitxv2's sbitx.c with its arithmetic unchanged.
// Thanks to Phil Karn, KA9Q, whose method of filtering by fast convolution
// it follows.
//
// HOW THE RADIO HEARS
//
// The board mixes the band down twice, and what reaches the sound card is a
// slice of the band about 48 kHz wide. The second oscillator, the BFO, is
// fixed. The first is set so that the wanted frequency lands on the board's
// centre bin: on the zbitx that is bin 600, 28.125 kHz. The centre bin is
// the board's to choose, and hw/board.c says why the zbitx chose that one.
//
// The sound card delivers a block of 1024 samples. The receiver then:
//
//   1. Puts the block after the block before it, 2048 samples in all.
//   2. Turns those into 2048 bins, each 46.875 Hz wide.
//   3. Turns the bins round by the centre bin, so that the wanted signal's
//      carrier is at bin zero. AM is left where it is.
//   4. Clears the half of the bins that holds the other sideband.
//   5. Multiplies every bin by the filter's shape.
//   6. Turns the bins back into 2048 samples and keeps the last 1024. The
//      first 1024 are spoiled by the filter's impulse and are thrown away,
//      which is why each block is transformed twice, once as the new half
//      and once as the old.
//   7. Applies the AGC.
//   8. Sends the imaginary part to the speaker, or for AM the magnitude.
//
// For the spectrum, the same 2048 samples are tapered with a Hann window and
// transformed a second time, because bins made from an untapered block smear
// into each other. That has no part in what is heard.
//
// CW is received as a single sideband signal. The first oscillator is set
// off by the pitch, so that a station on the dial's frequency is heard as a
// tone of that pitch.
//
// THE AGC
//
// Once a block, the AGC finds the loudest sample and works out the gain that
// would bring it to full level. If that is less than the gain in use, the
// gain drops at once and a hang timer starts. When the timer has run out the
// gain climbs back, a little each block. The speed of the AGC is the length
// of the hang, in blocks.
//
// WHO CHANGES WHAT
//
// The audio thread runs sdr_block(). Everything else here is for the thread
// that owns the controls. The two never wait for each other.
//
// What the controls decide, the mode, the AGC's speed, the filter's shape,
// is kept in three copies. The control thread fills one and offers it. At
// the start of a block the audio thread takes the newest offered, and works
// from it for the whole block. A copy is never written while it is read.
//
// What the audio thread measures, the signal's strength and the spectrum,
// goes the other way. The strength is written between two ticks of a
// counter, and a reader that sees the counter move tries again.
//
// sdr_open_without_radio() sets up the receiver and touches no hardware. The
// caller then feeds sdr_block() itself. It is for tests.
//
// ASKING BY NAME
//
// sdr_request() takes "key=value", as zbitxv2's did, with the same keys. It
// answers "ok", or a number, or "error". A key it does not know is an error;
// zbitxv2 let those pass in silence. The two edges of the passband may be
// sent in either order: the filter is reshaped when the pair makes sense.

#include <complex.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fftw3.h>

#include "audio.h"
#include "board.h"
#include "filter.h"
#include "i2c.h"
#include "si5351.h"
#include "sdr.h"

#define SAMPLE_RATE          96000.0
#define AM_CARRIER_HZ        24000

#define BFO_CLOCK            1
#define OSCILLATOR_CLOCK     2
#define OSCILLATOR_SETTLE_US 200000

#define INPUT_HALVED         2
#define RECEIVE_SCALE        20000000.0

#define SPECTRUM_FIRST       1269
#define SPECTRUM_END         1803
#define SPECTRUM_NEW         0.3
#define SPECTRUM_OFFSET      (SDR_BINS / 4)

#define AGC_PEAK_SCALE       1000
#define AGC_FULL_LEVEL       100000000000.0
#define AGC_GAIN_WHEN_OFF    10000000
#define AGC_AVERAGE_OLD      0.93
#define AGC_AVERAGE_NEW      0.07

#define MUTED_AT_START       50

#define S_METER_TRIM         200.0
#define S_METER_REFERENCE    1e-4
#define DB_PER_S_UNIT        6.0
#define S9                   9
#define MOST_DB_OVER_S9      20
#define S_UNIT_WEIGHT        100

#define DEFAULT_MODE         SDR_LSB
#define DEFAULT_LOW_HZ       300
#define DEFAULT_HIGH_HZ      3000
#define DEFAULT_PITCH_HZ     700
#define DEFAULT_GAIN         100

#define COPIES               3
#define WHICH_COPY           3u
#define IS_NEW               4u

struct settings {
	int mode;
	int agc_speed;
	int tuned_bin;
	float complex receive_shape[FILTER_BINS];
};

static const struct board *board;
static int has_radio;
static int is_open;

static struct settings wanted;
static struct settings copy[COPIES];
static unsigned filling;
static unsigned in_use;
static atomic_uint offered;

static int low_hz;
static int high_hz;
static int pitch_hz;
static int receiver_gain;
static uint32_t dial_hz;
static uint32_t oscillator_hz;
static int is_tuned;

static fftw_complex *samples;
static fftw_complex *bins;
static fftw_complex *previous;
static fftw_complex *spectrum_bins;
static fftw_complex *receive_bins;
static fftw_complex *receive_samples;
static fftw_plan to_bins;
static fftw_plan to_spectrum;
static fftw_plan to_samples;

static float hann[SDR_BINS];
static float smoothed[SDR_BINS];
static int plot[SDR_BINS];

static double agc_gain;
static int agc_hang;
static int muted_blocks;

static atomic_uint strength_tick;
static double strength;

static void offer(void) {
	copy[filling] = wanted;
	filling = atomic_exchange(&offered, filling | IS_NEW) & WHICH_COPY;
}

static const struct settings *newest(void) {
	if (atomic_load(&offered) & IS_NEW)
		in_use = atomic_exchange(&offered, in_use) & WHICH_COPY;
	return &copy[in_use];
}

static int is_lower_sideband(int mode) {
	return mode == SDR_LSB || mode == SDR_CWR;
}

static int mode_ok(int mode) {
	if (mode == SDR_USB || mode == SDR_LSB)
		return 1;
	if (mode == SDR_CW || mode == SDR_CWR)
		return 1;
	return mode == SDR_AM;
}

static int shape_the_receive_filter(void) {
	float low;
	float high;

	if (wanted.mode == SDR_AM) {
		low = (1.0 * (AM_CARRIER_HZ - high_hz)) / SAMPLE_RATE;
		high = (1.0 * (AM_CARRIER_HZ + high_hz)) / SAMPLE_RATE;
	} else if (is_lower_sideband(wanted.mode)) {
		low = (1.0 * -high_hz) / SAMPLE_RATE;
		high = (1.0 * -low_hz) / SAMPLE_RATE;
	} else {
		low = (1.0 * low_hz) / SAMPLE_RATE;
		high = (1.0 * high_hz) / SAMPLE_RATE;
	}
	return filter_shape(wanted.receive_shape, low, high, FILTER_BETA);
}

static int tune_the_oscillator(void) {
	int bin_offset_hz;
	uint32_t hz;

	bin_offset_hz = (int)(SDR_HZ_PER_BIN * (1.0 * wanted.tuned_bin));
	hz = dial_hz + board->bfo_hz - bin_offset_hz;
	if (wanted.mode == SDR_CW)
		hz -= pitch_hz;
	if (wanted.mode == SDR_CWR)
		hz += pitch_hz;

	oscillator_hz = hz;
	if (!has_radio)
		return 0;
	return si5351_set_freq(OSCILLATOR_CLOCK, hz);
}

int sdr_set_frequency(uint32_t hz) {
	if (!is_open)
		return -1;
	if (is_tuned && hz == dial_hz)
		return 0;
	dial_hz = hz;
	if (tune_the_oscillator() < 0)
		return -1;
	is_tuned = 1;
	return 0;
}

int sdr_set_mode(int mode) {
	if (!is_open || !mode_ok(mode))
		return -1;
	wanted.mode = mode;
	if (shape_the_receive_filter() < 0)
		return -1;
	offer();
	if (!is_tuned)
		return 0;
	return tune_the_oscillator();
}

int sdr_set_passband(int low, int high) {
	if (!is_open || low < 0 || high <= low)
		return -1;
	low_hz = low;
	high_hz = high;
	if (shape_the_receive_filter() < 0)
		return -1;
	offer();
	return 0;
}

int sdr_set_agc(int speed) {
	if (!is_open)
		return -1;
	if (speed != SDR_AGC_OFF && speed <= 0)
		return -1;
	wanted.agc_speed = speed;
	offer();
	return 0;
}

int sdr_set_pitch(int hz) {
	if (!is_open || hz <= 0)
		return -1;
	pitch_hz = hz;
	if (!is_tuned)
		return 0;
	return tune_the_oscillator();
}

int sdr_set_volume(int percent) {
	if (!is_open)
		return -1;
	if (!has_radio)
		return 0;
	return audio_set_speaker(percent);
}

int sdr_set_gain(int percent) {
	if (!is_open || percent < 0 || percent > AUDIO_LEVEL_MAX)
		return -1;
	receiver_gain = percent;
	if (!has_radio)
		return 0;
	return audio_set_receiver(percent);
}

uint32_t sdr_oscillator_hz(void) {
	return oscillator_hz;
}

static void take_in(const int32_t *from_receiver) {
	double sample;
	int i;

	for (i = 0; i < SDR_BLOCK; i++)
		samples[i] = previous[i];
	for (i = 0; i < SDR_BLOCK; i++) {
		sample = (1.0 * (from_receiver[i] / INPUT_HALVED)) / RECEIVE_SCALE;
		previous[i] = sample;
		samples[SDR_BLOCK + i] = sample;
	}
}

static void update_the_spectrum(const struct settings *now) {
	int offset;
	int i;

	for (i = 0; i < SDR_BINS; i++)
		samples[i] = CMPLX(creal(samples[i]) * hann[i], cimag(samples[i]));
	fftw_execute(to_spectrum);

	offset = now->tuned_bin - SPECTRUM_OFFSET;
	for (i = SPECTRUM_FIRST; i < SPECTRUM_END; i++) {
		smoothed[i] = ((1.0 - SPECTRUM_NEW) * smoothed[i])
			+ (SPECTRUM_NEW * cabs(spectrum_bins[i - offset]));
		plot[i] = 10 * log10f(smoothed[i] * smoothed[i]);
	}
}

static void bring_to_baseband(const struct settings *now) {
	int turn;
	int from;
	int i;

	turn = now->tuned_bin;
	if (now->mode == SDR_AM)
		turn = 0;
	for (i = 0; i < SDR_BINS; i++) {
		from = i + turn;
		if (from >= SDR_BINS)
			from = from - SDR_BINS;
		if (from < 0)
			from = from + SDR_BINS;
		receive_bins[i] = bins[from];
	}
}

static void clear_the_other_sideband(const struct settings *now) {
	int i;

	if (now->mode == SDR_AM)
		return;
	if (is_lower_sideband(now->mode)) {
		for (i = 0; i < SDR_BLOCK; i++)
			receive_bins[i] = 0;
		return;
	}
	for (i = SDR_BLOCK; i < SDR_BINS; i++)
		receive_bins[i] = 0;
}

static void scale_the_kept_half(double gain) {
	fftw_complex *kept;
	int i;

	kept = receive_samples + SDR_BLOCK;
	for (i = 0; i < SDR_BLOCK; i++)
		kept[i] = CMPLX(creal(kept[i]), cimag(kept[i]) * gain);
}

static void apply_the_agc(const struct settings *now) {
	double loudest;
	double sample;
	double gain_should_be;
	double climb;
	int i;

	if (now->agc_speed == SDR_AGC_OFF) {
		scale_the_kept_half(AGC_GAIN_WHEN_OFF);
		return;
	}

	loudest = 0.0;
	for (i = 0; i < SDR_BLOCK; i++) {
		sample = cimag(receive_samples[SDR_BLOCK + i]) * AGC_PEAK_SCALE;
		if (loudest < sample)
			loudest = sample;
	}
	if (loudest == 0)
		gain_should_be = AGC_GAIN_WHEN_OFF;
	else
		gain_should_be = AGC_FULL_LEVEL / loudest;

	climb = 0.0;
	if (gain_should_be < agc_gain) {
		agc_gain = gain_should_be;
		agc_hang = now->agc_speed;
	} else if (agc_hang <= 0) {
		climb = (gain_should_be - agc_gain) / SDR_BLOCK;
	}

	scale_the_kept_half(agc_gain);
	if (climb != 0)
		agc_gain += climb;
	agc_hang--;
}

static void measure_the_strength(void) {
	double sum;
	int i;

	sum = 0.0;
	for (i = 0; i < SDR_BLOCK; i++)
		sum += cabs(receive_samples[i]);

	atomic_fetch_add(&strength_tick, 1);
	strength = sum / SDR_BLOCK;
	atomic_fetch_add(&strength_tick, 1);
}

static void give_out(const struct settings *now, struct audio_block *block) {
	fftw_complex *kept;
	int32_t sample;
	int i;

	kept = receive_samples + SDR_BLOCK;
	for (i = 0; i < SDR_BLOCK; i++) {
		if (now->mode == SDR_AM)
			sample = cabs(kept[i]);
		else
			sample = cimag(kept[i]);
		block->to_speaker[i] = sample;
		block->to_transmitter[i] = 0;
	}
	if (muted_blocks > 0) {
		memset(block->to_speaker, 0, SDR_BLOCK * sizeof(int32_t));
		muted_blocks--;
	}
}

void sdr_block(struct audio_block *block) {
	const struct settings *now;
	int i;

	now = newest();
	take_in(block->from_receiver);
	fftw_execute(to_bins);
	update_the_spectrum(now);

	bring_to_baseband(now);
	clear_the_other_sideband(now);
	for (i = 0; i < SDR_BINS; i++)
		receive_bins[i] *= now->receive_shape[i];
	fftw_execute(to_samples);

	measure_the_strength();
	apply_the_agc(now);
	give_out(now, block);
}

static double strength_now(void) {
	unsigned before;
	unsigned after;
	double value;

	do {
		before = atomic_load(&strength_tick);
		value = strength;
		after = atomic_load(&strength_tick);
	} while (before != after || (before & 1u));
	return value;
}

int sdr_s_meter(void) {
	double signal;
	double from_gain;
	double power;
	double db;
	int s_units;
	int db_over;

	signal = strength_now();
	from_gain = log10((receiver_gain * 1.0) / 100.0 + 1.0);
	power = signal * signal * S_METER_REFERENCE;
	db = 10 * log10(power / S_METER_REFERENCE);
	db += from_gain * S_METER_TRIM;

	s_units = (int)(db / DB_PER_S_UNIT);
	db_over = (int)(db - (s_units * DB_PER_S_UNIT));
	if (s_units < 0)
		s_units = 0;
	if (db_over < 0)
		db_over = 0;
	if (s_units >= S9 && db_over > MOST_DB_OVER_S9)
		db_over = MOST_DB_OVER_S9;
	return (s_units * S_UNIT_WEIGHT) + db_over;
}

int sdr_spectrum(int *out, int first_bin, int count) {
	if (first_bin < 0 || count < 0 || first_bin + count > SDR_BINS)
		return -1;
	memcpy(out, plot + first_bin, count * sizeof(int));
	return 0;
}

static void *zeroed(int count) {
	fftw_complex *made;

	made = fftw_malloc(sizeof(fftw_complex) * count);
	if (made != NULL)
		memset(made, 0, sizeof(fftw_complex) * count);
	return made;
}

static int plan_the_transforms(const struct sdr_setup *setup) {
	if (setup->wisdom != NULL)
		fftw_import_wisdom_from_filename(setup->wisdom);
	if (setup->wisdom_float != NULL)
		fftwf_import_wisdom_from_filename(setup->wisdom_float);

	previous = zeroed(SDR_BLOCK);
	samples = zeroed(SDR_BINS);
	bins = zeroed(SDR_BINS);
	spectrum_bins = zeroed(SDR_BINS);
	receive_bins = zeroed(SDR_BINS);
	receive_samples = zeroed(SDR_BINS);
	if (previous == NULL || samples == NULL || bins == NULL)
		return -1;
	if (spectrum_bins == NULL || receive_bins == NULL)
		return -1;
	if (receive_samples == NULL)
		return -1;

	to_bins = fftw_plan_dft_1d(SDR_BINS, samples, bins, FFTW_FORWARD,
		FFTW_MEASURE);
	to_spectrum = fftw_plan_dft_1d(SDR_BINS, samples, spectrum_bins,
		FFTW_FORWARD, FFTW_MEASURE);
	to_samples = fftw_plan_dft_1d(SDR_BINS, receive_bins, receive_samples,
		FFTW_BACKWARD, FFTW_MEASURE);
	if (to_bins == NULL || to_spectrum == NULL || to_samples == NULL)
		return -1;
	if (filter_open() < 0)
		return -1;

	memset(samples, 0, sizeof(fftw_complex) * SDR_BINS);
	memset(receive_bins, 0, sizeof(fftw_complex) * SDR_BINS);

	if (setup->wisdom != NULL)
		fftw_export_wisdom_to_filename(setup->wisdom);
	if (setup->wisdom_float != NULL)
		fftwf_export_wisdom_to_filename(setup->wisdom_float);
	return 0;
}

static int start_the_receiver(const struct sdr_setup *setup) {
	int i;

	if (setup == NULL || setup->board == NULL)
		return -1;
	board = setup->board;
	if (plan_the_transforms(setup) < 0)
		return -1;
	filter_hann(hann, SDR_BINS);

	memset(smoothed, 0, sizeof(smoothed));
	memset(plot, 0, sizeof(plot));
	agc_gain = 0.0;
	agc_hang = 0;
	muted_blocks = MUTED_AT_START;
	is_tuned = 0;

	low_hz = DEFAULT_LOW_HZ;
	high_hz = DEFAULT_HIGH_HZ;
	pitch_hz = DEFAULT_PITCH_HZ;
	receiver_gain = DEFAULT_GAIN;

	wanted.mode = DEFAULT_MODE;
	wanted.agc_speed = SDR_AGC_SLOW;
	wanted.tuned_bin = board->centre_bin;
	if (shape_the_receive_filter() < 0)
		return -1;
	for (i = 0; i < COPIES; i++)
		copy[i] = wanted;
	filling = 0;
	in_use = 1;
	atomic_store(&offered, 2u);
	return 0;
}

static int start_the_oscillators(void) {
	usleep(OSCILLATOR_SETTLE_US);
	if (si5351_init() < 0)
		return -1;
	usleep(OSCILLATOR_SETTLE_US);
	if (si5351_set_freq(BFO_CLOCK, board->bfo_hz) < 0)
		return -1;
	usleep(OSCILLATOR_SETTLE_US);
	if (si5351_set_freq(BFO_CLOCK, board->bfo_hz) < 0)
		return -1;
	return si5351_reset_plls();
}

int sdr_open_without_radio(const struct sdr_setup *setup) {
	if (is_open)
		return -1;
	has_radio = 0;
	if (start_the_receiver(setup) < 0)
		return -1;
	is_open = 1;
	return 0;
}

int sdr_open(const struct sdr_setup *setup) {
	if (is_open)
		return -1;
	if (setup == NULL || board_open(setup->board) < 0)
		return -1;
	if (i2c_open_bus_of(SI5351_ADDRESS) < 0)
		return -1;
	has_radio = 1;
	if (start_the_receiver(setup) < 0)
		return -1;
	if (start_the_oscillators() < 0)
		return -1;
	if (audio_open(sdr_block) < 0)
		return -1;
	is_open = 1;
	return 0;
}

void sdr_close(void) {
	if (!is_open)
		return;
	is_open = 0;
	if (!has_radio)
		return;
	audio_close();
	si5351_clock_off(OSCILLATOR_CLOCK);
	si5351_clock_off(BFO_CLOCK);
	i2c_close();
	board_close();
}

struct named {
	const char *name;
	int value;
};

struct request {
	const char *key;
	int (*handle)(const char *value, char *response, int size);
};

static const struct named modes[] = {
	{ "USB", SDR_USB },
	{ "LSB", SDR_LSB },
	{ "CW", SDR_CW },
	{ "CWR", SDR_CWR },
	{ "AM", SDR_AM },
	{ NULL, 0 },
};

static const struct named agc_speeds[] = {
	{ "OFF", SDR_AGC_OFF },
	{ "SLOW", SDR_AGC_SLOW },
	{ "MED", SDR_AGC_MEDIUM },
	{ "FAST", SDR_AGC_FAST },
	{ NULL, 0 },
};

static const struct named *find(const struct named *list, const char *name) {
	int i;

	for (i = 0; list[i].name != NULL; i++) {
		if (strcmp(list[i].name, name) == 0)
			return &list[i];
	}
	return NULL;
}

static int answer(int result, char *response, int size) {
	if (result < 0) {
		snprintf(response, size, "error");
		return -1;
	}
	snprintf(response, size, "ok");
	return 0;
}

static int set_an_edge(int *edge, const char *value) {
	*edge = atoi(value);
	if (low_hz < 0 || high_hz <= low_hz)
		return 0;
	if (shape_the_receive_filter() < 0)
		return -1;
	offer();
	return 0;
}

static int on_freq(const char *value, char *response, int size) {
	return answer(sdr_set_frequency(strtoul(value, NULL, 10)), response, size);
}

static int on_mode(const char *value, char *response, int size) {
	const struct named *mode;

	mode = find(modes, value);
	if (mode == NULL)
		return answer(-1, response, size);
	return answer(sdr_set_mode(mode->value), response, size);
}

static int on_low(const char *value, char *response, int size) {
	return answer(set_an_edge(&low_hz, value), response, size);
}

static int on_high(const char *value, char *response, int size) {
	return answer(set_an_edge(&high_hz, value), response, size);
}

static int on_agc(const char *value, char *response, int size) {
	const struct named *speed;

	speed = find(agc_speeds, value);
	if (speed == NULL)
		return answer(-1, response, size);
	return answer(sdr_set_agc(speed->value), response, size);
}

static int on_gain(const char *value, char *response, int size) {
	return answer(sdr_set_gain(atoi(value)), response, size);
}

static int on_volume(const char *value, char *response, int size) {
	return answer(sdr_set_volume(atoi(value)), response, size);
}

static int on_pitch(const char *value, char *response, int size) {
	return answer(sdr_set_pitch(atoi(value)), response, size);
}

static int on_smeter(const char *value, char *response, int size) {
	(void)value;
	snprintf(response, size, "%d", sdr_s_meter());
	return 0;
}

static int on_stat_tx(const char *value, char *response, int size) {
	(void)value;
	snprintf(response, size, "ok off");
	return 0;
}

static const struct request requests[] = {
	{ "r1:freq", on_freq },
	{ "r1:mode", on_mode },
	{ "r1:low", on_low },
	{ "r1:high", on_high },
	{ "r1:agc", on_agc },
	{ "r1:gain", on_gain },
	{ "r1:volume", on_volume },
	{ "rx_pitch", on_pitch },
	{ "smeter", on_smeter },
	{ "stat:tx", on_stat_tx },
	{ NULL, NULL },
};

int sdr_request(const char *request, char *response, int size) {
	const char *equals;
	size_t key_length;
	int i;

	if (!is_open || request == NULL || response == NULL || size <= 0)
		return -1;
	equals = strchr(request, '=');
	if (equals == NULL)
		return answer(-1, response, size);
	key_length = equals - request;

	for (i = 0; requests[i].key != NULL; i++) {
		if (strlen(requests[i].key) != key_length)
			continue;
		if (strncmp(requests[i].key, request, key_length) != 0)
			continue;
		return requests[i].handle(equals + 1, response, size);
	}
	snprintf(response, size, "error unknown");
	return -1;
}
