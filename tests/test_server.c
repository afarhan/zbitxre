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
// The checks: a client is told every setting when it connects; a band
// chosen moves the frequency and the mode to that band's; the other client
// is told of it too; a frequency tuned out of a band says so, and one tuned
// back in is where the band's memory was; a setting the radio will not take
// is answered with ERROR, to the one who asked alone; ? gets every setting
// again; a client that goes is forgotten and another can come.
//
// Exits 0 when every check passes.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "board.h"
#include "sdr.h"
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
		count = server_fill_polls(polls);
		if (poll(polls, count, ROUND_MS) > 0)
			server_serve(polls, count);
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

int main(void) {
	char heard[HEARD_MAX];
	char other_heard[HEARD_MAX];
	int one;
	int other;

	if (setup(&board_zbitx_1) < 0 || server_open(TEST_PORT) < 0) {
		printf("could not set up\n");
		return EXIT_FAILURE;
	}
	radio_open(server_tell);

	one = connect_a_client();
	other = connect_a_client();
	hear(one, heard);
	hear(other, other_heard);
	expect(has(heard, "FREQ 7088000\n") && has(heard, "BAND 40M\n")
		&& has(heard, "MODE LSB\n") && has(heard, "AUDIO 60\n"),
		"a client is told every setting when it connects");

	say(one, "BAND 20M\n");
	hear(one, heard);
	hear(other, other_heard);
	expect(has(heard, "BAND 20M\n") && has(heard, "FREQ 14200000\n")
		&& has(heard, "MODE USB\n"),
		"BAND 20M goes to 20 m, at its frequency and in its mode");
	expect(has(other_heard, "FREQ 14200000\n"),
		"and the other client is told of it too");

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
	expect(has(other_heard, "AUDIO 60\n") && !has(heard, "AUDIO 60\n"),
		"? gets every setting again, to the one who asked");

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
