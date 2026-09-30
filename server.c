// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// server.c — the radio's control protocol, served over TCP.
//
// A client, the desktop GUI, a program on another computer, one day the
// front panel, connects to the radio on port 8081 and they talk in lines of
// text. It can be tried by hand:
//
//     nc 192.168.1.76 8081
//
// THE PROTOCOL
//
// A line is a label, a space, and a value, ending in a newline. The labels
// are the settings radio.c keeps: FREQ, BAND, MODE and AUDIO.
//
// The client sets:
//
//     FREQ 7074000        tune to 7074 kHz
//     BAND 20M            go to 20 m, where the operator last was
//     MODE CW
//     AUDIO 80
//
// The radio tells. Whenever a setting changes, whoever changed it, the radio
// sends it to every client, the one that asked included:
//
//     FREQ 7074000
//
// A client learns of a change only this way, so it never has to ask again.
// When a client connects, it is sent every setting, one line each. A
// client that sends ? alone on a line is sent them all again.
//
// A line the radio will not take is answered, to that client alone, with
// ERROR, the label, and what it would take:
//
//     ERROR FREQ a frequency from 500000 to 30000000 Hz
//
// Nothing is polled. A client that says nothing is sent only what changes,
// and the radio sends nothing when nothing changes. That matters on a radio
// whose receiver hears its own traffic.
//
// This is zbitxv2's remote protocol on the same port, with its labels,
// except that zbitxv2's clients asked with ? many times a second and here
// the radio tells unasked. Settings will be added to it as the radio gains
// them; a client passes over a label it does not know.
//
// HOW IT IS SERVED
//
// There is no thread for it. The thread that owns the controls waits on the
// server's sockets along with everything else it waits on, with poll():
// server_fill_polls() gives the sockets to wait on, and server_serve() deals
// with whatever they have. A line is acted on when it is whole.
//
// Nothing here waits. Sockets are non-blocking. What is to be sent to a
// client that cannot take it yet is kept; a client that falls a whole
// buffer behind is taken to be gone and is disconnected, so that one slow
// client never holds up the radio.
//
// At most SERVER_CLIENTS_MAX clients at once; one more is turned away.
//
// server_tell() may be called before server_open(), since the radio says
// what its settings are as it opens, and it then does nothing. Before the
// server is open its list of clients holds nothing to be trusted.

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "radio.h"
#include "server.h"

#define LINE_MAX_LENGTH 128
#define OUT_MAX 4096
#define LISTEN_QUEUE 4
#define NOBODY (-1)
#define WHY_MAX 80
#define LABEL_ROOM 16

struct client {
	int fd;
	char in[LINE_MAX_LENGTH];
	int in_count;
	char out[OUT_MAX];
	int out_count;
};

static int listener = NOBODY;
static struct client clients[SERVER_CLIENTS_MAX];
static struct client *being_told;

static int make_non_blocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int server_open(int port) {
	struct sockaddr_in where;
	int yes = 1;
	int i;

	for (i = 0; i < SERVER_CLIENTS_MAX; i++)
		clients[i].fd = NOBODY;
	listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0)
		return -1;
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	memset(&where, 0, sizeof(where));
	where.sin_family = AF_INET;
	where.sin_addr.s_addr = htonl(INADDR_ANY);
	where.sin_port = htons(port);
	if (bind(listener, (struct sockaddr *)&where, sizeof(where)) < 0
			|| listen(listener, LISTEN_QUEUE) < 0
			|| make_non_blocking(listener) < 0) {
		close(listener);
		listener = NOBODY;
		return -1;
	}
	return 0;
}

static void drop(struct client *c) {
	close(c->fd);
	c->fd = NOBODY;
	c->in_count = 0;
	c->out_count = 0;
}

void server_close(void) {
	int i;

	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (clients[i].fd != NOBODY)
			drop(&clients[i]);
	}
	if (listener != NOBODY)
		close(listener);
	listener = NOBODY;
}

