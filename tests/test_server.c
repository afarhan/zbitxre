// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_server.c — the control protocol, spoken to by clients, with no radio.
//
// It sets up the radio as the radio program does, with the hardware stood in
// for, opens the server on a port of its own, and connects clients to it as a
// program on another computer would. Between each thing a client says and
// looking at what came back, it lets the server work, as the radio program's
// loop does.
//
// The checks: a change told before the server opens closes nothing; a
// client is told every setting when it connects, a modem's among them; a
// change is told to the other clients and not to the one who made it; a
// value already set is told to nobody; a number snapped onto its steps is
// told back to the one who asked; a band chosen moves the frequency and the
// mode, and those are told to everyone; tuning out of a band says so, and a
// band returned to is where it was left; a value the radio will not take is
// an ERROR, to the one who asked alone; ? gets every setting again; the
// schema lists the controls in their groups; a client that goes is
// forgotten and another can come.
//
// And the passband, as zbitxv2 made it: in USB, 300 Hz to 300 + BW_VOICE;
// in CW, BW_CW wide, centred on PITCH, each put to use as soon as it is
// changed, the oscillator moving with the pitch; a mode chosen again finds
// the bandwidth it was left with. The receiver is run for a block after
// each change, as the audio thread would, so that it takes up what it was
// offered. And the sideband: with a tone fed in on each side of the tuned
// frequency and the AGC off, USB hears one side alone and LSB the other,
// whichever mode it was changed from; a filter left on the last mode's side
// hears neither. And the keyer, through the whole modem: a tap of the dot
// paddle shorter than a tenth of a dot is a whole dot of sidetone in the
// speaker, beginning the keyer's 12 ms after the tap; and with a straight
// key the ring, which the key's plug holds closed, keys nothing. And
// transmitting, with the pins and the sound card stood in for, so that
// nothing is keyed: a mode that cannot transmit refuses; 2TONE puts the
// receiver off, selects the band's filter and puts the transmitter on, in
// that order, and sends two tones of like strength on the upper side and
// nothing on the lower; the level follows the drive; going back to receive
// puts the transmitter off before the receiver on; a change of mode ends a
// transmission; and nothing is sent outside the bands. And CW through the
// transmitter: a tap of the dot paddle in receive makes the modem ask for
// the transmitter; the radio is put in transmit a block later, as late as
// the radio program would be; and the transmitter is sent one whole dot,
// the keyer's 12 ms and the modem's two blocks after the tap, while the sidetone began with the
// keyer; and the modem stops asking once its hang time has passed. And the
// spectrum:
// sent only to a client that asked for it, and no longer once it says
// "spectrum off". And a client that stalls while
// the radio changes much: it is not dropped, it is sent the last value of
// FREQ though not every one, and few lines of the spectrum, not the
// backlog; and once it has caught up, the next line.

#include <arpa/inet.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "board.h"
#include "hw_stand_ins.h"
#include "gpio.h"
#include "key.h"
#include "sdr.h"
#include "control.h"
#include "modem.h"
#include "radio.h"
#include "server.h"
#include "settings.h"

#define TEST_PORT 18081
#define HEARD_MAX 4096
#define SERVE_ROUNDS 20
#define ROUND_MS 5
#define BLOCK_FRAMES 1024

#define ANSWER_MAX 100
#define SAMPLE_RATE 96000.0
#define TUNED_HZ 28125.0
#define TONE_OFFSET_HZ 1000.0
#define TONE_LEVEL 2.0e6
#define TONE_BLOCKS 24
#define TONE_SETTLING_BLOCKS 12
#define ONE_SIDED 30.0
// the receiver says nothing for its first blocks after it starts
#define START_UP_MUTE_BLOCKS 50

#define CW_BLOCKS 64
#define BLOCKS_TO_TRANSMIT 1
// tr_switch()'s 33 ms less the 14 ms the modem has in hand
#define RF_DELAY (19 * 96)
#define FILTER_DELAY 512
#define BURST_FRACTION 10
#define BURST_SLACK 300

// a squeeze: the dash paddle 30 ms after the dot's, both held for 0.9 s,
// which at 20 words a minute is dot, dash, three times over
#define SQUEEZE_BLOCKS 160
#define SQUEEZE_LATER_US 30000
#define SQUEEZE_US 900000
#define SQUEEZE_MARKS ".-.-.-"
// the dash paddle held alone for 0.4 s, two dashes, then the dot's as well
#define DOT_JOINS_US 400000
#define DASH_FIRST_MARKS "--.-."
#define BANDS_KEPT_IN "build/test_server_settings.ini"
#define MARKS_MAX 64
// vu2ese at 20 words a minute is 4.6 seconds, and the hang time after it
#define MACRO_BLOCKS 520
#define VU2ESE_IN_MORSE "...-..-..---....."
#define MARK_FRACTION 10
#define WHY_MAX 100

#define TX_TEST_HZ 14100000
#define TONE_A_HZ 700.0
#define TONE_B_HZ 1900.0

#define SAMPLES_PER_MS 96
#define TAP_AT_US 20000
#define TAP_US 5000
#define KEYER_LAG_US 12000
#define DOT_AT_20_WPM 5760
#define ENVELOPE_FALL 478
#define SETTLING_BLOCKS 8
#define SIDETONE_BLOCKS 24

