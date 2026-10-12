// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// zbitx_remote.c — the radio's way out to my.sbitx.net, so that its owner
// can reach it from anywhere.
//
//     zbitx_remote [--server my.sbitx.net] [--port 8081]
//                  [--web https://my.sbitx.net] [--radio-port 8081]
//                  [--key data/remote.key] [--name sbitx]
//
// PROTOCOL.md, at the top of the repository, says what is spoken and why;
// accounts/remote.py is the other end.
//
// WHY IT IS A PROGRAM OF ITS OWN
//
// The radio program, sdr, is a server: clients connect to it. A radio at
// home is behind a router and cannot be connected to from outside, so
// something on the radio has to connect out. The author's choice was a
// small program beside sdr and not more in sdr itself. To sdr this is one
// more client on its own machine; to my.sbitx.net it is the radio. If it
// fails, or the internet does, the radio program is not touched, and a
// radio whose owner wants nothing of this does not run it.
//
// IT IS THE OWNER'S TO SWITCH ON AND OFF
//
// The author's rule. The radio has a control, REMOTE, OFF or ON, which is
// in the settings of every client and is OFF until the owner turns it on.
// This program watches it, as any client is told of a control. While it is
// OFF this program holds no connection to anything outside the radio. What
// it has to tell the owner it says in another control, REMOTE_STATUS: OFF,
// ASKING, CODE BCDF-GHJK, CONNECTING, ONLINE VU2ESE, and when things go
// wrong, CODE RAN OUT, KEY REFUSED, NO ANSWER FROM SERVER. The radio
// program keeps the two controls and does nothing with them.
//
// WHAT IT DOES WHILE REMOTE IS ON
//
// If the radio has no key, it gets one: linking.c says how, and the code
// to be typed at my.sbitx.net is shown in REMOTE_STATUS.
//
// Then it connects to my.sbitx.net, answers the challenge with the key,
// says "radio", and keeps that connection. Nothing goes down it but PING a
// minute from here and PONG back, the author's other rule being that
// nothing is sent that nobody asked for. If it is lost it is made again,
// after five seconds, then ten, up to a minute.
//
// When the service sends "SESSION <token>", somebody the service has
// vouched for wants the radio. This program then makes two connections for
// that one visitor: another to my.sbitx.net, where it answers a challenge
// again and says "session <token>", and one to sdr on 127.0.0.1. From then
// on it passes every byte from the one to the other, both ways, until
// either closes. The visitor is a client of the radio like any other, and
// is sent the spectrum only if it asks for it. There may be SESSIONS_MAX
// visitors at once.
//
// A key the service refuses is not tried again. Turning REMOTE off and on
// drops it and links the radio afresh.
//
// HOW IT RUNS
//
// One thread and one loop around poll(). The connections that carry a
// visitor's bytes never wait: each has a buffer for each way, and a side
// that is slow is simply not read from until it has taken what it was
// given. The connecting and the few lines of a challenge are done waiting,
// four seconds at most for each step, during which a visitor already on the
// radio is held up; that is the price of keeping this small.
//
// The key is never printed. What this program prints is who came and went.

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "linking.h"
#include "sha256.h"

#define DEFAULT_SERVER "my.sbitx.net"
#define DEFAULT_PORT "8081"
#define DEFAULT_WEB "https://my.sbitx.net"
#define DEFAULT_RADIO_PORT "8081"
#define DEFAULT_KEY_FILE "data/remote.key"
#define LOCAL_HOST "127.0.0.1"
#define LINE_ROOM 1024
#define TEXT_ROOM 256
#define NONCE_BYTES 16
#define NONCE_HEX_ROOM (2 * NONCE_BYTES + 1)
#define CALLSIGN_ROOM 20
#define CHALLENGE_ROOM 72
#define STEP_WAIT_MS 4000
#define LOOP_WAIT_MS 200
#define RADIO_RETRY_MS 2000
#define PING_EVERY_MS 60000
#define SILENCE_MOST_MS 150000
#define RETRY_FIRST_MS 5000
#define RETRY_MOST_MS 60000
#define SESSIONS_MAX 4
#define PIPE_ROOM 16384
#define MS_PER_SECOND 1000
#define US_PER_MS 1000
#define NS_PER_MS 1000000
#define NOT_OPEN (-1)
#define AUTH_OK 0
#define AUTH_REFUSED 1
#define AUTH_NO_ANSWER 2

enum state {
	OFF,
	LINKING,
	TO_CONNECT,
	ONLINE,
	WAITING_TO_RETRY,
	STOPPED_UNTIL_OFF,
};