static void send_what_is_kept(struct client *c) {
	ssize_t sent;

	if (c->out_count == 0)
		return;
	sent = send(c->fd, c->out, c->out_count, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (sent < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			drop(c);
		return;
	}
	memmove(c->out, c->out + sent, c->out_count - sent);
	c->out_count -= sent;
}

static void say_to(struct client *c, const char *line) {
	int length = strlen(line);

	if (c->fd == NOBODY)
		return;
	if (c->out_count + length > OUT_MAX) {
		drop(c);
		return;
	}
	memcpy(c->out + c->out_count, line, length);
	c->out_count += length;
	send_what_is_kept(c);
}

void server_tell(const char *label, const char *value) {
	char line[LINE_MAX_LENGTH];
	int i;

	if (listener == NOBODY)
		return;
	snprintf(line, sizeof(line), "%s %s\n", label, value);
	for (i = 0; i < SERVER_CLIENTS_MAX; i++)
		say_to(&clients[i], line);
}

static void tell_one(const char *label, const char *value) {
	char line[LINE_MAX_LENGTH];

	snprintf(line, sizeof(line), "%s %s\n", label, value);
	say_to(being_told, line);
}

static void tell_everything(struct client *c) {
	being_told = c;
	radio_tell_all(tell_one);
	being_told = NULL;
}

static void take_in(void) {
	int fd;
	int i;

	fd = accept(listener, NULL, NULL);
	if (fd < 0)
		return;
	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (clients[i].fd == NOBODY)
			break;
	}
	if (i == SERVER_CLIENTS_MAX || make_non_blocking(fd) < 0) {
		close(fd);
		return;
	}
	clients[i].fd = fd;
	clients[i].in_count = 0;
	clients[i].out_count = 0;
	tell_everything(&clients[i]);
}

static void obey(struct client *c, char *line) {
	char why[WHY_MAX];
	char answer[LINE_MAX_LENGTH + WHY_MAX + LABEL_ROOM];
	char *value;

	if (!strcmp(line, "?")) {
		tell_everything(c);
		return;
	}
	value = strchr(line, ' ');
	if (value == NULL) {
		snprintf(answer, sizeof(answer), "ERROR %s a label and a value\n", line);
		say_to(c, answer);
		return;
	}
	*value = 0;
	value++;
	if (radio_set(line, value, why, sizeof(why)) < 0) {
		snprintf(answer, sizeof(answer), "ERROR %s %s\n", line, why);
		say_to(c, answer);
	}
}

static void hear(struct client *c) {
	char got[LINE_MAX_LENGTH];
	ssize_t count;
	ssize_t i;

	count = recv(c->fd, got, sizeof(got), MSG_DONTWAIT);
	if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
		drop(c);
		return;
	}
	for (i = 0; i < count && c->fd != NOBODY; i++) {
		if (got[i] == '\r')
			continue;
		if (got[i] != '\n') {
			if (c->in_count < LINE_MAX_LENGTH - 1)
				c->in[c->in_count++] = got[i];
			continue;
		}
		c->in[c->in_count] = 0;
		c->in_count = 0;
		if (c->in[0] != 0)
			obey(c, c->in);
	}
}

int server_fill_polls(struct pollfd *polls) {
	int count = 0;
	int i;

	if (listener == NOBODY)
		return 0;
	polls[count].fd = listener;
	polls[count].events = POLLIN;
	count++;
	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (clients[i].fd == NOBODY)
			continue;
		polls[count].fd = clients[i].fd;
		polls[count].events = POLLIN;
		if (clients[i].out_count > 0)
			polls[count].events |= POLLOUT;
		count++;
	}
	return count;
}

static struct client *client_of(int fd) {
	int i;

	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (clients[i].fd == fd)
			return &clients[i];
	}
	return NULL;
}

void server_serve(const struct pollfd *polls, int count) {
	struct client *c;
	int i;

	for (i = 0; i < count; i++) {
		if (polls[i].revents == 0)
			continue;
		if (polls[i].fd == listener) {
			take_in();
			continue;
		}
		c = client_of(polls[i].fd);
		if (c == NULL)
			continue;
		if (polls[i].revents & POLLOUT)
			send_what_is_kept(c);
		if (c->fd != NOBODY && (polls[i].revents & (POLLIN | POLLHUP | POLLERR)))
			hear(c);
	}
}