#define STALLED_CHANGES 3000
#define STALLED_CHANGES_A_LINE 10
#define STALLED_FIRST_HZ 14000000
#define STALLED_STEP_HZ 10
#define STALLED_POINTS 534
#define STALLED_RECEIVE_BUFFER 2048
#define STALLED_HEARD_MAX 262144
#define STALLED_QUIET_ROUNDS 5
#define STALLED_LINES_AT_MOST 2

static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static void serve_a_while(void) {
	struct pollfd polls[SERVER_POLLS];
	int count;
	int i;

	for (i = 0; i < SERVE_ROUNDS; i++) {
		count = server_get_pollfds(polls);
		if (poll(polls, count, ROUND_MS) > 0)
			server_handle_events(polls, count);
	}
}

static int connect_a_client(void) {
	struct sockaddr_in where;
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	memset(&where, 0, sizeof(where));
	where.sin_family = AF_INET;
	where.sin_port = htons(TEST_PORT);
	where.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, (struct sockaddr *)&where, sizeof(where)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void say(int fd, const char *line) {
	if (write(fd, line, strlen(line)) < 0)
		perror("write");
	serve_a_while();
}

static void hear(int fd, char *heard) {
	ssize_t got;

	serve_a_while();
	got = recv(fd, heard, HEARD_MAX - 1, MSG_DONTWAIT);
	if (got < 0)
		got = 0;
	heard[got] = 0;
}

static int has(const char *heard, const char *line) {
	return strstr(heard, line) != NULL;
}

static int is_passband(int low_hz, int high_hz) {
	static int32_t from_receiver[BLOCK_FRAMES];
	static int32_t from_microphone[BLOCK_FRAMES];
	static int32_t to_speaker[BLOCK_FRAMES];
	static int32_t to_transmitter[BLOCK_FRAMES];
	struct audio_block block;

	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = BLOCK_FRAMES;
	hw_stand_in_block(&block);
	return rx_list->low_hz == low_hz && rx_list->high_hz == high_hz;
}

// how loud the speaker is with a steady tone coming in, this far from the
// tuned frequency: the receiver is run for some blocks to settle, and the
// speaker's samples in the blocks after are measured
static double tone_heard(double offset_hz) {
	static int32_t from_receiver[BLOCK_FRAMES];
	static int32_t from_microphone[BLOCK_FRAMES];
	static int32_t to_speaker[BLOCK_FRAMES];
	static int32_t to_transmitter[BLOCK_FRAMES];
	static double seconds;
	struct audio_block block;
	double sum = 0;
	int blocks;
	int i;

	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = BLOCK_FRAMES;
	for (blocks = 0; blocks < TONE_BLOCKS; blocks++) {
		for (i = 0; i < BLOCK_FRAMES; i++) {
			from_receiver[i] = (int32_t)(TONE_LEVEL
				* sin(2 * M_PI * (TUNED_HZ + offset_hz) * seconds));
			seconds += 1.0 / SAMPLE_RATE;
		}
		hw_stand_in_block(&block);
		if (blocks < TONE_SETTLING_BLOCKS)
			continue;
		for (i = 0; i < BLOCK_FRAMES; i++)
			sum += (double)to_speaker[i] * to_speaker[i];
	}
	return sqrt(sum);
}

// which side of the tuned frequency is heard: 1 for above, -1 for below,
// 0 when neither is much louder than the other
static int side_heard(void) {
	double above = tone_heard(TONE_OFFSET_HZ);
	double below = tone_heard(-TONE_OFFSET_HZ);

	if (above > below * ONE_SIDED)
		return 1;
	if (below > above * ONE_SIDED)
		return -1;
	return 0;
}

// the receiver is run in silence for some blocks, and where the speaker
// was not silent is found: the sample it began at and how many samples it
// lasted, both 0 if it stayed silent
static void find_the_sidetone(int blocks, int *first, int *length) {
	static int32_t from_receiver[BLOCK_FRAMES];
	static int32_t from_microphone[BLOCK_FRAMES];
	static int32_t to_speaker[BLOCK_FRAMES];
	static int32_t to_transmitter[BLOCK_FRAMES];
	struct audio_block block;
	int last = 0;
	int b;
	int i;

	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = BLOCK_FRAMES;
	*first = 0;
	*length = 0;
	for (b = 0; b < blocks; b++) {
		hw_stand_in_block(&block);
		for (i = 0; i < BLOCK_FRAMES; i++) {
			if (to_speaker[i] == 0)
				continue;
			if (*length == 0)
				*first = b * BLOCK_FRAMES + i;
			last = b * BLOCK_FRAMES + i;
			*length = last - *first + 1;
		}
	}
}

// the radio is run for some blocks with nothing coming in, and what it
// sends to the transmitter is measured: how strong it is at a frequency
// this far from the middle of the IF, and whether the speaker stayed silent
static double transmitted_at(double offset_hz, int *is_speaker_silent) {
	static int32_t from_receiver[BLOCK_FRAMES];
	static int32_t from_microphone[BLOCK_FRAMES];
	static int32_t to_speaker[BLOCK_FRAMES];
	static int32_t to_transmitter[BLOCK_FRAMES];
	struct audio_block block;
	double angle = 2 * M_PI * (TUNED_HZ + offset_hz) / SAMPLE_RATE;
	double in_phase = 0;
	double quadrature = 0;
	long n = 0;
	int b;
	int i;

	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.to_speaker = to_speaker;
	block.to_transmitter = to_transmitter;
	block.frames = BLOCK_FRAMES;
	*is_speaker_silent = 1;
	for (b = 0; b < TONE_BLOCKS; b++) {
		hw_stand_in_block(&block);
		if (b < TONE_SETTLING_BLOCKS)
			continue;
		for (i = 0; i < BLOCK_FRAMES; i++) {
			in_phase += to_transmitter[i] * cos(angle * n);
			quadrature += to_transmitter[i] * sin(angle * n);
			n++;
			if (to_speaker[i] != 0)
				*is_speaker_silent = 0;
		}
	}
	return sqrt(in_phase * in_phase + quadrature * quadrature) / n;
}

static void check_transmitting(int one, int other) {
	const struct board *board = board_in_use();
	char heard[HEARD_MAX];
	char other_heard[HEARD_MAX];
	int is_silent;
	double tone_a;
	double tone_b;
	double other_sideband;
	double at_twice_the_drive;
	int filter_pin = board_filter_for(TX_TEST_HZ)->pin;

	say(one, "BAND 20M\n");
	say(one, "MODE AM\n");
	hear(one, heard);
	say(one, "TX ON\n");
	hear(one, heard);
	expect(has(heard, "ERROR TX") && gpio_read(board->transmitter_pin) == 0,
		"TX ON in AM, which cannot transmit yet, is refused and keys nothing");

	say(one, "MODE 2TONE\n");
	say(one, "DRIVE 40\n");
	hear(other, other_heard);
	say(one, "TX ON\n");
	hear(other, other_heard);
	expect(has(other_heard, "TX ON\n") && gpio_read(board->transmitter_pin) == 1
		&& gpio_read(board->receiver_pin) == 0 && gpio_read(filter_pin) == 1,
		"TX ON in 2TONE: transmitter on, receiver off, the band's filter selected");
	expect(hw_stand_in_pin_changed_at(board->receiver_pin)
			< hw_stand_in_pin_changed_at(filter_pin)
		&& hw_stand_in_pin_changed_at(filter_pin)
			< hw_stand_in_pin_changed_at(board->transmitter_pin),
		"in that order: receiver off, then the filter, then the transmitter on");
	tone_a = transmitted_at(TONE_A_HZ, &is_silent);
	tone_b = transmitted_at(TONE_B_HZ, &is_silent);
	other_sideband = transmitted_at(-TONE_A_HZ, &is_silent);
	expect(tone_a > 0 && tone_b > tone_a / 2 && tone_b < tone_a * 2
		&& other_sideband < tone_a / ONE_SIDED && is_silent,
		"two tones of like strength go to the transmitter, 700 and 1900 Hz above the "
		"middle of the IF, and nothing on the other side or in the speaker");
	say(one, "DRIVE 80\n");
	at_twice_the_drive = transmitted_at(TONE_A_HZ, &is_silent);
	expect(at_twice_the_drive > tone_a * 1.9 && at_twice_the_drive < tone_a * 2.1,
		"twice the drive is twice the level");
	say(one, "DRIVE 0\n");
	expect(transmitted_at(TONE_A_HZ, &is_silent) == 0, "and no drive is nothing sent");
	say(one, "DRIVE 40\n");

	say(one, "TX OFF\n");
	expect(gpio_read(board->transmitter_pin) == 0 && gpio_read(board->receiver_pin) == 1
		&& gpio_read(filter_pin) == 0
		&& hw_stand_in_pin_changed_at(board->transmitter_pin)
			< hw_stand_in_pin_changed_at(board->receiver_pin),
		"TX OFF: the transmitter off first, then the receiver on, the filters released");
	transmitted_at(TONE_A_HZ, &is_silent);
	expect(transmitted_at(TONE_A_HZ, &is_silent) == 0,
		"and nothing more goes to the transmitter");

	say(one, "TX ON\n");
	hear(one, heard);
	say(one, "MODE USB\n");
	hear(one, heard);
	expect(has(heard, "TX OFF\n") && gpio_read(board->transmitter_pin) == 0,
		"a change of mode while transmitting goes back to receive first");
	say(one, "TX ON\n");
	expect(gpio_read(board->transmitter_pin) == 1
		&& transmitted_at(TONE_A_HZ, &is_silent) == 0,
		"USB transmits, and with a silent microphone sends nothing");
	say(one, "TX OFF\n");
	say(one, "FREQ 13000000\n");
	say(one, "MODE 2TONE\n");
	say(one, "TX ON\n");
	expect(transmitted_at(TONE_A_HZ, &is_silent) == 0,
		"outside every band, nothing is sent to the transmitter");
	say(one, "TX OFF\n");
	say(one, "BAND 20M\n");
	say(one, "MODE USB\n");
}

// where a stream of samples is louder than a tenth of its loudest: the
// first such sample and how many samples from it to the last
static void find_the_burst(const int32_t *samples, int count, int *first, int *length) {
	long loudest = 0;
	int last = 0;
	int i;

	*first = 0;
	*length = 0;
	for (i = 0; i < count; i++) {
		if (labs((long)samples[i]) > loudest)
			loudest = labs((long)samples[i]);
	}
	if (loudest == 0)
		return;
	for (i = 0; i < count; i++) {
		if (labs((long)samples[i]) * BURST_FRACTION < loudest)
			continue;
		if (*length == 0)
			*first = i;
		last = i;
		*length = last - *first + 1;
	}
}

// the marks in a run of samples, as dots and dashes: a mark is where the
// samples are loud for a millisecond at a stretch, a dot if it is nearer
// one dot long than three
static void read_the_marks(const int32_t *samples, int count, char *marks, int room) {
	long loudest = 0;
	long peak;
	int mark_ms = 0;
	int n = 0;
	int ms;
	int i;

	for (i = 0; i < count; i++) {
		if (labs((long)samples[i]) > loudest)
			loudest = labs((long)samples[i]);
	}
	for (ms = 0; ms < count / SAMPLES_PER_MS && n < room - 1; ms++) {
		peak = 0;
		for (i = 0; i < SAMPLES_PER_MS; i++) {
			if (labs((long)samples[ms * SAMPLES_PER_MS + i]) > peak)
				peak = labs((long)samples[ms * SAMPLES_PER_MS + i]);
		}
		if (loudest > 0 && peak * MARK_FRACTION >= loudest) {
			mark_ms++;
			continue;
		}
		if (mark_ms > 0) {
			marks[n] = '.';
			if (mark_ms > 2 * DOT_AT_20_WPM / SAMPLES_PER_MS)
				marks[n] = '-';
			n++;
		}
		mark_ms = 0;
	}
	marks[n] = 0;
}

// both paddles held, as an operator squeezes them, the radio going to
// transmit when the modem asks as tools/sdr.c would have it go
static void check_cw_squeeze(int one, int first_paddle, int second_paddle,
		int64_t later_us, const char *marks, const char *what) {
	static int32_t from_receiver[BLOCK_FRAMES];
	static int32_t from_microphone[BLOCK_FRAMES];
	static int32_t speaker[SQUEEZE_BLOCKS * BLOCK_FRAMES];
	static int32_t transmitter[SQUEEZE_BLOCKS * BLOCK_FRAMES];
	const struct modem *cw = modem_find("CW");
	struct audio_block block;
	char heard[MARKS_MAX];
	char sent[MARKS_MAX];
	char why[WHY_MAX];
	int is_transmitting = 0;
	int first;
	int length;
	int b;

	say(one, "MODE CW\n");
	say(one, "CW_INPUT IAMBIC\n");
	say(one, "WPM 20\n");
	find_the_sidetone(SETTLING_BLOCKS, &first, &length);
	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.frames = BLOCK_FRAMES;
	hw_stand_in_key(first_paddle, 1, TAP_AT_US);
	hw_stand_in_key(second_paddle, 1, TAP_AT_US + later_us);
	hw_stand_in_key(first_paddle, 0, TAP_AT_US + SQUEEZE_US);
	hw_stand_in_key(second_paddle, 0, TAP_AT_US + SQUEEZE_US);
	for (b = 0; b < SQUEEZE_BLOCKS; b++) {
		block.to_speaker = speaker + b * BLOCK_FRAMES;
		block.to_transmitter = transmitter + b * BLOCK_FRAMES;
		hw_stand_in_block(&block);
		if (cw->wants_tx() && !is_transmitting)
			control_set("TX", "ON", ORIGIN_NONE, why, sizeof(why));
		if (!cw->wants_tx() && is_transmitting)
			control_set("TX", "OFF", ORIGIN_NONE, why, sizeof(why));
		is_transmitting = cw->wants_tx();
	}
	control_set("TX", "OFF", ORIGIN_NONE, why, sizeof(why));
	read_the_marks(speaker, SQUEEZE_BLOCKS * BLOCK_FRAMES, heard, sizeof(heard));
	read_the_marks(transmitter, SQUEEZE_BLOCKS * BLOCK_FRAMES, sent, sizeof(sent));
	printf("          a squeeze is heard as %s and sent as %s\n", heard, sent);
	expect(strcmp(heard, marks) == 0 && strcmp(sent, marks) == 0, what);
	say(one, "MODE USB\n");
}

// a macro is sent as CW: the radio goes to transmit for it by itself and
// what reaches the transmitter is the macro's text with the callsign put in
static void check_cw_macro(int one) {
	static int32_t from_receiver[BLOCK_FRAMES];
	static int32_t from_microphone[BLOCK_FRAMES];
	static int32_t speaker[MACRO_BLOCKS * BLOCK_FRAMES];
	static int32_t transmitter[MACRO_BLOCKS * BLOCK_FRAMES];
	const struct modem *cw = modem_find("CW");
	struct audio_block block;
	char heard[HEARD_MAX];
	char sent[MARKS_MAX];
	char why[WHY_MAX];
	int is_transmitting = 0;
	int first;
	int length;
	int b;

	say(one, "MODE USB\n");
	hear(one, heard);
	say(one, "MACRO Call\n");
	hear(one, heard);
	expect(has(heard, "ERROR MACRO"), "a macro is refused outside CW");
	say(one, "MODE CW\n");
	say(one, "WPM 20\n");
	say(one, "MYCALLSIGN vu2ese\n");
	say(one, "MACRO nothing like it\n");
	hear(one, heard);
	expect(has(heard, "ERROR MACRO"), "and so is one that the file does not have");
	find_the_sidetone(SETTLING_BLOCKS, &first, &length);
	say(one, "MACRO Call\n");
	hear(one, heard);
	expect(!has(heard, "ERROR"), "the macro labelled Call is taken");

	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.frames = BLOCK_FRAMES;
	for (b = 0; b < MACRO_BLOCKS; b++) {
		block.to_speaker = speaker + b * BLOCK_FRAMES;
		block.to_transmitter = transmitter + b * BLOCK_FRAMES;
		hw_stand_in_block(&block);
		if (cw->wants_tx() && !is_transmitting)
			control_set("TX", "ON", ORIGIN_NONE, why, sizeof(why));
		if (!cw->wants_tx() && is_transmitting)
			control_set("TX", "OFF", ORIGIN_NONE, why, sizeof(why));
		is_transmitting = cw->wants_tx();
	}
	control_set("TX", "OFF", ORIGIN_NONE, why, sizeof(why));
	read_the_marks(transmitter, MACRO_BLOCKS * BLOCK_FRAMES, sent, sizeof(sent));
	printf("          the macro is sent as %s\n", sent);
	expect(strcmp(sent, VU2ESE_IN_MORSE) == 0,
		"it is sent as the station's callsign, in Morse, whole from its first dot");
	expect(!is_transmitting, "and the radio is back in receive when it is done");
	say(one, "MODE USB\n");
}

static void check_cw_transmitting(int one) {
	static int32_t from_receiver[BLOCK_FRAMES];
	static int32_t from_microphone[BLOCK_FRAMES];
	static int32_t speaker[CW_BLOCKS * BLOCK_FRAMES];
	static int32_t transmitter[CW_BLOCKS * BLOCK_FRAMES];
	const struct modem *cw = modem_find("CW");
	struct audio_block block;
	char why[CONTROL_WHY_MAX];
	int wanted_at_block = CW_BLOCKS;
	int tap_sample = TAP_AT_US * SAMPLES_PER_MS / 1000;
	int lag = KEYER_LAG_US * SAMPLES_PER_MS / 1000;
	int first;
	int length;
	int b;

	say(one, "BAND 20M\n");
	say(one, "MODE CW\n");
	say(one, "CW_INPUT IAMBIC\n");
	say(one, "WPM 20\n");
	say(one, "DRIVE 40\n");
	find_the_sidetone(SETTLING_BLOCKS, &first, &length);
	block.from_receiver = from_receiver;
	block.from_microphone = from_microphone;
	block.frames = BLOCK_FRAMES;
	hw_stand_in_key(KEY_TIP, 1, TAP_AT_US);
	hw_stand_in_key(KEY_TIP, 0, TAP_AT_US + TAP_US);
	for (b = 0; b < CW_BLOCKS; b++) {
		block.to_speaker = speaker + b * BLOCK_FRAMES;
		block.to_transmitter = transmitter + b * BLOCK_FRAMES;
		hw_stand_in_block(&block);
		if (wanted_at_block == CW_BLOCKS && cw->wants_tx())
			wanted_at_block = b;
		if (b == wanted_at_block + BLOCKS_TO_TRANSMIT)
			control_set("TX", "ON", ORIGIN_NONE, why, sizeof(why));
	}
	expect(wanted_at_block == tap_sample / BLOCK_FRAMES + 1
		|| wanted_at_block == (tap_sample + lag) / BLOCK_FRAMES,
		"a tap of the dot paddle in receive makes the CW modem ask for the transmitter");
	find_the_burst(speaker, CW_BLOCKS * BLOCK_FRAMES, &first, &length);
	expect(abs(first - (tap_sample + lag)) < BURST_SLACK
		&& abs(length - DOT_AT_20_WPM) < BURST_SLACK,
		"the sidetone is a whole dot, begun the keyer's 12 ms after the tap");
	find_the_burst(transmitter, CW_BLOCKS * BLOCK_FRAMES, &first, &length);
	printf("          the dot is sent %d samples after the tap, %d samples long\n",
		first - tap_sample, length);
	expect(abs(first - (tap_sample + lag + RF_DELAY + FILTER_DELAY)) < BURST_SLACK
		&& abs(length - DOT_AT_20_WPM) < BURST_SLACK,
		"the transmitter is sent a whole dot, after the sidetone by what the changeover needs");
	expect(!cw->wants_tx(), "and the modem stops asking after its hang time");
	control_set("TX", "OFF", ORIGIN_NONE, why, sizeof(why));
	say(one, "MODE USB\n");
}

// the bands' places are written with the settings and read back: where the
// radio was on a band's first and second places, and which it was at
static void check_bands_kept(int one) {
	char heard[HEARD_MAX];

	settings_set_extras(radio_save_bands, radio_load_band);
	remove(BANDS_KEPT_IN);
	settings_load(BANDS_KEPT_IN);
	say(one, "BAND 40M\n");
	say(one, "STACK 0\n");
	say(one, "FREQ 7123000\n");
	say(one, "STACK 1\n");
	say(one, "FREQ 7011000\n");
	hear(one, heard);
	settings_save();

	say(one, "FREQ 7033000\n");
	say(one, "STACK 0\n");
	say(one, "FREQ 7200000\n");
	say(one, "BAND 20M\n");
	hear(one, heard);
	settings_load(BANDS_KEPT_IN);
	hear(one, heard);
	expect(has(heard, "FREQ 7011000\n") && has(heard, "MODE CW\n"),
		"the settings read back, the radio is where it was when they were written");
	expect(strcmp(control_get("STACK"), "1") == 0, "at the place of the band's stack it was at");
	say(one, "STACK 0\n");
	hear(one, heard);
	expect(has(heard, "FREQ 7123000\n") && has(heard, "MODE LSB\n"),
		"and the band's other place is what it was then, not what it was changed to since");
	expect(!radio_load_band("BAND.40M", "1 7088000:LSB 99:CW 7040000:CW 7074000:USB")
		&& !radio_load_band("BAND.40M", "7 7088000:LSB 7000000:CW 7040000:CW 7074000:USB")
		&& !radio_load_band("BAND.40M", "1 7088000:LSB 7000000:FM 7040000:CW 7074000:USB"),
		"a band's line with a frequency outside the band, a place it has not, or a mode the radio has not, is not taken");
	settings_set_extras(NULL, NULL);
	say(one, "BAND 20M\n");
	say(one, "FREQ 14074000\n");
	hear(one, heard);
}

static const uint8_t spectrum_points[] = { 0x00, 0x1c, 0xff };

static int test_apply_calls;

static int count_applies(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)value;
	(void)why;
	(void)room;
	test_apply_calls++;
	return 0;
}

