// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_rade_codec.c — speech to RADE's signal and back, versions 1 and 2,
// measured as FreeDV measures it.
//
// It needs no radio. FreeDV's own test speech, tests/rade_speech.wav (the
// wav/all.wav of rade_c, fifty seconds of several voices), is put through
// dsp/rade_codec.c a frame at a time, as the radio will put a
// microphone's through it, and what the receiver makes of the signal is
// compared with what went in; once with a codec of each version.
//
// THE MEASURE
//
// FreeDV's "loss", from loss.py of github.com/drowe67/radae, written out
// here in C: the features the receiver gave against those the transmitter
// was given, the first twenty of each frame's thirty-six, eighteen of the
// spectrum, the pitch and how voiced the frame is, weighted as RADE's
// training weighted them. The receiver's first hundred frames and last
// three hundred are left out, and the two runs are slid along each other
// to where they fit best, the receiver not saying how late it is.
// rade_c's README gives 0.080 for this speech with version 2 and says an
// integration passes within a tenth of that. The radio's build gave 0.082
// on the laptop and on a Pi 4 when it was first carried in, and that is
// what is asked for here, within a tenth. For version 1, 0.113, which the
// codec gave on the zbitx's Pi Zero 2 W and the laptop alike on 6 October
// 2026, when the two versions were weighed.
//
// It also asks that the receiver finds the signal, that the frame that
// ends an over is reported, that speech comes out at a level like that
// which went in, that a second over is found as the first was, and that
// both ways are done in less time than the speech lasts.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rade_codec.h"

#define SPEECH_FILE "tests/rade_speech.wav"
#define LOSS_EXPECTED_V1 0.113
#define LOSS_EXPECTED_V2 0.082
#define LOSS_WITHIN 0.1
#define COMPARED 20
#define SPECTRUM 18
#define PAD_FRAMES 100
#define CLIP_START 100
#define CLIP_END 300
#define SILENCE_AFTER_S 1
#define SECOND_OVER_FRAMES 300
#define WAV_HEADER_MOST 4096

static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static double seconds_of_processor(void) {
	return (double)clock() / CLOCKS_PER_SEC;
}

// the samples of a 16-bit mono WAV file, and how many
static int16_t *read_speech(const char *path, long *count) {
	unsigned char head[WAV_HEADER_MOST];
	int16_t *samples;
	FILE *file = fopen(path, "rb");
	long got;
	long at;
	long bytes = 0;

	if (file == NULL)
		return NULL;
	got = (long)fread(head, 1, sizeof(head), file);
	for (at = 12; at + 8 <= got; at += 8 + bytes) {
		bytes = head[at + 4] | head[at + 5] << 8 | head[at + 6] << 16 | (long)head[at + 7] << 24;
		if (!memcmp(head + at, "data", 4))
			break;
	}
	if (at + 8 > got) {
		fclose(file);
		return NULL;
	}
	samples = malloc(bytes);
	fseek(file, at + 8, SEEK_SET);
	*count = (long)fread(samples, sizeof(int16_t), bytes / sizeof(int16_t), file);
	fclose(file);
	return samples;
}

// FreeDV's distortion between as many frames of the two as there are of
// the received
static double distortion(const float *sent, const float *received, long frames) {
	double sum = 0;
	double pitch;
	double voiced;
	double weight;
	double gap;
	long f;
	int i;

	for (f = 0; f < frames; f++) {
		const float *s = sent + f * COMPARED;
		const float *r = received + f * RADE_CODEC_FEATURES;

		for (i = 0; i < SPECTRUM; i++) {
			gap = r[i] - s[i];
			sum += gap * gap;
		}
		pitch = 2 * (r[18] - s[18]);
		voiced = r[19] - s[19];
		weight = s[19] + 0.5 > 0 ? (s[19] + 0.5) * (s[19] + 0.5) : 0;
		sum += SPECTRUM * (3.0 * (10.0 / 18.0) * fabs(pitch) * weight + (1.0 / 18.0) * voiced * voiced);
	}
	return sum / (frames * SPECTRUM);
}

