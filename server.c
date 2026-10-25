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
//     CONTROL CW MACRO action CQ/RST/End
//     CONTROL STATION MYCALLSIGN text
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
// client that cannot keep up misses lines rather than falling behind: how
// is below.
//
// While the radio transmits there is no spectrum, and in its place the
// client is sent, as often, the envelope of what is being sent, to draw as
// an oscilloscope would:
//
//     MODULATION 00030a1f...
//
// Each point is two hex digits, how far the signal swung from zero in half
// a millisecond, whatever the drive: 2TONE's two tones come to 107, and
// 255 is a microphone driven hard, or harder. The points are a fifth of a second, in the order the core keeps
// them, going round: the newest is somewhere in the line and the oldest
// follows it. core_sdr.c says how they are kept.
//
// A client that sends "messages on" is sent what the radio copies off the
// air, FT8's messages, a line each as it is copied, beginning with what
// the radio still has of what was copied before; messages.c says what is
// in a line:
//
//     MESSAGE 043015|-12|1797|CQ VU2ESE MK97
//
// until it sends "messages off". A message is not the radio's state, as a
// control's value is, so none is left out for a slow client: each client
// has its own place in what messages.c keeps, and is sent on from there
// as its socket takes them. One that stalls for longer than messages.c
// keeps misses the oldest.
//
// A client answers a message, as the front panel does with a click on it,
// by sending the message's line back after the word answer:
//
//     answer 043015|-12|1797|CQ VU2ESE MK97
//
// What is done with it is not the server's business; whoever opened the
// server says, with server_on_answer().
//
// What the radio has to say, that the front panel shows on its status
// line, goes to every client as well, as it is said:
//
//     SAY that message cannot be answered
//     WARN MYGRID is not set: enter it in SET
//     QSO VU2XZ > -19 > R-14 > RR73
//
// And what the radio reads of CW off the air goes to every client as it
// is read, a few characters at a time, the spaces between words among
// them, so a line may be CW and nothing but a space:
//
//     CW CQ CQ DE VU
//     CW 2ESE
//
// In JTTY, what has been read in each of the bins of the passband goes
// to every client as it changes: the bin, counted from 0, the pitch of
// what was last heard in it in Hz, and the newest of its text, all of it
// that is kept and not only what is new, so that a client shows a bin's
// line as it is told it. modems/modem_jtty.c says what a bin is and what
// is in its text. A client that has just come sends jtty, and is told
// every bin that has something in it:
//
//     jtty
//     JTTY_BIN 10 1507 RAN ALL NIGHT ON BAND NOISE - NO FALSE DECODES! |
//
// While a message is typed live, the characters each frame takes from
// what was typed go to every client as the frame is made, a space as "~",
// so that a client keeps in its box only what has still to go, as CW_SENT
// does in CW:
//
//     JTTY_SENT CQ~CQ
//
// SAY is a remark: why something asked for was not done, how a thing
// went. WARN is one the operator has to act on before what was asked can
// be done, a callsign or a grid not set; a client may show it more boldly.
// QSO is how the FT8 contact under way stands, and comes empty when it
// ends. None of them is a control and none is kept: a client that
// connects later is not told what was said before.
//
// A client that sends log is sent the logbook's newest entries, a hundred
// at most, the newest first; with the beginning of a callsign after the
// word, only the entries with stations whose callsign begins so, as the
// front panel's search has it. First a line that says how many are coming
// and what was searched for, then an entry to a line, as logbook.c writes
// one:
//
//     log VU2
//     LOG_LIST 2 VU2
//     LOG 972|2026-04-08|04:45|7130|LSB|VU2XZ|59|59
//     LOG 961|2026-04-02|14:10|14074|FT8|VU2ESE|-14 MK97|-09 MK81
//
// The entries go as the client's socket takes them. From then on the client
// is sent the list of its search afresh whenever an entry is added to the
// logbook or taken out of it, by anyone: by FT8, by the logger, by the
// front panel or by another client.
//
// An entry is never changed, but a bad one can be deleted, by its number,
// which is the first field of its line. What is being searched for goes
// with the request, as the author's rule for the panel has it, so that the
// list that comes back is still of that search:
//
//     logdel 972 VU2
//
// A client that sends adif is sent the whole logbook, every entry, the
// oldest first, as an ADIF file that logging programs read, a line of the
// file to a line, between a line that says how many entries come and one
// that says the file has ended; the client puts the lines after ADIF
// together, each with its line end, and has the file. logbook.c says how
// an entry is written:
//
//     adif
//     ADIF_BEGIN 2
//     ADIF zbitxre logbook <ADIF_VER:5>3.1.4 <PROGRAMID:7>zbitxre <EOH>
//     ADIF <CALL:6>VU2ESE <QSO_DATE:8>20260402 ... <EOR>
//     ADIF <CALL:5>VU2XZ <QSO_DATE:8>20260408 ... <EOR>
//     ADIF_END 2
//
// ADIF_END says how many entries were sent, which is fewer than ADIF_BEGIN
// said only if the logbook changed, or could not be read, on the way.
// It goes as the client's socket takes it, along with everything else.
//
// An entry is written with the radio's controls of the group LOGGER, which
// logger.c describes: CALL, SENT, RECV, EXCH and NR are set as any text is,
// and "LOGGER SAVE" logs the entry, "LOGGER WIPE" clears them.
//
// A client that sends time is answered with the radio's time, in UTC, as
// seconds since 1970, or 0 while the radio does not know the time:
//
//     TIME 1791397105
//
// The radio's CLOCK says the time each minute, which is as often as it
// changes for a control; a client that shows the seconds asks this once,
// counts on from it by its own clock, and has the start of each minute
// from CLOCK. The radio's time is hw/radio_clock.c's, which may be ahead
// of or behind the computer's own.
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
// Nothing here waits. Sockets are non-blocking, and one slow client never
// holds up the radio or the others.
//
// A CLIENT THAT IS SLOW
//
// A client may stall for seconds, a web page on a poor connection, while
// the radio goes on changing: tuning sends a change many times a second,
// and the spectrum ten lines a second. Were each change and each line
// queued for it, the queue would grow without end, and when the client
// woke it would be shown seconds of the past, late. So the radio sends a
// client what is, not what happened:
//
//   A client is not sent a queue of changes. Each client has a flag for
//   each control, set when the control changes. When the client's socket
//   can take more, it is sent the value each flagged control has now, and
//   the flags are cleared. A control that changed a hundred times while
//   the client stalled is sent once, with its last value. However long a
//   client stalls, it costs the radio the same, and when it wakes it is
//   up to date at once. The changes come in the order of the table, not
//   the order they were made in; a client is told what is, and that is the
//   same either way.
//
//   A line of the spectrum goes to a client only when nothing sent to it
//   before is still waiting, neither here nor in the kernel's queue for its
//   socket. A slow client misses lines, and the line it is sent is always
//   the newest.
//
//   The kernel's queue for each client's socket is made small, so that
//   what is in it cannot be long out of date.
//
// A client is disconnected only when its connection fails, or when it asks
// for the schema faster than it reads it: the replies to what a client
// asks, the schema and ERROR, are queued, in OUT_MAX bytes.
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
#include <linux/sockios.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "control.h"
#include "logbook.h"
#include "messages.h"
#include "radio_clock.h"
#include "server.h"