static struct control stepped = {
	.group = "TEST", .name = "STEPPED", .kind = CONTROL_NUMBER,
	.min = 0, .max = 100, .step = 5, .value = "0", .apply = count_applies,
};

// how many times a line beginning with this appears in what was heard
static int count_lines(const char *heard, const char *beginning) {
	const char *at = heard;
	int count = 0;

	while ((at = strstr(at, beginning)) != NULL) {
		if (at == heard || at[-1] == '\n')
			count++;
		at++;
	}
	return count;
}

// a client with a small receive buffer that does not read while the radio
// makes STALLED_CHANGES changes of FREQ and a line of the spectrum every
// tenth one; then it reads all it was sent, and once it has caught up it
// is sent the next line of the spectrum
static int a_stalled_client_is_kept_up_to_date(void) {
	static char heard[STALLED_HEARD_MAX];
	static uint8_t points[STALLED_POINTS];
	char why[CONTROL_WHY_MAX];
	char hz[CONTROL_VALUE_MAX];
	int small = STALLED_RECEIVE_BUFFER;
	struct sockaddr_in where;
	int stalled = socket(AF_INET, SOCK_STREAM, 0);
	int length = 0;
	ssize_t got;
	int quiet = 0;
	int is_line_sent_after;
	int i;

	setsockopt(stalled, SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
	memset(&where, 0, sizeof(where));
	where.sin_family = AF_INET;
	where.sin_port = htons(TEST_PORT);
	where.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(stalled, (struct sockaddr *)&where, sizeof(where)) < 0)
		return 0;
	say(stalled, "spectrum on\n");
	for (i = 1; i <= STALLED_CHANGES; i++) {
		snprintf(hz, sizeof(hz), "%d", STALLED_FIRST_HZ + i * STALLED_STEP_HZ);
		control_set("FREQ", hz, ORIGIN_NONE, why, sizeof(why));
		if (i % STALLED_CHANGES_A_LINE == 0)
			server_send_spectrum(25031, points, sizeof(points));
	}
	while (quiet < STALLED_QUIET_ROUNDS && length < STALLED_HEARD_MAX - 1) {
		serve_a_while();
		got = recv(stalled, heard + length, STALLED_HEARD_MAX - 1 - length, MSG_DONTWAIT);
		if (got <= 0) {
			quiet++;
			continue;
		}
		quiet = 0;
		length += got;
	}
	heard[length] = 0;
	server_send_spectrum(25031, points, sizeof(points));
	serve_a_while();
	got = recv(stalled, heard + length, STALLED_HEARD_MAX - 1 - length, MSG_DONTWAIT);
	is_line_sent_after = got > 0 && has(heard + length, "SPECTRUM ");
	close(stalled);
	snprintf(hz, sizeof(hz), "FREQ %d\n", STALLED_FIRST_HZ + STALLED_CHANGES * STALLED_STEP_HZ);
	printf("          the stalled client was sent %d of %d changes and %d of %d lines\n",
		count_lines(heard, "FREQ "), STALLED_CHANGES,
		count_lines(heard, "SPECTRUM "), STALLED_CHANGES / STALLED_CHANGES_A_LINE);
	return has(heard, hz) && count_lines(heard, "FREQ ") < STALLED_CHANGES
		&& count_lines(heard, "SPECTRUM ") <= STALLED_LINES_AT_MOST
		&& is_line_sent_after;
}

