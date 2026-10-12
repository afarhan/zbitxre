// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// zbitx_remote.c — the radio's way out to my.sbitx.net, so that its owner
// can reach it from anywhere.
//
//     zbitx_remote [--server my.sbitx.net] [--port 8081] [--radio-port 8081]
//                  [--key data/remote.key] [--microphone-port 5006]
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
// it has to tell the owner it says in another control, REMOTE_STATUS. The
// radio program keeps the two controls and does nothing with them.
//
// THE COOKIE
//
// The radio proves who it is to my.sbitx.net with a secret, and the author
// settled how the two come to share it: the radio makes it, and the owner
// carries it to the website. A zbitx has no keyboard, and a secret that had
// to be typed into the radio, or into a file on it, would be a trial. So
// the first time REMOTE is turned on, this program makes a cookie of twelve
// letters, from the system's source of random numbers, and keeps it in a
// file only the radio's user can read, data/remote.key. While my.sbitx.net
// does not know the radio, REMOTE_STATUS shows the cookie, "COOKIE
// abcd-jklm-wxyz", and the owner, signed in at my.sbitx.net, adds a radio
// there, gives it a name and types the cookie. Within a quarter of a
// minute the radio is on line, and from then on the cookie is not shown:
// REMOTE_STATUS says "ONLINE vu2ese-home".
//
// The grid locator set on the radio, MYGRID, goes to the service too, on
// the connection that is kept, when it is made and whenever the grid is
// changed: the author asked that the grid reach the service along with the
// callsign.
//
// The radio does not know the name it was given on the website and does
// not need to. It says whose it is, by the callsign set on it, MYCALLSIGN,
// which this program is told as any client is; the website finds which of
// that owner's radios has this cookie, and says the name back. So the
// callsign has to be set before any of this, and REMOTE_STATUS says so if
// it is not.
//
// The cookie is never sent. It is the secret in the answer to the
// service's challenge, and first it is stretched, by PBKDF2 with the
// callsign as salt, so that twelve letters cannot be found by someone who
// overheard an answer and tries them all. The stretching takes a moment
// and is done once for each callsign.
//
// Removing the radio on the website makes the cookie show again; deleting
// data/remote.key makes this program think up another.
//
// WHAT IT DOES WHILE REMOTE IS ON
//
// It connects to my.sbitx.net, answers the challenge, says "radio", and
// keeps that connection. Nothing goes down it but PING a minute from here
// and PONG back, the author's other rule being that nothing is sent that
// nobody asked for. If it is lost it is made again, after five seconds,
// then ten, up to a minute; while the website does not know the radio, it
// is tried every quarter of a minute.
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
// THE RADIO'S SOUND
//
// The radio program sends what its speaker plays out on the radio itself,
// as RTP to a multicast group; rtp_audio.c says how. While somebody afar is
// listening, and only then, the service says so on the connection that is
// kept, "AUDIO ON <the stream's id> <a nonce>". This program then joins the
// group, and sends each RTP packet it hears to my.sbitx.net by UDP, on the
// same port number: the stream's id, the packet as it came, and a tag. UDP
// has no connection on which a challenge was answered, so every packet
// carries its own proof. The tag is the first eight bytes of the keyed
// hash of the id and the packet, and the key is made of the stretched
// cookie and the nonce, so that it is never sent and is good for this
// stream alone; PROTOCOL.md has it exactly. At "AUDIO OFF", or when the
// connection is lost or REMOTE is turned off, it leaves the group and
// sends no more.
//
// While the sound is being sent, and only then, the service is also told
// where the radio is tuned and in what mode, "TUNED 7074000 USB", for
// whoever is listening to see: the author asked for the frequency and the
// mode beside the player. It is said when the sound begins and when either
// changes, and no oftener than once a second, a dial being turned changing
// the frequency many times in one.
//
// SPEECH TO TRANSMIT
//
// The other way, when the owner speaks into a telephone afar and the radio
// is to send it: the author's design is the same path in reverse. The
// service sends the speech by UDP to where this program's sound comes from,
// which is the one address a home's router will let it back in at, in
// packets made the same way: the stream's id, an RTP packet of mu-law, and
// a tag. The tag's key is made as the sound's is but of other words,
// "zbitx-speech-1 <nonce>", so that a packet of the radio's sound, which
// anyone on the way can copy, cannot be sent back as speech to transmit.
// A packet with a wrong tag, or numbered at or behind one already taken,
// is dropped; the second is what stops a true packet being sent again
// later by someone who kept it. What passes is handed to the radio program
// at the port where it listens for speech, on the radio itself;
// rtp_microphone.c says what it does with it. So speech can come only
// while the sound is going, which it is whenever the owner's page is open.
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
// What this program prints is what it says in REMOTE_STATUS, and who came
// and went.

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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "rtp_audio.h"
#include "rtp_microphone.h"
#include "sha256.h"