// the least distortion over every place the received could lie along the
// sent, the sent having silence put before and after it
static double loss_of(const float *features_in, long frames_in, const float *features_out,
		long frames_out, long *late) {
	long padded = frames_in + 2 * PAD_FRAMES;
	float *sent = calloc(padded * COMPARED, sizeof(float));
	long compared = frames_out - CLIP_START - CLIP_END;
	double least = 1e9;
	double d;
	long f;
	long shift;

	for (f = 0; f < frames_in; f++)
		memcpy(sent + (f + PAD_FRAMES) * COMPARED, features_in + f * RADE_CODEC_FEATURES,
			COMPARED * sizeof(float));
	for (shift = 0; shift + compared < padded; shift++) {
		d = distortion(sent + shift * COMPARED, features_out + CLIP_START * RADE_CODEC_FEATURES,
			compared);
		if (d < least) {
			least = d;
			*late = shift;
		}
	}
	free(sent);
	return least;
}

static double rms_of(const int16_t *samples, long count) {
	double sum = 0;
	long i;

	for (i = 0; i < count; i++)
		sum += (double)samples[i] * samples[i];
	return sqrt(sum / (count > 0 ? count : 1));
}

static float *signal;
static long signal_count;
static long signal_taken;

static void signal_add(const float *modem, int count) {
	memcpy(signal + signal_count, modem, count * sizeof(float));
	signal_count += count;
}

// the receiver is given what there is of the signal that it has not had,
// in the pieces it asks for; what it gives is kept, features and speech,
// and whether it found the signal and was told of the over's end
static long receive(struct rade_codec *c, float *features_out, int16_t *speech_out, long *speech_count, int *was_synced,
		int *ends) {
	float features[RADE_CODEC_FRAMES_MOST * RADE_CODEC_FEATURES];
	long frames_out = 0;
	int is_end;
	int frames;
	int wanted;
	int f;

	*speech_count = 0;
	*was_synced = 0;
	*ends = 0;
	for (wanted = rade_codec_rx_wanted(c); signal_taken + wanted <= signal_count;
			wanted = rade_codec_rx_wanted(c)) {
		frames = rade_codec_rx_demodulate(c, signal + signal_taken, features, &is_end);
		signal_taken += wanted;
		*ends += is_end;
		*was_synced |= rade_codec_rx_is_synced(c);
		for (f = 0; f < frames; f++) {
			memcpy(features_out + frames_out * RADE_CODEC_FEATURES, features + f * RADE_CODEC_FEATURES,
				RADE_CODEC_FEATURES * sizeof(float));
			frames_out++;
			*speech_count += rade_codec_rx_synthesise(c, features + f * RADE_CODEC_FEATURES,
				speech_out + *speech_count);
		}
	}
	return frames_out;
}