int main(void) {
	char heard[HEARD_MAX];
	char other_heard[HEARD_MAX];
	int one;
	int other;
	uint32_t oscillator_before;
	char answer[ANSWER_MAX];
	int usb_side;
	int lsb_side;
	int sidetone_first;
	int sidetone_length;
	int i;

	hw_stand_in_radio(board_zbitx_1.radio);
	if (setup() < 0) {
		printf("could not set up\n");
		return EXIT_FAILURE;
	}
	radio_open();
	control_register(&stepped);
	control_on_change(server_broadcast);
	control_update("AUDIO", "61");
	expect(fcntl(STDIN_FILENO, F_GETFD) != -1,
		"a change told before the server opens closes nothing");
	if (server_open(TEST_PORT) < 0) {
		printf("could not open the server\n");
		return EXIT_FAILURE;
	}

	one = connect_a_client();
	other = connect_a_client();
	hear(one, heard);
	hear(other, other_heard);
	expect(has(heard, "FREQ 7088000\n") && has(heard, "BAND 40M\n")
		&& has(heard, "MODE LSB\n") && has(heard, "AUDIO 61\n")
		&& has(heard, "WPM 20\n"),
		"a client is told every setting when it connects, a modem's too");

	say(one, "AUDIO 70\n");
	hear(one, heard);
	hear(other, other_heard);
	expect(!has(heard, "AUDIO") && has(other_heard, "AUDIO 70\n"),
		"a change is told to the other client, not to the one who made it");

	say(other, "AUDIO 70\n");
	hear(one, heard);
	hear(other, other_heard);
	expect(!has(heard, "AUDIO") && !has(other_heard, "AUDIO"),
		"a value already set is told to nobody");

	test_apply_calls = 0;
	say(one, "STEPPED 17\n");
	hear(one, heard);
	hear(other, other_heard);
	expect(has(heard, "STEPPED 15\n") && has(other_heard, "STEPPED 15\n")
		&& test_apply_calls == 1,
		"17 snapped onto steps of 5 is 15, and the one who asked is told");
	say(one, "STEPPED 15\n");
	expect(test_apply_calls == 1, "setting it to 15 again applies nothing");

	say(one, "BAND 20M\n");
	hear(one, heard);
	hear(other, other_heard);
	expect(!has(heard, "BAND") && has(heard, "FREQ 14200000\n")
		&& has(heard, "MODE USB\n"),
		"BAND 20M moves FREQ and MODE, and the one who asked is told of those");
	expect(has(other_heard, "BAND 20M\n") && has(other_heard, "FREQ 14200000\n"),
		"and the other client is told all of it");

	say(one, "FREQ 14074000\n");
	say(one, "FREQ 10500000\n");
	hear(one, heard);
	expect(has(heard, "BAND OUT\n"), "tuned out of every band, BAND is OUT");
	say(one, "BAND 40M\n");
	say(one, "BAND 20M\n");
	hear(one, heard);
	expect(has(heard, "FREQ 14074000\n"),
		"back on 20 m, the radio is where it was left on 20 m");

	say(one, "BAND 40M\n");
	hear(one, heard);
	say(one, "FREQ 7090000\n");
	say(one, "STACK 1\n");
	hear(one, heard);
	expect(has(heard, "FREQ 7000000\n") && has(heard, "MODE CW\n"),
		"STACK 1 goes to the second place of the band's stack, in its mode");
	say(one, "STACK 0\n");
	hear(one, heard);
	expect(has(heard, "FREQ 7090000\n") && has(heard, "MODE LSB\n"),
		"and STACK 0 comes back to where the first place was left");
	say(one, "STACK 1\n");
	say(one, "BAND 20M\n");
	hear(one, heard);
	expect(has(heard, "STACK 0\n") && has(heard, "FREQ 14074000\n"),
		"another band is at its own place, and STACK says which");
	say(one, "STACK 4\n");
	hear(one, heard);
	expect(has(heard, "ERROR STACK"), "a place the stack does not have is an ERROR");

	say(one, "CLOCK 2030-01-01 00:00 SET\n");
	say(one, "TIME_SET 39445920\n");
	hear(one, heard);
	expect(has(heard, "ERROR CLOCK") && has(heard, "ERROR TIME_SET"),
		"CLOCK cannot be set, nor a time given that is after the year 2100");
	say(one, "TIME_SET 395387\n");
	hear(one, heard);
	expect(!has(heard, "ERROR"), "a time in 2026 is taken");

	check_bands_kept(one);

	say(one, "FREQ 1\n");
	hear(one, heard);
	hear(other, other_heard);
	expect(has(heard, "ERROR FREQ") && !has(other_heard, "ERROR"),
		"a frequency the radio will not take is an ERROR, to the one who asked");
	say(one, "MODE FM\n");
	hear(one, heard);
	expect(has(heard, "ERROR MODE"), "so is a mode it does not have");

	say(other, "?\n");
	hear(other, other_heard);
	hear(one, heard);
	expect(has(other_heard, "AUDIO 70\n") && !has(heard, "AUDIO 70\n"),
		"? gets every setting again, to the one who asked");

	say(one, "schema\n");
	hear(one, heard);
	expect(has(heard, "CONTROL RADIO FREQ number 500000 30000000 1\n")
		&& has(heard, "CONTROL RADIO MODE choice USB/LSB/CW/CWR/AM/FT8/2TONE\n")
		&& has(heard, "CONTROL CW WPM number 5 50 1\n")
		&& has(heard, "CONTROL RADIO IF number 0 100 1\n")
		&& has(heard, "CONTROL RADIO DRIVE number 0 100 1\n")
		&& has(heard, "CONTROL VOICE BW_VOICE number 300 3000 50\n")
		&& has(heard, "CONTROL CW BW_CW number 50 3000 50\n")
		&& has(heard, "CONTROL CW CW_INPUT choice STRAIGHT/IAMBIC/IAMBICB/BUG/ULTIMATIC\n")
		&& has(heard, "CONTROL END\n"),
		"the schema lists the controls in their groups, CW's WPM among them");

	say(one, "MODE USB\n");
	expect(is_passband(300, 2500), "in USB the passband is 300 to 300 + BW_VOICE");
	say(one, "BW_VOICE 2700\n");
	expect(is_passband(300, 3000), "a wider BW_VOICE moves the high edge alone");
	say(one, "MODE CW\n");
	expect(is_passband(400, 800), "in CW the passband is BW_CW wide, centred on PITCH");
	oscillator_before = oscillator_frequency();
	say(one, "PITCH 700\n");
	expect(is_passband(500, 900), "a new PITCH moves the passband with it");
	expect(labs((long)oscillator_frequency() - (long)oscillator_before) == 100,
		"and the oscillator by as much, so the signal is heard at the new pitch");
	say(one, "BW_CW 200\n");
	expect(is_passband(600, 800), "a narrower BW_CW closes in on the pitch");
	say(one, "MODE USB\n");
	expect(is_passband(300, 3000), "back in USB, the voice bandwidth is as it was left");
	say(one, "MODE CWR\n");
	expect(is_passband(600, 800), "and CWR has CW's");

	sdr_request("r1:agc=OFF", answer);
	for (i = 0; i < START_UP_MUTE_BLOCKS; i += TONE_BLOCKS)
		tone_heard(TONE_OFFSET_HZ);
	say(one, "MODE USB\n");
	usb_side = side_heard();
	say(one, "MODE LSB\n");
	lsb_side = side_heard();
	expect(usb_side != 0 && lsb_side == -usb_side,
		"changed from USB to LSB, the receiver hears the other sideband, and that one alone");
	say(one, "MODE USB\n");
	expect(side_heard() == usb_side, "and changed back to USB, the first again");
	say(one, "MODE CW\n");
	say(one, "MODE LSB\n");
	expect(side_heard() == lsb_side, "from CW to LSB, LSB's sideband");
	sdr_request("r1:agc=SLOW", answer);

	say(one, "MODE CW\n");
	say(one, "WPM 20\n");
	find_the_sidetone(SETTLING_BLOCKS, &sidetone_first, &sidetone_length);
	hw_stand_in_key(KEY_TIP, 1, TAP_AT_US);
	hw_stand_in_key(KEY_TIP, 0, TAP_AT_US + TAP_US);
	find_the_sidetone(SIDETONE_BLOCKS, &sidetone_first, &sidetone_length);
	expect(abs(sidetone_first - (TAP_AT_US + KEYER_LAG_US) * SAMPLES_PER_MS / 1000) <= 2
		&& abs(sidetone_length - (DOT_AT_20_WPM + ENVELOPE_FALL)) <= 4,
		"a 5 ms tap of the dot paddle is a whole dot of sidetone, 12 ms after the tap");
	find_the_sidetone(SETTLING_BLOCKS, &sidetone_first, &sidetone_length);
	expect(sidetone_length == 0, "and then the speaker is the receiver's again");
	say(one, "CW_INPUT STRAIGHT\n");
	hw_stand_in_key(KEY_RING, 1, 0);
	find_the_sidetone(SETTLING_BLOCKS, &sidetone_first, &sidetone_length);
	expect(sidetone_length == 0,
		"with a straight key, the ring held closed by its plug keys nothing");
	hw_stand_in_key(KEY_RING, 0, 0);
	find_the_sidetone(SETTLING_BLOCKS, &sidetone_first, &sidetone_length);
	say(one, "CW_INPUT IAMBIC\n");

	check_transmitting(one, other);
	check_cw_transmitting(one);
	check_cw_macro(one);
	check_cw_squeeze(one, KEY_TIP, KEY_RING, SQUEEZE_LATER_US, SQUEEZE_MARKS,
		"both paddles held take turns, heard and sent, through the modem");
	check_cw_squeeze(one, KEY_RING, KEY_TIP, DOT_JOINS_US, DASH_FIRST_MARKS,
		"the dot paddle closed while dashes are being sent makes them take turns");

	say(one, "spectrum on\n");
	server_send_spectrum(25031, spectrum_points, sizeof(spectrum_points));
	hear(one, heard);
	hear(other, other_heard);
	expect(has(heard, "SPECTRUM 25031 001cff\n") && !has(other_heard, "SPECTRUM"),
		"the spectrum goes to a client that asked for it, and no other");
	say(one, "spectrum off\n");
	server_send_spectrum(25031, spectrum_points, sizeof(spectrum_points));
	hear(one, heard);
	expect(!has(heard, "SPECTRUM") && !server_wants_spectrum(),
		"and not after it says spectrum off");

	close(other);
	serve_a_while();
	other = connect_a_client();
	hear(other, other_heard);
	say(one, "AUDIO 75\n");
	hear(other, other_heard);
	expect(has(other_heard, "AUDIO 75\n"),
		"a client that goes is forgotten, and a new one is told of changes");

	expect(a_stalled_client_is_kept_up_to_date(),
		"a client that stalls is not dropped, and is sent the last FREQ, not the backlog");

	close(one);
	close(other);
	server_close();
	teardown();
	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("control protocol: all checks passed\n");
	return EXIT_SUCCESS;
}
