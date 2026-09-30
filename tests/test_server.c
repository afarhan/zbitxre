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
// It registers one control of its own, STEPPED, a number in steps of 5, to
// see a value snapped; no control of the radio's is snapped yet.
//
// Exits 0 when every check passes.

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
#include "sdr.h"
#include "control.h"
#include "radio.h"
#include "server.h"

#define TEST_PORT 18081
#define HEARD_MAX 4096
#define SERVE_ROUNDS 20
#define ROUND_MS 5

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
		&& has(heard, "CONTROL END\n"),
		"the schema lists the controls in their groups, CW's WPM among them");

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