#define LINE_MAX_LENGTH 128
#define OUT_MAX 4096
#define SEND_BUFFER_BYTES 8192
#define LISTEN_QUEUE 4
#define NOBODY (-1)
#define WHY_MAX CONTROL_WHY_MAX
#define LABEL_ROOM 16
#define SPECTRUM_LINE_MAX (2 * SERVER_SPECTRUM_POINTS_MAX + 32)
#define SPECTRUM_BEGINS_MAX 32
#define HEX_DIGITS "0123456789abcdef"
#define MESSAGE_LINE_MAX (MESSAGE_TEXT_ROOM + 48)
#define ANSWER_WORD "answer "
#define LOG_WORD "log"
#define BINS_WORD "jtty"
#define LOG_DELETE_WORD "logdel "
#define ADIF_WORD "adif"
#define ADIF_LINE_MAX 512
#define ADIF_AT_ONCE 6
#define NOT_EXPORTING (-1)
#define LOG_ENTRIES_MAX 100
#define LOG_LINE_MAX (12 * LOGBOOK_FIELD_MAX)
#define NOT_LISTING (-1)

struct client {
	int fd;
	char in[LINE_MAX_LENGTH];
	int in_count;
	char out[OUT_MAX];
	int out_count;
	int wants_spectrum;
	int wants_messages;
	unsigned long last_message;
	// the logbook: what the client searches for, the next entry of the list
	// to go to it, and how the logbook stood when the list was begun
	int shows_logbook;
	char log_searched_for[LOGBOOK_FIELD_MAX];
	int log_next;
	unsigned long log_changes;
	// the whole logbook, as ADIF: the number of the last entry sent, or
	// NOT_EXPORTING, and how many have gone
	int adif_after;
	int adif_sent;
	uint8_t changed[CONTROLS_MAX];
};