#define DEFAULT_SERVER "my.sbitx.net"
#define DEFAULT_PORT "8081"
#define DEFAULT_RADIO_PORT "8081"
// where the radio program listens for speech: RTP_MICROPHONE_PORT, as a word
#define DEFAULT_MICROPHONE_PORT "5006"
#define DEFAULT_KEY_FILE "data/remote.key"
#define LOCAL_HOST "127.0.0.1"
#define LINE_ROOM 1024
#define TEXT_ROOM 256
#define NONCE_BYTES 16
#define NONCE_HEX_ROOM (2 * NONCE_BYTES + 1)
#define CALLSIGN_ROOM 20
#define CHALLENGE_ROOM 72
#define NAME_ROOM 40
#define GRID_ROOM 12
#define TUNED_ROOM 16
#define TUNED_EVERY_MS 1000
#define STATUS_ROOM 32
#define PATH_ROOM 512
#define COOKIE_LETTERS "abcdefghjkmnpqrstuvwxyz"
#define COOKIE_LENGTH 12
#define COOKIE_GROUP 4
#define COOKIE_ROOM (COOKIE_LENGTH + 1)
#define COOKIE_SHOWN_ROOM (COOKIE_LENGTH + COOKIE_LENGTH / COOKIE_GROUP)
#define KEY_FILE_MODE 0600
#define STRETCH_ROUNDS 50000
#define SALT_BEGINS "zbitx-1 "
#define REFUSED_RETRY_MS 15000
#define STREAM_ID_BYTES 8
#define TAG_BYTES 8
// a packet of speech this far ahead of the last, or less, is taken
#define SPEECH_AHEAD_MOST 3000
#define SOUND_PACKET_BYTES (STREAM_ID_BYTES + RTP_PACKET_BYTES + TAG_BYTES)
#define LOOPBACK "127.0.0.1"
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
	TO_CONNECT,
	ONLINE,
	WAITING_TO_RETRY,
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
static const char *microphone_port = DEFAULT_MICROPHONE_PORT;

static const char *key_file = DEFAULT_KEY_FILE;

static enum state state = OFF;
static int is_wanted;
static char callsign[CALLSIGN_ROOM];
static int is_callsign_new;
static char grid[GRID_ROOM];
static char grid_told[GRID_ROOM];
static int is_grid_told;
static char tuned_hz[TUNED_ROOM];
static char tuned_mode[TUNED_ROOM];
static char tuned_told[2 * TUNED_ROOM + 2];
static long tuned_told_ms;
static char cookie[COOKIE_ROOM];
static char stretched[SHA256_HEX_ROOM];
static char stretched_for[CALLSIGN_ROOM];
static int radio_fd = NOT_OPEN;
static struct lines radio_lines;
static long radio_retry_ms;
static int line_fd = NOT_OPEN;
static struct lines line_lines;
static long next_ping_ms;
static long last_heard_ms;
static long retry_ms;
static long retry_after_ms = RETRY_FIRST_MS;
static char status_said[STATUS_ROOM];
static int sound_in = NOT_OPEN;
static int sound_out = NOT_OPEN;
static uint8_t stream_id[STREAM_ID_BYTES];
static char stream_key[SHA256_HEX_ROOM];
static char speech_key[SHA256_HEX_ROOM];
static int speech_out = NOT_OPEN;
static struct sockaddr_in speech_port;
static int is_speech_begun;
static uint16_t speech_sequence;
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

/* the cookie */

static int is_a_cookie(const char *text) {
	int i;

	if (strlen(text) != COOKIE_LENGTH)
		return 0;
	for (i = 0; i < COOKIE_LENGTH; i++) {
		if (strchr(COOKIE_LETTERS, text[i]) == NULL)
			return 0;
	}
	return 1;
}

