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
// offered. And the spectrum: sent only to a client that asked for it, and
// no longer once it says "spectrum off".
//
// And the frequency's own controls: each VFO keeps its frequency, and one
// not yet used opens where the other is; SPLIT ON goes to VFO A; RIT moves
// the receiver by RIT_DELTA, only while it is on, and keeps it there as the
// frequency is tuned. And tuning as a knob does, with "tune": by STEP, or
// RIT_DELTA while RIT is on, told to everyone; and the readings, RX_FREQ
// and TX_FREQ, following RIT and SPLIT, and not to be set by a client.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "board.h"
#include "hw_stand_ins.h"
#include "sdr.h"
#include "control.h"
#include "radio.h"
#include "server.h"

#define TEST_PORT 18081
#define HEARD_MAX 4096
#define SERVE_ROUNDS 20
#define ROUND_MS 5
#define BLOCK_FRAMES 1024

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

int main(void) {
	char heard[HEARD_MAX];
	char other_heard[HEARD_MAX];
	int one;
	int other;
	uint32_t oscillator_before;

	if (setup(&board_zbitx_1) < 0) {
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
		&& has(heard, "CONTROL RADIO MODE choice USB/LSB/CW/CWR/AM\n")
		&& has(heard, "CONTROL CW WPM number 5 50 1\n")
		&& has(heard, "CONTROL RADIO IF number 0 100 1\n")
		&& has(heard, "CONTROL RADIO DRIVE number 0 100 1\n")
		&& has(heard, "CONTROL VOICE BW_VOICE number 300 3000 50\n")
		&& has(heard, "CONTROL CW BW_CW number 50 3000 50\n")
		&& has(heard, "CONTROL TUNING STEP choice 10H/100H/500H/1K/10K\n")
		&& has(heard, "CONTROL TUNING VFO choice A/B\n")
		&& has(heard, "CONTROL TUNING RIT_DELTA number -25000 25000 1\n")
		&& has(heard, "CONTROL TUNING RX_FREQ reading\n")
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

	say(one, "FREQ 14100000\n");
	say(one, "VFO B\n");
	hear(one, heard);
	expect(has(heard, "FREQ 14100000\n") || control_get_int("FREQ") == 14100000,
		"VFO B, not yet used, opens where A is");
	say(one, "FREQ 14150000\n");
	say(one, "VFO A\n");
	hear(one, heard);
	expect(has(heard, "FREQ 14100000\n"), "back on VFO A, the radio is where A was left");
	say(one, "VFO B\n");
	hear(one, heard);
	expect(has(heard, "FREQ 14150000\n"), "and on VFO B, where B was left");
	say(one, "SPLIT ON\n");
	hear(one, heard);
	expect(has(heard, "VFO A\n") && has(heard, "FREQ 14100000\n"),
		"SPLIT ON goes to VFO A, to receive on A and transmit on B");
	say(one, "SPLIT OFF\n");

	oscillator_before = oscillator_frequency();
	say(one, "RIT_DELTA 500\n");
	expect(oscillator_frequency() == oscillator_before,
		"RIT_DELTA moves nothing while RIT is off");
	say(one, "RIT ON\n");
	expect(labs((long)oscillator_frequency() - (long)oscillator_before) == 500,
		"RIT ON moves the receiver by RIT_DELTA");
	say(one, "FREQ 14101000\n");
	expect(labs((long)oscillator_frequency() - (long)oscillator_before) == 1500,
		"and it stays RIT_DELTA from the frequency as it is tuned");
	say(one, "RIT OFF\n");
	expect(labs((long)oscillator_frequency() - (long)oscillator_before) == 1000,
		"RIT OFF brings the receiver back to the frequency");

	say(one, "FREQ 14101500\n");
	hear(one, heard);
	say(one, "tune 1\n");
	hear(one, heard);
	hear(other, other_heard);
	expect(has(heard, "FREQ 14102000\n") && has(other_heard, "FREQ 14102000\n"),
		"tune 1 moves FREQ a STEP, onto a multiple of it, and tells everyone");
	say(one, "STEP 100H\n");
	say(one, "tune -3\n");
	hear(one, heard);
	expect(has(heard, "FREQ 14101700\n"), "tune -3 moves it three STEPs down");
	say(one, "RIT ON\n");
	hear(one, heard);
	say(one, "tune 2\n");
	hear(one, heard);
	expect(has(heard, "RIT_DELTA 700\n") && control_get_int("FREQ") == 14101700
		&& has(heard, "RX_FREQ 14102400\n") && control_get_int("TX_FREQ") == 14101700,
		"with RIT on, tune moves RIT_DELTA and RX_FREQ, and not FREQ or TX_FREQ");
	say(one, "RIT OFF\n");
	hear(one, heard);
	expect(has(heard, "RX_FREQ 14101700\n"), "RIT OFF brings RX_FREQ back to FREQ");
	say(one, "SPLIT ON\n");
	hear(one, heard);
	expect(has(heard, "TX_FREQ 14150000\n"), "with SPLIT on, TX_FREQ is the other VFO's");
	say(one, "SPLIT OFF\n");
	hear(one, heard);
	expect(has(heard, "TX_FREQ 14101700\n"), "and with SPLIT off, FREQ again");
	say(one, "RX_FREQ 7000000\n");
	hear(one, heard);
	expect(has(heard, "ERROR RX_FREQ"), "a reading cannot be set by a client");

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
