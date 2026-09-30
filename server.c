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
// A line is a name, a space, and a value, ending in a newline. The names are
// those of the radio's controls, in control.c's table: FREQ, BAND, MODE and
// AUDIO of the radio's own, and those a modem adds, such as WPM.
//
// The client sets:
//
//     FREQ 7074000        tune to 7074 kHz
//     BAND 20M            go to 20 m, where the operator last was
//     MODE CW
//     AUDIO 80
//
// The radio tells. Whenever a control changes, whoever changed it, the radio
// sends it to every client but the one that changed it:
//
//     FREQ 7074000
//
// A client learns of others' changes only this way, so it never has to ask.
// It already knows its own, so it is not told them, unless the radio took a
// value other than the one asked for: then it is told what was taken. A
// change that moves other controls, a band moving the frequency and the
// mode, is told to everyone, the one who made it included.
//
// Two rules keep a change from going round in a loop, as it could in
// zbitxv2. A value a control already has changes nothing and is told to
// nobody. And a client never sends a change because it was told one: it
// shows it, and that is all.
//
// When a client connects, it is sent every control's value, one line each.
// A client that sends ? alone on a line is sent them all again.
//
// A client that sends schema is sent the table of controls, a line each and
// then a line of its own to end it, so that it can build itself without
// knowing beforehand what the radio has:
//
//     CONTROL RADIO FREQ number 500000 30000000 1
//     CONTROL RADIO MODE choice USB/LSB/CW/CWR/AM
//     CONTROL CW WPM number 5 50 1
//     CONTROL END
//
// A client that sends "spectrum on" is sent, ten times a second, what the
// receiver hears either side of the tuned frequency, for a graph and a
// waterfall, until it sends "spectrum off":
//
//     SPECTRUM 25031 1c1d1f2a...
//
// The first number is how many Hz the line spans, the tuned frequency in its
// middle. Then comes a strength for each point across that span, from the
// lowest frequency up, two hex digits each, in dB with 30 added so that the
// noise sits a little above the bottom. It is the line the front panel is
// sent, but at the full resolution of the receiver's bins, 534 points. A
// line is sent to a client only when all it was sent before has gone, so a
// client that cannot keep up misses lines rather than falling behind; the
// next line is as good as the one missed.
//
// A line the radio will not take is answered, to that client alone, with
// ERROR, the name, and what it would take:
//
//     ERROR FREQ a number from 500000 to 30000000
//
// Nothing is polled. A client that says nothing is sent only what changes,
// and the radio sends nothing when nothing changes. That matters on a radio
// whose receiver hears its own traffic.
//
// This is zbitxv2's remote protocol on the same port, with its names,
// except that zbitxv2's clients asked with ? many times a second and here
// the radio tells unasked.
//
// HOW IT IS SERVED
//
// There is no thread for it. The thread that owns the controls waits on the
// server's sockets along with everything else it waits on, with poll():
// server_get_pollfds() gives the sockets to wait on, and server_handle_events() deals
// with whatever they have. A line is acted on when it is whole.
//
// Nothing here waits. Sockets are non-blocking. What is to be sent to a
// client that cannot take it yet is kept; a client that falls a whole
// buffer behind is taken to be gone and is disconnected, so that one slow
// client never holds up the radio.
//
// At most SERVER_CLIENTS_MAX clients at once; one more is turned away.
//
// server_broadcast() may be called before server_open(), since the radio says
// what its settings are as it opens, and it then does nothing. Before the
// server is open its list of clients holds nothing to be trusted.

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "control.h"
#include "server.h"

#define LINE_MAX_LENGTH 128
#define OUT_MAX 4096
#define LISTEN_QUEUE 4
#define NOBODY (-1)
#define WHY_MAX CONTROL_WHY_MAX
#define LABEL_ROOM 16
#define SPECTRUM_LINE_MAX (2 * SERVER_SPECTRUM_POINTS_MAX + 32)
#define HEX_DIGITS "0123456789abcdef"

struct client {
	int fd;
	char in[LINE_MAX_LENGTH];
	int in_count;
	char out[OUT_MAX];
	int out_count;
	int wants_spectrum;
};

static int listener = NOBODY;
static struct client clients[SERVER_CLIENTS_MAX];

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

static void client_close(struct client *c) {
	close(c->fd);
	c->fd = NOBODY;
	c->in_count = 0;
	c->out_count = 0;
	c->wants_spectrum = 0;
}

void server_close(void) {
	int i;

	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (clients[i].fd != NOBODY)
			client_close(&clients[i]);
	}
	if (listener != NOBODY)
		close(listener);
	listener = NOBODY;
}