static void (*answer_a_message)(const char *line);
static void (*resend_the_bins)(void);

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
	c->wants_messages = 0;
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

// what the kernel has been given for a client and not yet sent
static int unsent_bytes(const struct client *c) {
	int count = 0;

	if (ioctl(c->fd, SIOCOUTQ, &count) < 0)
		return 0;
	return count;
}

static int has_changes(const struct client *c) {
	int i;

	for (i = 0; i < control_count(); i++) {
		if (c->changed[i])
			return 1;
	}
	return 0;
}

// the value each flagged control has now, as many as there is room for;
// the rest stay flagged until the socket can take more
static void client_send_changes(struct client *client) {
	char line[LINE_MAX_LENGTH];
	const struct control *c;
	int length;
	int i;

	if (client->fd == NOBODY)
		return;
	for (i = 0; i < control_count(); i++) {
		if (!client->changed[i])
			continue;
		c = control_at(i);
		if (c->kind == CONTROL_ACTION) {
			client->changed[i] = 0;
			continue;
		}
		length = snprintf(line, sizeof(line), "%s %s\n", c->name, c->value);
		if (client->out_count + length > OUT_MAX)
			break;
		memcpy(client->out + client->out_count, line, length);
		client->out_count += length;
		client->changed[i] = 0;
	}
	client_flush(client);
}

static int index_of_control(const struct control *c) {
	int i;

	for (i = 0; i < control_count(); i++) {
		if (control_at(i) == c)
			return i;
	}
	return NOBODY;
}

// a reply to what a client asked; a client that does not read its replies
// fills OUT_MAX and is disconnected
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
	int index;
	int i;

	if (listener == NOBODY)
		return;
	index = index_of_control(c);
	if (index == NOBODY)
		return;
	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (i == origin || clients[i].fd == NOBODY)
			continue;
		clients[i].changed[index] = 1;
		client_send_changes(&clients[i]);
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

// a line of points, two hex digits each, after its first words, to every
// client that asked for the spectrum and has nothing waiting to be sent
static void send_points(const char *begins, const uint8_t *points, int count) {
	char line[SPECTRUM_LINE_MAX];
	int length;
	int i;

	if (listener == NOBODY)
		return;
	if (count > SERVER_SPECTRUM_POINTS_MAX)
		count = SERVER_SPECTRUM_POINTS_MAX;
	length = snprintf(line, sizeof(line), "%s", begins);
	for (i = 0; i < count; i++) {
		line[length++] = HEX_DIGITS[points[i] >> 4];
		line[length++] = HEX_DIGITS[points[i] & 0xf];
	}
	line[length++] = '\n';
	line[length] = 0;
	for (i = 0; i < SERVER_CLIENTS_MAX; i++) {
		if (clients[i].fd == NOBODY || !clients[i].wants_spectrum)
			continue;
		if (clients[i].out_count > 0 || unsent_bytes(&clients[i]) > 0)
			continue;
		client_queue_line(&clients[i], line);
	}
}

void server_send_spectrum(int span_hz, const uint8_t *points, int count) {
	char begins[SPECTRUM_BEGINS_MAX];

	snprintf(begins, sizeof(begins), "SPECTRUM %d ", span_hz);
	send_points(begins, points, count);
}

void server_send_modulation(const uint8_t *points, int count) {
	send_points("MODULATION ", points, count);
}

static void tell_everyone(const char *word, const char *text) {
	char line[LINE_MAX_LENGTH + LABEL_ROOM];
	struct client *c;

	if (listener == NOBODY)
		return;
	snprintf(line, sizeof(line), "%s %.*s\n", word, LINE_MAX_LENGTH - 1, text);
	for (c = clients; c < clients + SERVER_CLIENTS_MAX; c++) {
		if (c->fd != NOBODY && c->out_count + (int)sizeof(line) <= OUT_MAX)
			client_queue_line(c, line);
	}
}

void server_say(const char *text) {
	tell_everyone("SAY", text);
}

void server_warn(const char *text) {
	tell_everyone("WARN", text);
}

void server_show_qso(const char *text) {
	tell_everyone("QSO", text);
}

void server_send_cw(const char *text) {
	tell_everyone("CW", text);
}

// what the radio has begun to send of a text, a macro or a client's keying:
// "CW_SENT CQ_DE", a space as "_", which a line's value cannot begin or end
// with otherwise. KBD takes each character out of its box of what is still
// to go
void server_send_cw_sent(const char *text) {
	char marked[LINE_MAX_LENGTH];
	int i;

	snprintf(marked, sizeof(marked), "%s", text);
	for (i = 0; marked[i] != 0; i++) {
		if (marked[i] == ' ')
			marked[i] = '_';
	}
	tell_everyone("CW_SENT", marked);
}