// twelve letters, each as likely as another: a random byte too large to
// divide evenly among the letters is thrown away and another taken
static int make_a_cookie(void) {
	int letters = (int)strlen(COOKIE_LETTERS);
	int evenly = 256 - 256 % letters;
	uint8_t byte;
	int made = 0;
	int file;
	int is_written;

	while (made < COOKIE_LENGTH) {
		if (getrandom(&byte, 1, 0) != 1)
			return -1;
		if (byte < evenly)
			cookie[made++] = COOKIE_LETTERS[byte % letters];
	}
	cookie[COOKIE_LENGTH] = 0;
	file = open(key_file, O_WRONLY | O_CREAT | O_TRUNC, KEY_FILE_MODE);
	if (file < 0)
		return -1;
	is_written = write(file, cookie, COOKIE_LENGTH) == COOKIE_LENGTH
		&& write(file, "\n", 1) == 1;
	close(file);
	if (!is_written)
		return -1;
	return 0;
}

// the radio's cookie, from its file, or a new one if it has none; says
// whether there is one
static int have_a_cookie(void) {
	char line[LINE_ROOM];
	FILE *file;

	if (is_a_cookie(cookie))
		return 1;
	file = fopen(key_file, "r");
	if (file != NULL) {
		if (fgets(line, sizeof(line), file) != NULL) {
			line[strcspn(line, "\r\n")] = 0;
			if (is_a_cookie(line))
				snprintf(cookie, sizeof(cookie), "%s", line);
		}
		fclose(file);
	}
	if (is_a_cookie(cookie))
		return 1;
	return make_a_cookie() == 0;
}

// "COOKIE abcd-jklm-wxyz", for the owner to type at the website
static void show_the_cookie(char *text, int room) {
	char shown[COOKIE_SHOWN_ROOM];
	int used = 0;
	int i;

	for (i = 0; i < COOKIE_LENGTH; i++) {
		if (i > 0 && i % COOKIE_GROUP == 0)
			shown[used++] = '-';
		shown[used++] = cookie[i];
	}
	shown[used] = 0;
	snprintf(text, room, "COOKIE %s", shown);
}

// the cookie made slow to guess, with the callsign as salt; done once for
// a callsign, since it takes a moment
static void stretch_the_cookie(void) {
	char salt[sizeof(SALT_BEGINS) + CALLSIGN_ROOM];
	size_t i;

	if (!strcmp(stretched_for, callsign))
		return;
	snprintf(salt, sizeof(salt), "%s%s", SALT_BEGINS, callsign);
	for (i = 0; salt[i] != 0; i++) {
		if (salt[i] >= 'A' && salt[i] <= 'Z')
			salt[i] = (char)(salt[i] - 'A' + 'a');
	}
	pbkdf2_sha256_hex(cookie, salt, STRETCH_ROUNDS, stretched);
	snprintf(stretched_for, sizeof(stretched_for), "%s", callsign);
}

