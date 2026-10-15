// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// rtp_voip.c — the radio's side of VoIP: the owner's speech to the gateway,
// and another's from it to the speaker.
//
// WHAT THE AUTHOR ASKED FOR
//
// The dial is the room. With VOIP online, whoever speaks over the internet
// on the frequency the radio is tuned to, rounded to the kilohertz, is
// heard in the radio's speaker in place of the receiver, in any voice mode,
// while the radio is receiving; half a second after the speech stops, the
// receiver is heard again. The owner speaks by putting the radio in the
// mode VOIP and pressing PTT: the microphone then goes to the internet, and
// the transmitter is not keyed. my.sbitx.net does the repeating;
// accounts/voip.py says how, and the gateway, zbitx_remote, carries the
// speech between the two. This file is the radio program's end of it, and
// knows nothing of the internet: like rtp_audio.c and rtp_microphone.c it
// speaks RTP on the radio itself, 16-bit samples at 48000 a second, 960 to
// a packet, the more significant byte first.
//
// THE OWNER'S SPEECH
//
// While rtp_voip_set_speaking() says so, the microphone's samples, 96000 a
// second, or the speech of the owner working the radio from afar, which
// rtp_microphone.c puts in their place, are brought down to 48000 through a low-pass filter and sent as
// RTP to the gateway at RTP_VOIP_PORT on the loopback address. They are
// taken as RADE's modem takes them, modems/modem_rade.c: the microphone's
// 32 bits over MIC_A_SAMPLE, so that MIC can stay where SSB has it.
//
// ANOTHER'S SPEECH
//
// The gateway hands it to rtp_microphone.c's port, under the label that
// says it is to be heard and whose it is, and that file passes it here,
// rtp_voip_take_heard(). It is put in a ring, and the audio thread puts it
// in the speaker's place, brought up to 96000 a second, while
// rtp_voip_set_hearing() allows: the tools say when, a voice mode and the
// radio receiving. Speech begins when LEAST, a hundred and twenty
// milliseconds, is waiting, as speech to transmit does in rtp_microphone.c,
// to ride over the internet's unevenness; a gap is silence, and the
// receiver comes back HANG_MS after the speech stops. Who is speaking is
// said by rtp_voip_heard_callsign(), for the radio to show.
//
// The speaker's level is the radio's AUDIO, which is the sound card's, and
// the samples are put at RADE's level for its decoded speech,
// SPEAKER_A_SAMPLE, about where the receiver's own audio is.
//
// WHO DOES WHAT
//
// poll(), set_*(), take_heard() and heard_callsign() are the thread's that
// owns the controls; microphone() and speaker() are the audio thread's. Each
// ring has one writer and one reader, each writing its own position only.

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "rtp_audio.h"
#include "rtp_voip.h"

#define RATE_RADIO_HZ 96000
#define STEP (RATE_RADIO_HZ / RTP_AUDIO_RATE_HZ)
#define TAPS 32
#define TAPS_A_PHASE (TAPS / STEP)
#define CUTOFF_HZ 20000.0
#define HAMMING_A 0.54
#define HAMMING_B 0.46
#define RING_SAMPLES 32768
#define LEAST_SAMPLES 5760
#define MOST_SAMPLES 19200
#define HANG_MS 500
#define HANG_FRAMES (RATE_RADIO_HZ / 1000 * HANG_MS)
// who spoke is shown this long after the last of the speech came
#define SHOWN_MS 700
#define MIC_A_SAMPLE 6553.6f
#define SPEAKER_A_SAMPLE 16000.0f
#define INT16_MOST 32767
#define LOOPBACK "127.0.0.1"
#define RTP_VERSION_2 0x80
#define RTP_SOURCE 0x766f6970
#define NOT_OPEN (-1)

static float taps[TAPS];
static float taps_up[STEP][TAPS_A_PHASE];
static int out = NOT_OPEN;
static struct sockaddr_in gateway;
static atomic_int is_speaking;
static atomic_int is_hearing;
static atomic_int packets_sent;

// the owner's speech: the audio thread's filter, the ring, the poll's count
static float down_history[TAPS];
static int down_at;
static int until_kept = STEP;
static int is_down_begun;
static int16_t spoken[RING_SAMPLES];
static atomic_uint spoken_put;
static atomic_uint spoken_taken;
static uint16_t sequence;
static uint32_t timestamp;

// another's speech: the ring, the poll's note of whose and when, and the
// audio thread's state
static int16_t heard[RING_SAMPLES];
static atomic_uint heard_put;
static atomic_uint heard_taken;
static char heard_callsign[RTP_VOIP_CALLSIGN_BYTES + 1];
static int64_t heard_ms;
static float up_history[TAPS_A_PHASE];
static int up_at;
static int phase;
static int is_breaking_in;
static int is_playing;
static int quiet_frames;