void server_on_answer(void (*answer)(const char *line)) {
	answer_a_message = answer;
}

void server_on_bins_asked(void (*resend)(void)) {
	resend_the_bins = resend;
}

void server_send_jtty_sent(const char *text) {
	char marked[LINE_MAX_LENGTH];
	int i;

	snprintf(marked, sizeof(marked), "%s", text);
	for (i = 0; marked[i] != 0; i++) {
		if (marked[i] == ' ')
			marked[i] = '~';
	}
	tell_everyone("JTTY_SENT", marked);
}

void server_send_jtty_bin(int bin, int hz, const char *text) {
	char said[LINE_MAX_LENGTH];

	snprintf(said, sizeof(said), "%d %d %s", bin, hz, text);
	tell_everyone("JTTY_BIN", said);
}

// to each client that asked, the messages copied since it was last sent
// any, as many as there is room to queue for it
void server_send_messages(void) {
	char text[MESSAGE_LINE_MAX];
	char line[MESSAGE_LINE_MAX + LABEL_ROOM];
	struct message m;
	struct client *c;

	if (listener == NOBODY)
		return;
	for (c = clients; c < clients + SERVER_CLIENTS_MAX; c++) {
		while (c->fd != NOBODY && c->wants_messages
				&& c->out_count + (int)sizeof(line) <= OUT_MAX
				&& message_next(&c->last_message, &m)) {
			message_line(&m, text, sizeof(text));
			snprintf(line, sizeof(line), "MESSAGE %s\n", text);
			client_queue_line(c, line);
		}
	}
}

// the list of what a client searches the logbook for is begun
static void list_logbook(struct client *c, const char *callsign) {
	while (*callsign == ' ')
		callsign++;
	snprintf(c->log_searched_for, sizeof(c->log_searched_for), "%.*s",
		LOGBOOK_FIELD_MAX - 1, callsign);
	c->shows_logbook = 1;
	c->log_next = 0;
}

// "972 VU2": the entry of that number is deleted, and the list of the
// search that came with it begun again
static void delete_from_logbook(struct client *c, const char *asked) {
	const char *callsign = strchr(asked, ' ');

	if (logbook_delete(atoi(asked)) < 0)
		client_queue_line(c, "WARN log: the entry could not be deleted\n");
	if (callsign == NULL)
		callsign = "";
	list_logbook(c, callsign);
}

// to each client that asked for the logbook as ADIF, a few of its entries
// at a time, and the end when they are all gone. They wait, as the spectrum
// does, until the client has taken all it was sent, so that a long logbook
// takes its turn with the spectrum and does not stop it
static void send_adif(void) {
	char record[ADIF_LINE_MAX];
	char line[ADIF_LINE_MAX + LABEL_ROOM];
	struct client *c;
	int id, sent;

	for (c = clients; c < clients + SERVER_CLIENTS_MAX; c++) {
		if (c->fd == NOBODY || c->adif_after == NOT_EXPORTING || c->out_count > 0
				|| unsent_bytes(c) > 0)
			continue;
		for (sent = 0; sent < ADIF_AT_ONCE && c->fd != NOBODY
				&& c->adif_after != NOT_EXPORTING; sent++) {
			id = logbook_adif_after(c->adif_after, record, sizeof(record));
			if (id <= 0) {
				if (id < 0)
					client_queue_line(c, "WARN log: the logbook could not be read\n");
				snprintf(line, sizeof(line), "ADIF_END %d\n", c->adif_sent);
				client_queue_line(c, line);
				c->adif_after = NOT_EXPORTING;
				break;
			}
			snprintf(line, sizeof(line), "ADIF %s\n", record);
			client_queue_line(c, line);
			c->adif_after = id;
			c->adif_sent++;
		}
	}
}

// the ADIF file is begun: how many entries, and its header
static void export_logbook(struct client *c) {
	char line[ADIF_LINE_MAX + LABEL_ROOM];
	int count = logbook_count();

	if (count < 0) {
		client_queue_line(c, "WARN log: the radio has no logbook\n");
		client_queue_line(c, "ADIF_BEGIN 0\nADIF_END 0\n");
		return;
	}
	snprintf(line, sizeof(line), "ADIF_BEGIN %d\nADIF %s\n", count, logbook_adif_header());
	client_queue_line(c, line);
	c->adif_after = 0;
	c->adif_sent = 0;
}