// the speech through a codec of the version, there and back, as the radio
// will do it
static void test_version(int version, double loss_expected, const int16_t *speech,
		long speech_count) {
	struct rade_codec *c;
	float modem[RADE_CODEC_MODEM_MOST];
	float *features_in;
	float *features_out;
	int16_t *speech_out;
	long speech_out_count;
	long frames_in;
	long frames_out;
	long late = 0;
	long f;
	long i;
	double loss;
	double peak = 0;
	double power = 0;
	double began;
	double tx_seconds;
	double rx_seconds;
	double speech_seconds;
	int was_synced;
	int ends;
	char said[128];

	printf(" version %d\n", version);
	c = rade_codec_open(version);
	expect(c != NULL, "the codec opens");
	if (c == NULL)
		return;
	signal_count = 0;
	signal_taken = 0;
	frames_in = speech_count / RADE_CODEC_SPEECH_FRAME;
	speech_seconds = (double)speech_count / RADE_CODEC_SPEECH_RATE_HZ;
	features_in = calloc(frames_in * RADE_CODEC_FEATURES, sizeof(float));
	features_out = calloc((frames_in + 2 * PAD_FRAMES) * RADE_CODEC_FEATURES, sizeof(float));
	speech_out = calloc(speech_count + 2 * PAD_FRAMES * RADE_CODEC_SPEECH_FRAME, sizeof(int16_t));
	signal = calloc(speech_count + 4 * RADE_CODEC_MODEM_MOST + SILENCE_AFTER_S * RADE_CODEC_MODEM_RATE_HZ,
		sizeof(float));

	// sending: the speech, a frame at a time
	began = seconds_of_processor();
	rade_codec_tx_begin(c);
	for (f = 0; f < frames_in; f++) {
		rade_codec_tx_analyse(c, speech + f * RADE_CODEC_SPEECH_FRAME, features_in + f * RADE_CODEC_FEATURES);
		signal_add(modem, rade_codec_tx_modulate(c, features_in + f * RADE_CODEC_FEATURES, modem));
	}
	tx_seconds = seconds_of_processor() - began;
	for (i = 0; i < signal_count; i++) {
		power += (double)signal[i] * signal[i];
		if (fabs(signal[i]) > peak)
			peak = fabs(signal[i]);
	}
	power = sqrt(power / signal_count);
	snprintf(said, sizeof(said), "the signal is as long as the speech (%.1f s of %.1f s)",
		(double)signal_count / RADE_CODEC_MODEM_RATE_HZ, speech_seconds);
	expect(labs(signal_count - speech_count / 2) < RADE_CODEC_MODEM_RATE_HZ / 10, said);
	snprintf(said, sizeof(said), "its level is %.2f and its peaks %.2f, %.1f dB over it", power,
		peak, 20 * log10(peak / power));
	expect(power > 0.1 && power < 1.0 && peak < 2.0, said);

	// receiving it, and no end of the over yet: this is what is measured
	began = seconds_of_processor();
	rade_codec_rx_begin(c);
	frames_out = receive(c, features_out, speech_out, &speech_out_count, &was_synced, &ends);
	rx_seconds = seconds_of_processor() - began;
	expect(was_synced, "the receiver finds the signal");
	expect(frames_out > frames_in - PAD_FRAMES && frames_out <= frames_in,
		"and gives nearly as many frames as were sent");
	loss = frames_out > CLIP_START + CLIP_END
		? loss_of(features_in, frames_in, features_out, frames_out, &late) : 1e9;
	snprintf(said, sizeof(said), "FreeDV's loss is %.3f, within a tenth of %.3f (it comes %.2f s late)",
		loss, loss_expected, (late - PAD_FRAMES) * 0.01);
	expect(fabs(loss - loss_expected) <= LOSS_WITHIN * loss_expected, said);
	snprintf(said, sizeof(said), "speech comes out, at %.0f where %.0f went in",
		rms_of(speech_out, speech_out_count), rms_of(speech, speech_count));
	expect(speech_out_count > speech_count * 9 / 10
		&& rms_of(speech_out, speech_out_count) > rms_of(speech, speech_count) / 3
		&& rms_of(speech_out, speech_out_count) < rms_of(speech, speech_count) * 3, said);
	expect(ends == 0, "no end of an over is reported while it goes on");
	snprintf(said, sizeof(said), "sending took %.0f%% of one processor, receiving %.0f%%",
		100 * tx_seconds / speech_seconds, 100 * rx_seconds / speech_seconds);
	expect(tx_seconds < speech_seconds && rx_seconds < speech_seconds, said);

	// the frame that ends the over, and quiet after it
	signal_add(modem, rade_codec_tx_end(c, modem));
	signal_count += SILENCE_AFTER_S * RADE_CODEC_MODEM_RATE_HZ;
	receive(c, features_out, speech_out, &speech_out_count, &was_synced, &ends);
	expect(ends == 1, "the end of the over is reported, once");
	expect(!rade_codec_rx_is_synced(c), "and the receiver lets go of the signal");

	// a second over, begun afresh at both ends
	memset(signal, 0, signal_count * sizeof(float));
	signal_count = 0;
	signal_taken = 0;
	rade_codec_tx_begin(c);
	for (f = 0; f < SECOND_OVER_FRAMES; f++)
		signal_add(modem, rade_codec_tx_modulate(c, features_in + f * RADE_CODEC_FEATURES, modem));
	signal_add(modem, rade_codec_tx_end(c, modem));
	signal_count += SILENCE_AFTER_S * RADE_CODEC_MODEM_RATE_HZ;
	rade_codec_rx_begin(c);
	frames_out = receive(c, features_out, speech_out, &speech_out_count, &was_synced, &ends);
	expect(was_synced && frames_out > SECOND_OVER_FRAMES / 2 && ends == 1,
		"a second over is found and its end reported");

	rade_codec_close(c);
	free(features_in);
	free(features_out);
	free(speech_out);
	free(signal);
}

int main(void) {
	int16_t *speech;
	long speech_count = 0;

	printf("test_rade_codec\n");
	speech = read_speech(SPEECH_FILE, &speech_count);
	expect(speech != NULL && speech_count > RADE_CODEC_SPEECH_RATE_HZ, "the test speech is read");
	if (speech == NULL)
		return EXIT_FAILURE;
	test_version(RADE_CODEC_V1, LOSS_EXPECTED_V1, speech, speech_count);
	test_version(RADE_CODEC_V2, LOSS_EXPECTED_V2, speech, speech_count);
	if (checks_failed) {
		printf("test_rade_codec: %d FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("test_rade_codec: all passed\n");
	return EXIT_SUCCESS;
}