static int64_t now_ms(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int16_t clipped(float sample) {
	if (sample > INT16_MOST)
		return INT16_MOST;
	if (sample < -INT16_MOST)
		return -INT16_MOST;
	return (int16_t)lrintf(sample);
}

int rtp_voip_open(void) {
	double centre = (TAPS - 1) / 2.0;
	double wide = 2.0 * CUTOFF_HZ / RATE_RADIO_HZ;
	double sum = 0;
	double phase_sum[STEP];
	double x;
	int i;

	memset(phase_sum, 0, sizeof(phase_sum));
	for (i = 0; i < TAPS; i++) {
		x = i - centre;
		taps[i] = (float)(sin(M_PI * wide * x) / (M_PI * x)
			* (HAMMING_A - HAMMING_B * cos(2.0 * M_PI * i / (TAPS - 1))));
		sum += taps[i];
		phase_sum[i % STEP] += taps[i];
	}
	// each way, a steady level stays the level it was
	for (i = 0; i < TAPS; i++) {
		taps_up[i % STEP][i / STEP] = (float)(taps[i] / phase_sum[i % STEP]);
		taps[i] = (float)(taps[i] / sum);
	}
	out = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	if (out < 0)
		return -1;
	memset(&gateway, 0, sizeof(gateway));
	gateway.sin_family = AF_INET;
	gateway.sin_port = htons(RTP_VOIP_PORT);
	inet_pton(AF_INET, LOOPBACK, &gateway.sin_addr);
	return 0;
}

void rtp_voip_close(void) {
	if (out != NOT_OPEN)
		close(out);
	out = NOT_OPEN;
}

void rtp_voip_set_speaking(int is_on) {
	atomic_store(&is_speaking, is_on);
}

// whether the owner is speaking over VoIP: the audio thread asks, so that
// the owner working the radio from afar, whose speech comes to
// rtp_microphone.c, is heard over VoIP as over the air
int rtp_voip_is_speaking(void) {
	return atomic_load(&is_speaking);
}

void rtp_voip_set_hearing(int is_allowed) {
	atomic_store(&is_hearing, is_allowed);
}

// how many packets of the owner's speech have gone to the gateway since
// this was last asked; for "report"
int rtp_voip_packets_sent(void) {
	return atomic_exchange(&packets_sent, 0);
}

/* the owner's speech */

// the audio thread's: a block from the microphone, 96000 samples a second
void rtp_voip_microphone(const int32_t *microphone, int frames) {
	unsigned int put = atomic_load(&spoken_put);
	float sum;
	int i;
	int tap;
	int at;

	if (!atomic_load(&is_speaking)) {
		is_down_begun = 0;
		return;
	}
	if (!is_down_begun) {
		is_down_begun = 1;
		memset(down_history, 0, sizeof(down_history));
		down_at = 0;
		until_kept = STEP;
	}
	for (i = 0; i < frames; i++) {
		down_history[down_at] = (float)microphone[i] / MIC_A_SAMPLE;
		down_at++;
		if (down_at == TAPS)
			down_at = 0;
		until_kept--;
		if (until_kept > 0)
			continue;
		until_kept = STEP;
		sum = 0;
		at = down_at;
		for (tap = 0; tap < TAPS; tap++) {
			sum += taps[tap] * down_history[at];
			at++;
			if (at == TAPS)
				at = 0;
		}
		// no room: the poll is not sending, and this is let go
		if (put - atomic_load(&spoken_taken) < RING_SAMPLES)
			spoken[put++ % RING_SAMPLES] = clipped(sum);
	}
	atomic_store(&spoken_put, put);
}

// what the microphone has given is sent to the gateway, a packet at a time
static void send_spoken(void) {
	uint8_t packet[RTP_PACKET_BYTES];
	unsigned int taken = atomic_load(&spoken_taken);
	unsigned int put = atomic_load(&spoken_put);
	int16_t sample;
	int i;

	if (!atomic_load(&is_speaking)) {
		atomic_store(&spoken_taken, put);
		return;
	}
	while (put - taken >= RTP_AUDIO_FRAME_SAMPLES) {
		sequence++;
		timestamp += RTP_AUDIO_FRAME_SAMPLES;
		packet[0] = RTP_VERSION_2;
		packet[1] = RTP_AUDIO_PAYLOAD_TYPE;
		packet[2] = (uint8_t)(sequence >> 8);
		packet[3] = (uint8_t)sequence;
		packet[4] = (uint8_t)(timestamp >> 24);
		packet[5] = (uint8_t)(timestamp >> 16);
		packet[6] = (uint8_t)(timestamp >> 8);
		packet[7] = (uint8_t)timestamp;
		packet[8] = (uint8_t)(RTP_SOURCE >> 24);
		packet[9] = (uint8_t)(RTP_SOURCE >> 16);
		packet[10] = (uint8_t)(RTP_SOURCE >> 8);
		packet[11] = (uint8_t)RTP_SOURCE;
		for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++) {
			sample = spoken[(taken + i) % RING_SAMPLES];
			packet[RTP_HEADER_BYTES + 2 * i] = (uint8_t)((uint16_t)sample >> 8);
			packet[RTP_HEADER_BYTES + 2 * i + 1] = (uint8_t)sample;
		}
		taken += RTP_AUDIO_FRAME_SAMPLES;
		if (out != NOT_OPEN && sendto(out, packet, sizeof(packet), MSG_NOSIGNAL,
				(struct sockaddr *)&gateway, sizeof(gateway)) == (ssize_t)sizeof(packet))
			atomic_fetch_add(&packets_sent, 1);
	}
	atomic_store(&spoken_taken, taken);
}