// the challenge is answered with the radio's cookie, and the service's own
// proof that it has the cookie is checked; PROTOCOL.md has the handshake.
// name is what the website calls this radio, vu2ese-home
static int authenticate(int fd, char *name) {
	uint8_t random_bytes[NONCE_BYTES];
	char nonce[NONCE_HEX_ROOM];
	char line[LINE_ROOM];
	char challenge[CHALLENGE_ROOM];
	char text[TEXT_ROOM];
	char proof[SHA256_HEX_ROOM];
	char theirs[SHA256_HEX_ROOM + 16];
	int i;

	name[0] = 0;
	stretch_the_cookie();
	if (read_line_waiting(fd, line, sizeof(line)) < 0)
		return AUTH_NO_ANSWER;
	if (sscanf(line, "CHALLENGE %64s", challenge) != 1)
		return AUTH_NO_ANSWER;
	if (getrandom(random_bytes, sizeof(random_bytes), 0) != sizeof(random_bytes))
		return AUTH_NO_ANSWER;
	for (i = 0; i < NONCE_BYTES; i++)
		snprintf(nonce + 2 * i, 3, "%02x", random_bytes[i]);
	snprintf(text, sizeof(text), "zbitx-auth-1 radio %s %s %s", callsign, challenge, nonce);
	hmac_sha256_hex(stretched, text, proof);
	snprintf(line, sizeof(line), "AUTH radio %s %s %s\n", callsign, nonce, proof);
	if (send_text(fd, line) < 0)
		return AUTH_NO_ANSWER;
	if (read_line_waiting(fd, line, sizeof(line)) < 0)
		return AUTH_NO_ANSWER;
	if (!strncmp(line, "ERROR AUTH", strlen("ERROR AUTH")))
		return AUTH_REFUSED;
	if (sscanf(line, "WELCOME %39s %80s", name, theirs) != 2)
		return AUTH_NO_ANSWER;
	snprintf(text, sizeof(text), "zbitx-auth-1 server %s %s %s", callsign, challenge, nonce);
	hmac_sha256_hex(stretched, text, proof);
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
	char said[STATUS_ROOM];
	char callsign_heard[CALLSIGN_ROOM];
	int got;

	snprintf(callsign_heard, sizeof(callsign_heard), "%s", callsign);
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
		sscanf(line, "FREQ %15s", tuned_hz);
		sscanf(line, "MODE %15s", tuned_mode);
		if (!strncmp(line, "MYGRID", strlen("MYGRID"))) {
			grid[0] = 0;
			sscanf(line, "MYGRID %11s", grid);
		}
		if (!strncmp(line, "MYCALLSIGN", strlen("MYCALLSIGN"))) {
			callsign_heard[0] = 0;
			sscanf(line, "MYCALLSIGN %19s", callsign_heard);
		}
		got = take_line(&radio_lines, radio_fd, line, sizeof(line));
	}
	// with no radio program there is nothing to offer anyone
	if (got < 0) {
		close_fd(&radio_fd);
		is_wanted = 0;
	}
	// a radio that changes hands, or whose callsign is put right, begins
	// again under the new one
	if (strcmp(callsign_heard, callsign)) {
		snprintf(callsign, sizeof(callsign), "%s", callsign_heard);
		is_callsign_new = 1;
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
	char name[NAME_ROOM];
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
	if (authenticate(s->service_fd, name) != AUTH_OK || send_text(s->service_fd, line) < 0
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

/* the radio's sound, to the service while somebody listens */

static void sound_off(void) {
	close_fd(&sound_in);
	close_fd(&sound_out);
	close_fd(&speech_out);
}

// the group the radio program sends its sound to is joined, on loopback,
// and a socket is made for sending it on
static void sound_on(const char *id_hex, const char *nonce) {
	struct addrinfo hints;
	struct addrinfo *found;
	struct sockaddr_in any;
	struct ip_mreq membership;
	char text[TEXT_ROOM];
	unsigned int byte;
	int one = 1;
	int i;

	sound_off();
	for (i = 0; i < STREAM_ID_BYTES; i++) {
		if (sscanf(id_hex + 2 * i, "%2x", &byte) != 1)
			return;
		stream_id[i] = (uint8_t)byte;
	}
	snprintf(text, sizeof(text), "zbitx-audio-1 %.64s", nonce);
	hmac_sha256_hex(stretched, text, stream_key);
	snprintf(text, sizeof(text), "zbitx-speech-1 %.64s", nonce);
	hmac_sha256_hex(stretched, text, speech_key);
	is_speech_begun = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(server, port, &hints, &found) != 0)
		return;
	sound_out = socket(found->ai_family, found->ai_socktype | SOCK_NONBLOCK,
		found->ai_protocol);
	if (sound_out < 0 || connect(sound_out, found->ai_addr, found->ai_addrlen) < 0) {
		freeaddrinfo(found);
		sound_off();
		return;
	}
	freeaddrinfo(found);

	sound_in = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	memset(&any, 0, sizeof(any));
	any.sin_family = AF_INET;
	any.sin_port = htons(RTP_AUDIO_PORT);
	inet_pton(AF_INET, RTP_AUDIO_GROUP, &membership.imr_multiaddr);
	inet_pton(AF_INET, LOOPBACK, &membership.imr_interface);
	tuned_told[0] = 0;
	if (sound_in < 0
			|| setsockopt(sound_in, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0
			|| bind(sound_in, (struct sockaddr *)&any, sizeof(any)) < 0
			|| setsockopt(sound_in, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership,
				sizeof(membership)) < 0) {
		sound_off();
		return;
	}
	// and one for handing speech to the radio program
	speech_out = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	memset(&speech_port, 0, sizeof(speech_port));
	speech_port.sin_family = AF_INET;
	speech_port.sin_port = htons((uint16_t)atoi(microphone_port));
	inet_pton(AF_INET, LOOPBACK, &speech_port.sin_addr);
}

// every packet of sound that has come is sent on, with the stream's id
// before it and its tag after
static void sound_pass_on(void) {
	uint8_t packet[SOUND_PACKET_BYTES];
	uint8_t hash[SHA256_BYTES];
	ssize_t got;

	if (sound_in == NOT_OPEN)
		return;
	memcpy(packet, stream_id, STREAM_ID_BYTES);
	got = recv(sound_in, packet + STREAM_ID_BYTES, RTP_PACKET_BYTES, 0);
	while (got > 0) {
		if (got == RTP_PACKET_BYTES) {
			hmac_sha256((const uint8_t *)stream_key, strlen(stream_key), packet,
				STREAM_ID_BYTES + RTP_PACKET_BYTES, hash);
			memcpy(packet + STREAM_ID_BYTES + RTP_PACKET_BYTES, hash, TAG_BYTES);
			send(sound_out, packet, sizeof(packet), MSG_NOSIGNAL);
		}
		got = recv(sound_in, packet + STREAM_ID_BYTES, RTP_PACKET_BYTES, 0);
	}
}

// every packet of speech the service has sent is handed to the radio
// program, if its tag is right and it is ahead of the last
static void speech_pass_on(void) {
	uint8_t packet[SOUND_PACKET_BYTES + 1];
	uint8_t hash[SHA256_BYTES];
	uint16_t sequence;
	uint8_t differing;
	ssize_t got;
	int i;

	if (sound_out == NOT_OPEN || speech_out == NOT_OPEN)
		return;
	for (;;) {
		got = recv(sound_out, packet, sizeof(packet), 0);
		if (got < 0)
			return;
		if (got != SOUND_PACKET_BYTES || memcmp(packet, stream_id, STREAM_ID_BYTES) != 0)
			continue;
		hmac_sha256((const uint8_t *)speech_key, strlen(speech_key), packet,
			STREAM_ID_BYTES + RTP_PACKET_BYTES, hash);
		// compared without stopping at the first byte that differs, so that
		// how long it takes says nothing of how nearly right a guess was
		differing = 0;
		for (i = 0; i < TAG_BYTES; i++)
			differing |= (uint8_t)(hash[i] ^ packet[STREAM_ID_BYTES + RTP_PACKET_BYTES + i]);
		if (differing)
			continue;
		sequence = (uint16_t)(packet[STREAM_ID_BYTES + 2] << 8 | packet[STREAM_ID_BYTES + 3]);
		if (is_speech_begun && (uint16_t)(sequence - speech_sequence - 1) >= SPEECH_AHEAD_MOST)
			continue;
		is_speech_begun = 1;
		speech_sequence = sequence;
		sendto(speech_out, packet + STREAM_ID_BYTES, RTP_PACKET_BYTES, MSG_NOSIGNAL,
			(struct sockaddr *)&speech_port, sizeof(speech_port));
	}
}

/* the connection kept to my.sbitx.net */

static void line_close(long now) {
	close_fd(&line_fd);
	sound_off();
	state = WAITING_TO_RETRY;
	retry_ms = now + retry_after_ms;
	retry_after_ms *= 2;
	if (retry_after_ms > RETRY_MOST_MS)
		retry_after_ms = RETRY_MOST_MS;
}

static void line_open(long now) {
	char name[NAME_ROOM];
	char line[LINE_ROOM];
	char said[STATUS_ROOM];
	int result = AUTH_NO_ANSWER;

	say_status("CONNECTING");
	line_fd = dial(server, port);
	if (line_fd != NOT_OPEN)
		result = authenticate(line_fd, name);
	// the website does not know this radio, not yet or no longer: the
	// cookie is shown, for the owner to add the radio with, and the
	// website is asked again before long
	if (result == AUTH_REFUSED) {
		close_fd(&line_fd);
		state = WAITING_TO_RETRY;
		retry_ms = now + REFUSED_RETRY_MS;
		show_the_cookie(said, sizeof(said));
		say_status(said);
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
	is_grid_told = 0;
	snprintf(said, sizeof(said), "ONLINE %.22s", name);
	say_status(said);
}

static void line_poll(long now) {
	char line[LINE_ROOM];
	char token[LINE_ROOM];
	char nonce[LINE_ROOM];
	int got = take_line(&line_lines, line_fd, line, sizeof(line));

	while (got > 0) {
		last_heard_ms = now;
		if (sscanf(line, "SESSION %64s", token) == 1)
			session_open(token);
		if (sscanf(line, "AUDIO ON %16s %64s", token, nonce) == 2)
			sound_on(token, nonce);
		if (!strcmp(line, "AUDIO OFF"))
			sound_off();
		got = take_line(&line_lines, line_fd, line, sizeof(line));
	}
	// the grid set on the radio, once the connection is made and again
	// whenever it is changed
	if (got == 0 && (!is_grid_told || strcmp(grid, grid_told))) {
		snprintf(line, sizeof(line), "GRID %s\n", grid);
		if (send_text(line_fd, line) < 0)
			got = -1;
		snprintf(grid_told, sizeof(grid_told), "%s", grid);
		is_grid_told = 1;
	}
	// where the radio is tuned, while somebody is listening to it
	snprintf(token, sizeof(token), "%s %s", tuned_hz, tuned_mode);
	if (got == 0 && sound_in != NOT_OPEN && strcmp(token, tuned_told)
			&& now - tuned_told_ms >= TUNED_EVERY_MS) {
		snprintf(line, sizeof(line), "TUNED %.40s\n", token);
		if (send_text(line_fd, line) < 0)
			got = -1;
		snprintf(tuned_told, sizeof(tuned_told), "%.33s", token);
		tuned_told_ms = now;
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
	close_fd(&line_fd);
	sound_off();
	sessions_close();
	state = OFF;
	retry_after_ms = RETRY_FIRST_MS;
	say_status("OFF");
}

static void follow_the_owner(long now) {
	if (!is_wanted) {
		if (state != OFF)
			switch_off();
		return;
	}
	if (is_callsign_new && state != OFF) {
		close_fd(&line_fd);
		sessions_close();
		state = OFF;
	}
	is_callsign_new = 0;
	if (state == OFF) {
		if (callsign[0] == 0) {
			say_status("SET MYCALLSIGN FIRST");
			return;
		}
		if (!have_a_cookie()) {
			say_status("COOKIE NOT SAVED");
			return;
		}
		state = TO_CONNECT;
	}
	if (state == WAITING_TO_RETRY && now >= retry_ms)
		state = TO_CONNECT;
	if (state == TO_CONNECT)
		line_open(now);
	else if (state == ONLINE)
		line_poll(now);
}

static void wait_and_pass_on(void) {
	struct pollfd polls[4 + 2 * SESSIONS_MAX];
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
	if (sound_in != NOT_OPEN) {
		polls[count].fd = sound_in;
		polls[count].events = POLLIN;
		polls[count++].revents = 0;
	}
	if (sound_out != NOT_OPEN) {
		polls[count].fd = sound_out;
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
	sound_pass_on();
	speech_pass_on();
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
	int i;

	for (i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--server"))
			server = argv[i + 1];
		else if (!strcmp(argv[i], "--port"))
			port = argv[i + 1];
		else if (!strcmp(argv[i], "--radio-port"))
			radio_port = argv[i + 1];
		else if (!strcmp(argv[i], "--key"))
			key_file = argv[i + 1];
		else if (!strcmp(argv[i], "--microphone-port"))
			microphone_port = argv[i + 1];
		else
			i = argc;
	}
	if (i != argc) {
		printf("zbitx_remote [--server host] [--port n] [--radio-port n] [--key file]"
			" [--microphone-port n]\n");
		return EXIT_FAILURE;
	}
	signal(SIGPIPE, SIG_IGN);
	for (i = 0; i < SESSIONS_MAX; i++) {
		sessions[i].service_fd = NOT_OPEN;
		sessions[i].radio_fd = NOT_OPEN;
	}
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