// to each client that shows the logbook: the list it asked for, as much of
// it as there is room to queue, and the list again when the logbook has
// changed since
void server_send_log(void) {
	static struct logbook_row rows[LOG_ENTRIES_MAX];
	char text[LOG_LINE_MAX];
	char line[LOG_LINE_MAX + LABEL_ROOM];
	struct client *c;
	int count;

	if (listener == NOBODY)
		return;
	send_adif();
	for (c = clients; c < clients + SERVER_CLIENTS_MAX; c++) {
		if (c->fd == NOBODY || !c->shows_logbook)
			continue;
		if (c->log_next == NOT_LISTING && c->log_changes != logbook_changes())
			c->log_next = 0;
		if (c->log_next == NOT_LISTING || c->out_count + (int)sizeof(line) > OUT_MAX)
			continue;
		count = logbook_find(c->log_searched_for, rows, LOG_ENTRIES_MAX);
		if (count < 0) {
			client_queue_line(c, "WARN log: the radio has no logbook\n");
			count = 0;
		}
		if (c->log_next == 0) {
			c->log_changes = logbook_changes();
			snprintf(line, sizeof(line), "LOG_LIST %d %s\n", count, c->log_searched_for);
			client_queue_line(c, line);
		}
		while (c->fd != NOBODY && c->log_next < count
				&& c->out_count + (int)sizeof(line) <= OUT_MAX) {
			logbook_line(&rows[c->log_next], text, sizeof(text));
			snprintf(line, sizeof(line), "LOG %s\n", text);
			client_queue_line(c, line);
			c->log_next++;
		}
		if (c->log_next >= count)
			c->log_next = NOT_LISTING;
	}
}

static void send_all_settings(struct client *client) {
	int i;

	for (i = 0; i < control_count(); i++)
		client->changed[i] = 1;
	client_send_changes(client);
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
		else if (c->kind == CONTROL_ACTION)
			snprintf(line, sizeof(line), "CONTROL %s %s action %s\n",
				c->group, c->name, c->choices);
		else
			snprintf(line, sizeof(line), "CONTROL %s %s text\n", c->group, c->name);
		client_queue_line(client, line);
	}
	client_queue_line(client, "CONTROL END\n");
}

static void accept_client(void) {
	int send_buffer = SEND_BUFFER_BYTES;
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
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
	clients[i].fd = fd;
	clients[i].in_count = 0;
	clients[i].out_count = 0;
	clients[i].wants_spectrum = 0;
	clients[i].wants_messages = 0;
	clients[i].shows_logbook = 0;
	clients[i].log_next = NOT_LISTING;
	clients[i].adif_after = NOT_EXPORTING;
	memset(clients[i].changed, 0, sizeof(clients[i].changed));
	send_all_settings(&clients[i]);
}

static void handle_line(struct client *c, char *line) {
	char why[WHY_MAX];
	char answer[LINE_MAX_LENGTH + WHY_MAX + LABEL_ROOM];
	time_t now = 0;
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
	if (!strcmp(line, "time")) {
		if (radio_clock_now(&now) == RADIO_CLOCK_NOT_SET)
			now = 0;
		snprintf(answer, sizeof(answer), "TIME %lld\n", (long long)now);
		client_queue_line(c, answer);
		return;
	}
	if (!strcmp(line, "messages on")) {
		c->wants_messages = 1;
		c->last_message = 0;
		return;
	}
	if (!strcmp(line, "messages off")) {
		c->wants_messages = 0;
		return;
	}
	if (!strcmp(line, BINS_WORD)) {
		if (resend_the_bins != NULL)
			resend_the_bins();
		return;
	}
	if (!strncmp(line, ANSWER_WORD, strlen(ANSWER_WORD))) {
		if (answer_a_message != NULL)
			answer_a_message(line + strlen(ANSWER_WORD));
		return;
	}
	if (!strncmp(line, LOG_DELETE_WORD, strlen(LOG_DELETE_WORD))) {
		delete_from_logbook(c, line + strlen(LOG_DELETE_WORD));
		return;
	}
	if (!strcmp(line, ADIF_WORD) || !strcmp(line, ADIF_WORD " ")) {
		export_logbook(c);
		return;
	}
	if (!strcmp(line, LOG_WORD) || !strncmp(line, LOG_WORD " ", strlen(LOG_WORD " "))) {
		list_logbook(c, line + strlen(LOG_WORD));
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
		if (clients[i].out_count > 0 || has_changes(&clients[i]))
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
			client_send_changes(c);
		if (c->fd != NOBODY && (polls[i].revents & (POLLIN | POLLHUP | POLLERR)))
			read_client(c);
	}
}