void rtp_voip_poll(void) {
	send_spoken();
}

/* another's speech */

// a packet's 960 samples, the more significant byte first, and the
// callsign of whoever said them, ten bytes with noughts after it
void rtp_voip_take_heard(const uint8_t *callsign, const uint8_t *samples) {
	unsigned int put = atomic_load(&heard_put);
	int i;

	memcpy(heard_callsign, callsign, RTP_VOIP_CALLSIGN_BYTES);
	heard_callsign[RTP_VOIP_CALLSIGN_BYTES] = 0;
	heard_ms = now_ms();
	// no room: the audio thread is not taking, and this is let go
	if (put - atomic_load(&heard_taken) > RING_SAMPLES - RTP_AUDIO_FRAME_SAMPLES)
		return;
	for (i = 0; i < RTP_AUDIO_FRAME_SAMPLES; i++)
		heard[(put + i) % RING_SAMPLES] = (int16_t)(samples[2 * i] << 8 | samples[2 * i + 1]);
	atomic_store(&heard_put, put + RTP_AUDIO_FRAME_SAMPLES);
}

// the callsign of whoever is being heard now, or "" while nobody is
void rtp_voip_heard_callsign(char *callsign, int room) {
	int is_heard = atomic_load(&is_hearing) && heard_callsign[0] != 0
		&& now_ms() - heard_ms < SHOWN_MS;

	snprintf(callsign, room, "%s", is_heard ? heard_callsign : "");
}

// the audio thread's: the block made for the speaker, 96000 samples a
// second, which another's speech takes the place of while it comes and
// for HANG_MS after; is_sending says whether the radio is transmitting
void rtp_voip_speaker(int32_t *speaker, int frames, int is_sending) {
	unsigned int taken = atomic_load(&heard_taken);
	unsigned int waiting = atomic_load(&heard_put) - taken;
	float sum;
	int i;
	int tap;
	int at;

	if (is_sending || !atomic_load(&is_hearing)) {
		atomic_store(&heard_taken, taken + waiting);
		is_breaking_in = 0;
		return;
	}
	if (!is_breaking_in) {
		if (waiting < LEAST_SAMPLES)
			return;
		is_breaking_in = 1;
		is_playing = 0;
		quiet_frames = 0;
		memset(up_history, 0, sizeof(up_history));
		phase = 0;
	}
	if (waiting > MOST_SAMPLES) {
		taken += waiting - LEAST_SAMPLES;
		waiting = LEAST_SAMPLES;
	}
	for (i = 0; i < frames; i++) {
		if (phase == 0) {
			if (!is_playing && waiting >= LEAST_SAMPLES)
				is_playing = 1;
			if (is_playing && waiting == 0)
				is_playing = 0;
			up_at++;
			if (up_at == TAPS_A_PHASE)
				up_at = 0;
			up_history[up_at] = 0;
			if (is_playing) {
				up_history[up_at] = heard[taken % RING_SAMPLES];
				taken++;
				waiting--;
			}
		}
		// taps_up[phase][j] multiplies the sample j back from the newest
		sum = 0;
		at = up_at;
		for (tap = 0; tap < TAPS_A_PHASE; tap++) {
			sum += taps_up[phase][tap] * up_history[at];
			at--;
			if (at < 0)
				at = TAPS_A_PHASE - 1;
		}
		speaker[i] = (int32_t)(sum * SPEAKER_A_SAMPLE);
		phase++;
		if (phase == STEP)
			phase = 0;
		quiet_frames = is_playing ? 0 : quiet_frames + 1;
	}
	atomic_store(&heard_taken, taken);
	if (quiet_frames >= HANG_FRAMES)
		is_breaking_in = 0;
}