struct lines {
	char bytes[LINE_ROOM];
	int used;
};

struct waiting {
	char bytes[PIPE_ROOM];
	int used;
};

struct session {
	int service_fd;
	int radio_fd;
	struct waiting for_radio;
	struct waiting for_service;
};

static const char *server = DEFAULT_SERVER;
static const char *port = DEFAULT_PORT;
static const char *radio_port = DEFAULT_RADIO_PORT;

static enum state state = OFF;
static int is_wanted;
static int is_key_refused;
static int radio_fd = NOT_OPEN;
static struct lines radio_lines;
static long radio_retry_ms;
static int line_fd = NOT_OPEN;
static struct lines line_lines;
static long next_ping_ms;
static long last_heard_ms;
static long retry_ms;
static long retry_after_ms = RETRY_FIRST_MS;
static char status_said[LINKING_STATUS_ROOM];
static struct session sessions[SESSIONS_MAX];

static long now_ms(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * MS_PER_SECOND + now.tv_nsec / NS_PER_MS;
}

static void close_fd(int *fd) {
	if (*fd != NOT_OPEN)
		close(*fd);
	*fd = NOT_OPEN;
}

/* connections that are waited on: connecting, and a challenge's few lines */

// a TCP connection, waited for no longer than STEP_WAIT_MS, and after that
// no read or write on it waits longer either
static int dial(const char *host, const char *service) {
	struct addrinfo hints;
	struct addrinfo *found;
	struct timeval wait;
	struct pollfd connected;
	socklen_t size = sizeof(int);
	int error = 0;
	int one = 1;
	int fd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, service, &hints, &found) != 0)
		return NOT_OPEN;
	fd = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
	if (fd < 0) {
		freeaddrinfo(found);
		return NOT_OPEN;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	if (connect(fd, found->ai_addr, found->ai_addrlen) < 0 && errno != EINPROGRESS) {
		freeaddrinfo(found);
		close(fd);
		return NOT_OPEN;
	}
	freeaddrinfo(found);
	connected.fd = fd;
	connected.events = POLLOUT;
	if (poll(&connected, 1, STEP_WAIT_MS) <= 0
			|| getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error != 0) {
		close(fd);
		return NOT_OPEN;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
	wait.tv_sec = STEP_WAIT_MS / MS_PER_SECOND;
	wait.tv_usec = (STEP_WAIT_MS % MS_PER_SECOND) * US_PER_MS;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	return fd;
}

static int send_text(int fd, const char *text) {
	size_t length = strlen(text);
	size_t sent = 0;
	ssize_t wrote;

	while (sent < length) {
		wrote = send(fd, text + sent, length - sent, MSG_NOSIGNAL);
		if (wrote <= 0)
			return -1;
		sent += (size_t)wrote;
	}
	return 0;
}

// a line, without its end, read a byte at a time so that nothing after it
// is taken; -1 if the connection closes or nothing comes in time
static int read_line_waiting(int fd, char *line, int room) {
	int used = 0;
	char byte;

	while (used < room - 1) {
		if (recv(fd, &byte, 1, 0) != 1)
			return -1;
		if (byte == '\n') {
			line[used] = 0;
			return 0;
		}
		if (byte != '\r')
			line[used++] = byte;
	}
	return -1;
}

static int is_same(const char *a, const char *b) {
	size_t length = strlen(a);
	unsigned char differs = 0;
	size_t i;

	if (strlen(b) != length)
		return 0;
	for (i = 0; i < length; i++)
		differs |= (unsigned char)(a[i] ^ b[i]);
	return differs == 0;
}

// the challenge is answered with the radio's key, and the service's own
// proof that it has the key is checked; PROTOCOL.md has the handshake
static int authenticate(int fd, char *callsign) {
	char key_id[LINKING_KEY_ID_ROOM];
	char key[LINKING_KEY_ROOM];
	uint8_t random_bytes[NONCE_BYTES];
	char nonce[NONCE_HEX_ROOM];
	char line[LINE_ROOM];
	char challenge[CHALLENGE_ROOM];
	char text[TEXT_ROOM];
	char proof[SHA256_HEX_ROOM];
	char theirs[SHA256_HEX_ROOM + 16];
	int i;

	callsign[0] = 0;
	if (!linking_read_key(key_id, key))
		return AUTH_REFUSED;
	if (read_line_waiting(fd, line, sizeof(line)) < 0)
		return AUTH_NO_ANSWER;
	if (sscanf(line, "CHALLENGE %64s", challenge) != 1)
		return AUTH_NO_ANSWER;
	if (getrandom(random_bytes, sizeof(random_bytes), 0) != sizeof(random_bytes))
		return AUTH_NO_ANSWER;
	for (i = 0; i < NONCE_BYTES; i++)
		snprintf(nonce + 2 * i, 3, "%02x", random_bytes[i]);
	snprintf(text, sizeof(text), "zbitx-auth-1 client %s %s", challenge, nonce);
	hmac_sha256_hex(key, text, proof);
	snprintf(line, sizeof(line), "AUTH %s %s %s\n", key_id, nonce, proof);
	if (send_text(fd, line) < 0)
		return AUTH_NO_ANSWER;
	if (read_line_waiting(fd, line, sizeof(line)) < 0)
		return AUTH_NO_ANSWER;
	if (!strncmp(line, "ERROR AUTH", strlen("ERROR AUTH")))
		return AUTH_REFUSED;
	if (sscanf(line, "WELCOME %19s %80s", callsign, theirs) != 2)
		return AUTH_NO_ANSWER;
	snprintf(text, sizeof(text), "zbitx-auth-1 server %s %s", challenge, nonce);
	hmac_sha256_hex(key, text, proof);
	if (!is_same(proof, theirs))
		return AUTH_NO_ANSWER;
	return AUTH_OK;
}

/* lines from a connection that is not waited on */

// the next whole line a connection has, without its end: 1 with a line, 0
// with none yet, -1 when the connection has closed
static int take_line(struct lines *l, int fd, char *line, int room) {
	char *end;
	ssize_t got;
	int length;

	end = memchr(l->bytes, '\n', l->used);
	if (end == NULL) {
		if (l->used >= LINE_ROOM)
			l->used = 0;
		got = recv(fd, l->bytes + l->used, LINE_ROOM - l->used, MSG_DONTWAIT);
		if (got == 0)
			return -1;
		if (got < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				return 0;
			return -1;
		}
		l->used += (int)got;
		end = memchr(l->bytes, '\n', l->used);
		if (end == NULL)
			return 0;
	}
	length = (int)(end - l->bytes);
	snprintf(line, room, "%.*s", length, l->bytes);
	line[strcspn(line, "\r")] = 0;
	l->used -= length + 1;
	memmove(l->bytes, end + 1, l->used);
	return 1;
}

/* the radio program, of which this is a client */

static void say_status(const char *text) {
	char line[TEXT_ROOM];

	if (!strcmp(text, status_said))
		return;
	snprintf(status_said, sizeof(status_said), "%s", text);
	printf("remote: %s\n", text);
	fflush(stdout);
	if (radio_fd == NOT_OPEN)
		return;
	snprintf(line, sizeof(line), "REMOTE_STATUS %s\n", text);
	if (send_text(radio_fd, line) < 0)
		close_fd(&radio_fd);
}

static void radio_poll(long now) {
	char line[LINE_ROOM];
	char said[LINKING_STATUS_ROOM];
	int got;

	if (radio_fd == NOT_OPEN) {
		if (now < radio_retry_ms)
			return;
		radio_retry_ms = now + RADIO_RETRY_MS;
		radio_fd = dial(LOCAL_HOST, radio_port);
		if (radio_fd == NOT_OPEN)
			return;
		radio_lines.used = 0;
		// what there is to say is said afresh to a radio program just found
		snprintf(said, sizeof(said), "%s", status_said);
		status_said[0] = 0;
		say_status(said);
	}
	got = take_line(&radio_lines, radio_fd, line, sizeof(line));
	while (got > 0) {
		if (!strcmp(line, "REMOTE ON"))
			is_wanted = 1;
		if (!strcmp(line, "REMOTE OFF"))
			is_wanted = 0;
		got = take_line(&radio_lines, radio_fd, line, sizeof(line));
	}
	// with no radio program there is nothing to offer anyone
	if (got < 0) {
		close_fd(&radio_fd);
		is_wanted = 0;
	}
}

/* a visitor's two connections */

static void session_close(struct session *s) {
	close_fd(&s->service_fd);
	close_fd(&s->radio_fd);
	s->for_radio.used = 0;
	s->for_service.used = 0;
}

static void sessions_close(void) {
	int i;

	for (i = 0; i < SESSIONS_MAX; i++)
		session_close(&sessions[i]);
}

static void session_open(const char *token) {
	struct session *s = NULL;
	char callsign[CALLSIGN_ROOM];
	char line[LINE_ROOM];
	int i;

	for (i = 0; i < SESSIONS_MAX; i++) {
		if (sessions[i].service_fd == NOT_OPEN)
			s = &sessions[i];
	}
	if (s == NULL)
		return;
	s->service_fd = dial(server, port);
	if (s->service_fd == NOT_OPEN)
		return;
	snprintf(line, sizeof(line), "session %.64s\n", token);
	if (authenticate(s->service_fd, callsign) != AUTH_OK || send_text(s->service_fd, line) < 0
			|| read_line_waiting(s->service_fd, line, sizeof(line)) < 0
			|| strcmp(line, "OK")) {
		session_close(s);
		return;
	}
	s->radio_fd = dial(LOCAL_HOST, radio_port);
	if (s->radio_fd == NOT_OPEN) {
		session_close(s);
		return;
	}
	fcntl(s->service_fd, F_SETFL, fcntl(s->service_fd, F_GETFL, 0) | O_NONBLOCK);
	fcntl(s->radio_fd, F_SETFL, fcntl(s->radio_fd, F_GETFL, 0) | O_NONBLOCK);
	printf("remote: a visitor is on the radio\n");
	fflush(stdout);
}

// what one side has is taken into the buffer for the other, if there is
// room, and what is in a buffer is given to its side, as much as it takes;
// says whether the connection is still good
static int pass_on(int from_fd, int to_fd, struct waiting *w, short from_events,
		short to_events) {
	ssize_t count;

	if ((from_events & (POLLIN | POLLHUP | POLLERR)) && w->used < PIPE_ROOM) {
		count = recv(from_fd, w->bytes + w->used, PIPE_ROOM - w->used, 0);
		if (count == 0)
			return 0;
		if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			return 0;
		if (count > 0)
			w->used += (int)count;
	}
	if (w->used > 0 && ((to_events & POLLOUT) || (from_events & POLLIN))) {
		count = send(to_fd, w->bytes, w->used, MSG_NOSIGNAL);
		if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			return 0;
		if (count > 0) {
			w->used -= (int)count;
			memmove(w->bytes, w->bytes + count, w->used);
		}
	}
	return 1;
}

/* the connection kept to my.sbitx.net */

static void line_close(long now) {
	close_fd(&line_fd);
	state = WAITING_TO_RETRY;
	retry_ms = now + retry_after_ms;
	retry_after_ms *= 2;
	if (retry_after_ms > RETRY_MOST_MS)
		retry_after_ms = RETRY_MOST_MS;
}

static void line_open(long now) {
	char callsign[CALLSIGN_ROOM];
	char line[LINE_ROOM];
	char said[LINKING_STATUS_ROOM];
	int result = AUTH_NO_ANSWER;

	say_status("CONNECTING");
	line_fd = dial(server, port);
	if (line_fd != NOT_OPEN)
		result = authenticate(line_fd, callsign);
	if (result == AUTH_REFUSED) {
		close_fd(&line_fd);
		is_key_refused = 1;
		state = STOPPED_UNTIL_OFF;
		say_status("KEY REFUSED");
		return;
	}
	if (result != AUTH_OK || send_text(line_fd, "radio\n") < 0
			|| read_line_waiting(line_fd, line, sizeof(line)) < 0 || strcmp(line, "OK")) {
		line_close(now);
		say_status("NO ANSWER FROM SERVER");
		return;
	}
	line_lines.used = 0;
	next_ping_ms = now + PING_EVERY_MS;
	last_heard_ms = now;
	retry_after_ms = RETRY_FIRST_MS;
	state = ONLINE;
	snprintf(said, sizeof(said), "ONLINE %.16s", callsign);
	say_status(said);
}

static void line_poll(long now) {
	char line[LINE_ROOM];
	char token[LINE_ROOM];
	int got = take_line(&line_lines, line_fd, line, sizeof(line));

	while (got > 0) {
		last_heard_ms = now;
		if (sscanf(line, "SESSION %64s", token) == 1)
			session_open(token);
		got = take_line(&line_lines, line_fd, line, sizeof(line));
	}
	if (got == 0 && now >= next_ping_ms) {
		next_ping_ms = now + PING_EVERY_MS;
		if (send_text(line_fd, "PING\n") < 0)
			got = -1;
	}
	if (got < 0 || now - last_heard_ms > SILENCE_MOST_MS) {
		line_close(now);
		say_status("CONNECTING");
	}
}

/* what the owner wants, carried out */

static void switch_off(void) {
	linking_stop();
	close_fd(&line_fd);
	sessions_close();
	state = OFF;
	retry_after_ms = RETRY_FIRST_MS;
	say_status("OFF");
}

static void follow_the_owner(long now) {
	char key_id[LINKING_KEY_ID_ROOM];
	char key[LINKING_KEY_ROOM];
	char said[LINKING_STATUS_ROOM];
	enum linking_result linked;

	if (!is_wanted) {
		if (state != OFF)
			switch_off();
		return;
	}
	if (state == OFF) {
		// a key the service refused is dropped, and the radio linked afresh
		if (is_key_refused)
			linking_forget_key();
		is_key_refused = 0;
		state = TO_CONNECT;
		if (!linking_read_key(key_id, key)) {
			linking_begin();
			state = LINKING;
		}
	}
	if (state == LINKING) {
		linked = linking_poll(now, said);
		if (said[0] != 0 && linked != LINKING_LINKED)
			say_status(said);
		if (linked == LINKING_LINKED)
			state = TO_CONNECT;
		if (linked == LINKING_FAILED)
			state = STOPPED_UNTIL_OFF;
	}
	if (state == WAITING_TO_RETRY && now >= retry_ms)
		state = TO_CONNECT;
	if (state == TO_CONNECT)
		line_open(now);
	else if (state == ONLINE)
		line_poll(now);
}

static void wait_and_pass_on(void) {
	struct pollfd polls[2 + 2 * SESSIONS_MAX];
	struct session *s;
	int count = 0;
	int first;
	int i;

	if (radio_fd != NOT_OPEN) {
		polls[count].fd = radio_fd;
		polls[count].events = POLLIN;
		polls[count++].revents = 0;
	}
	if (line_fd != NOT_OPEN) {
		polls[count].fd = line_fd;
		polls[count].events = POLLIN;
		polls[count++].revents = 0;
	}
	first = count;
	for (i = 0; i < SESSIONS_MAX; i++) {
		s = &sessions[i];
		polls[count].fd = s->service_fd;
		polls[count].events = 0;
		polls[count].revents = 0;
		polls[count + 1].fd = s->radio_fd;
		polls[count + 1].events = 0;
		polls[count + 1].revents = 0;
		if (s->service_fd != NOT_OPEN) {
			if (s->for_radio.used < PIPE_ROOM)
				polls[count].events |= POLLIN;
			if (s->for_service.used > 0)
				polls[count].events |= POLLOUT;
			if (s->for_service.used < PIPE_ROOM)
				polls[count + 1].events |= POLLIN;
			if (s->for_radio.used > 0)
				polls[count + 1].events |= POLLOUT;
		}
		count += 2;
	}
	poll(polls, count, LOOP_WAIT_MS);
	for (i = 0; i < SESSIONS_MAX; i++) {
		s = &sessions[i];
		if (s->service_fd == NOT_OPEN)
			continue;
		if (!pass_on(s->service_fd, s->radio_fd, &s->for_radio, polls[first + 2 * i].revents,
				polls[first + 2 * i + 1].revents)
				|| !pass_on(s->radio_fd, s->service_fd, &s->for_service,
				polls[first + 2 * i + 1].revents, polls[first + 2 * i].revents)) {
			session_close(s);
			printf("remote: a visitor has left\n");
			fflush(stdout);
		}
	}
}

int main(int argc, char **argv) {
	const char *web = DEFAULT_WEB;
	const char *key_file = DEFAULT_KEY_FILE;
	char name[TEXT_ROOM] = "radio";
	int i;

	gethostname(name, sizeof(name) - 1);
	for (i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--server"))
			server = argv[i + 1];
		else if (!strcmp(argv[i], "--port"))
			port = argv[i + 1];
		else if (!strcmp(argv[i], "--web"))
			web = argv[i + 1];
		else if (!strcmp(argv[i], "--radio-port"))
			radio_port = argv[i + 1];
		else if (!strcmp(argv[i], "--key"))
			key_file = argv[i + 1];
		else if (!strcmp(argv[i], "--name"))
			snprintf(name, sizeof(name), "%s", argv[i + 1]);
		else
			i = argc;
	}
	if (i != argc) {
		printf("zbitx_remote [--server host] [--port n] [--web url] [--radio-port n] "
			"[--key file] [--name text]\n");
		return EXIT_FAILURE;
	}
	signal(SIGPIPE, SIG_IGN);
	for (i = 0; i < SESSIONS_MAX; i++) {
		sessions[i].service_fd = NOT_OPEN;
		sessions[i].radio_fd = NOT_OPEN;
	}
	linking_open(web, key_file, name, "radio");
	snprintf(status_said, sizeof(status_said), "OFF");
	printf("remote: for %s port %s, the radio program on port %s\n", server, port, radio_port);
	fflush(stdout);
	while (1) {
		radio_poll(now_ms());
		follow_the_owner(now_ms());
		wait_and_pass_on();
	}
	return EXIT_SUCCESS;
}