static void client_flush(struct client *c) {
	ssize_t sent;

	if (c->out_count == 0)
		return;
	sent = send(c->fd, c->out, c->out_count, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (sent < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			client_close(c);
		return;
	}
	memmove(c->out, c->out + sent, c->out_count - sent);
	c->out_count -= sent;
}

static void client_queue_line(struct client *c, const char *line) {
	int length = strlen(line);

	if (c->fd == NOBODY)
		return;
	if (c->out_count + length > OUT_MAX) {
		client_close(c);
		return;
	}
	memcpy(c->out + c->out_count, line, length);
	c->out_count += length;
	client_flush(c);
}

void server_broadcast(const struct control *c, int origin) {
	char line[LINE_MAX_LENGTH];
	int i;

	if (listener == NOBODY)
		return;
	snprintf(line, sizeof(line), "%s %s\n", c->name, c->value);
	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (i != origin)
			client_queue_line(&clients[i], line);
	}
}

int server_wants_spectrum(void) {
	int i;

	if (listener == NOBODY)
		return 0;
	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (clients[i].fd != NOBODY && clients[i].wants_spectrum)
			return 1;
	}
	return 0;
}

void server_send_spectrum(int span_hz, const uint8_t *points, int count) {
	char line[SPECTRUM_LINE_MAX];
	int length;
	int i;

	if (listener == NOBODY)
		return;
	if (count > SERVER_SPECTRUM_POINTS_MAX)
		count = SERVER_SPECTRUM_POINTS_MAX;
	length = snprintf(line, sizeof(line), "SPECTRUM %d ", span_hz);
	for (i = 0; i < count; i++) {
		line[length++] = HEX_DIGITS[points[i] >> 4];
		line[length++] = HEX_DIGITS[points[i] & 0xf];
	}
	line[length++] = '\n';
	line[length] = 0;
	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (clients[i].fd == NOBODY || !clients[i].wants_spectrum)
			continue;
		if (clients[i].out_count > 0)
			continue;
		client_queue_line(&clients[i], line);
	}
}

static void send_all_settings(struct client *client) {
	char line[LINE_MAX_LENGTH];
	const struct control *c;
	int i;

	for (i = 0; i < control_count(); i++) {
		c = control_at(i);
		snprintf(line, sizeof(line), "%s %s\n", c->name, c->value);
		client_queue_line(client, line);
	}
}

static void send_schema(struct client *client) {
	char line[LINE_MAX_LENGTH];
	const struct control *c;
	int i;

	for (i = 0; i < control_count(); i++) {
		c = control_at(i);
		if (c->kind == CONTROL_NUMBER)
			snprintf(line, sizeof(line), "CONTROL %s %s number %d %d %d\n",
				c->group, c->name, c->min, c->max, c->step);
		else if (c->kind == CONTROL_CHOICE)
			snprintf(line, sizeof(line), "CONTROL %s %s choice %s\n",
				c->group, c->name, c->choices);
		else
			snprintf(line, sizeof(line), "CONTROL %s %s text\n", c->group, c->name);
		client_queue_line(client, line);
	}
	client_queue_line(client, "CONTROL END\n");
}

static void accept_client(void) {
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
	clients[i].wants_spectrum = 0;
	send_all_settings(&clients[i]);
}

static void handle_line(struct client *c, char *line) {
	char why[WHY_MAX];
	char answer[LINE_MAX_LENGTH + WHY_MAX + LABEL_ROOM];
	char *value;

	if (!strcmp(line, "?")) {
		send_all_settings(c);
		return;
	}
	if (!strcmp(line, "schema")) {
		send_schema(c);
		return;
	}
	if (!strcmp(line, "spectrum on")) {
		c->wants_spectrum = 1;
		return;
	}
	if (!strcmp(line, "spectrum off")) {
		c->wants_spectrum = 0;
		return;
	}
	value = strchr(line, ' ');
	if (value == NULL) {
		snprintf(answer, sizeof(answer), "ERROR %s a label and a value\n", line);
		client_queue_line(c, answer);
		return;
	}
	*value = 0;
	value++;
	if (control_set(line, value, (int)(c - clients), why, sizeof(why)) < 0) {
		snprintf(answer, sizeof(answer), "ERROR %s %s\n", line, why);
		client_queue_line(c, answer);
	}
}

static void read_client(struct client *c) {
	char got[LINE_MAX_LENGTH];
	ssize_t count;
	ssize_t i;

	count = recv(c->fd, got, sizeof(got), MSG_DONTWAIT);
	if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
		client_close(c);
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
			handle_line(c, c->in);
	}
}

int server_get_pollfds(struct pollfd *polls) {
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

static struct client *find_client(int fd) {
	int i;

	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (clients[i].fd == fd)
			return &clients[i];
	}
	return NULL;
}

void server_handle_events(const struct pollfd *polls, int count) {
	struct client *c;
	int i;

	for (i = 0; i < count; i++) {
		if (polls[i].revents == 0)
			continue;
		if (polls[i].fd == listener) {
			accept_client();
			continue;
		}
		c = find_client(polls[i].fd);
		if (c == NULL)
			continue;
		if (polls[i].revents & POLLOUT)
			client_flush(c);
		if (c->fd != NOBODY && (polls[i].revents & (POLLIN | POLLHUP | POLLERR)))
			read_client(c);
	}
}
