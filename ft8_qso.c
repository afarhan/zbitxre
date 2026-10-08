// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// ft8_qso.c — an FT8 contact, from the call to the logbook.
//
// This is the design written at the top of modems/modem_ft8.c, under "THE
// CONTACT", which says why each thing is so: the author's three ways of
// working, the states, after WSJT-X's, and the moment each side logs. Read
// that first; what is here is how it is done.
//
// The modem hears and sends; this decides what is sent. It reads what the
// modem copies from messages.c, as the terminal and the panel do, and hands
// what is to be sent to the modem with ft8_tx_arm(), for a slot. It runs in
// the thread that owns the controls, from ft8_qso_poll(), so that it can
// read the controls, log, and tell the panel, without a lock.
//
// THE CONTROLS
//
//   FT8_OPS    NORMAL, RUNNING or POUNCING: the author's "FT8-Operations",
//              in the panel's settings.
//   FT8_REPLY  RR73 or RRR: what we send when the R and report have come,
//              having called CQ. "FT8 Reply" in the settings.
//   TX_PITCH   where in the passband we send, 200 to 3000 Hz.
//   FT8_AUTO   OFF or ON, shown as AUTO. ON: while no contact is under way,
//              a station that calls us is answered, in any of the three
//              ways of working; that settles the question the design left
//              open for POUNCING. OFF: such a call is only in the list.
//              It starts OFF, so that the radio never transmits of itself
//              until the operator has said it may.
//   FT8_REPEAT 1 to 5, shown as REPEAT: how often a message is sent before
//              a contact that is not answered is given up. It starts at 5,
//              zbitxv2's. CQ in NORMAL is three times whatever it is.
//   FT8_TX1ST  ON or OFF, shown as TX1ST: which slots a CQ goes in, as
//              zbitxv2's FT8_TX1ST said. ON, the first and the third of the
//              minute, at :00 and :30; OFF, the second and the fourth, at
//              :15 and :45. It starts ON, as zbitxv2's did. It is only for
//              calling CQ: an answer goes in the slots the station called
//              in does not.
//   FT8_DO     an action: CQ starts calling CQ, HALT stops whatever is
//              being sent and ends the contact, without logging it.
//
// All but FT8_DO are kept with the other settings.
//
// AUTO answers only a call: a message to us with a grid, a report or
// nothing after our callsign. A sign-off to us, RR73, RRR or 73, comes
// late from a contact already ended and is not a call; and the station of
// the last contact is not answered of itself for two minutes, so that its
// R and report sent again, our RR73 having been missed, cannot start the
// contact again and log it twice. FT8_DO's buttons are
// on the panel's bottom row in FT8.
//
// A message to be answered comes from the panel as the line the panel
// showed, ft8_qso_answer(); panel_messages.c says what is in it. On the
// terminal, "answer=G0XYZ" answers the newest message heard from G0XYZ
// that may be answered.
//
// THE SLOTS
//
// Our messages go in every other slot, and the other station's in the
// slots between. When a message has been armed for our slot at T, the
// answer is looked for in the slot at T + 15, and what we send next goes in
// the slot at T + 30. The decision is made as soon as the modem says it has
// searched the other station's slot, about 14.6 seconds into it, or at
// 14.95 seconds if it has not, which leaves the modem time to make the
// sound before our slot begins at 15, and to send it from 15.5.
//
// What we send is added to messages.c as sent, and so is in the panel's
// list among what is heard; and the state of the contact is put on the
// panel's status bar and the terminal.
//
// THE EXCHANGE
//
// While a contact is under way, the panel's status line shows it, at the
// author's asking, in a colour of its own: the other station, and each
// word that has passed between us, theirs and ours, in the order they
// passed, "VU2XZ > -19 > R-14 > RR73". A word sent or heard again is not
// written again; it is counted, "RR73 x2". It is sent with
// panel_show_qso(), and an empty line when the contact ends, whether
// logged, given up or stopped. Calling CQ is not a contact yet; it begins
// when a station is taken.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "control.h"
#include "logbook.h"
#include "messages.h"
#include "panel.h"
#include "ft8_qso.h"
#include "modems/modem_ft8.h"
#include "modems/ft8_callsign_hash.h"

#define SLOT_SECONDS 15
#define DECIDED_BY_S 14.95
#define LATE_START_S 1.5
#define REPEAT_LOWEST 1
#define REPEAT_HIGHEST 5
#define AUTO_QUIET_FOR_S 120
#define CQ_CALLS_NORMAL 3
#define WAIT_SLOTS_MAX 8
#define QUIET_SLOTS_AFTER_RRR 2
#define HEARD_MAX 128
#define CALL_ROOM 16
#define CALL_LENGTH_MIN 3
#define GRID_ROOM 8
#define WORD_ROOM 16
#define MESSAGE_ROOM 40
#define SAY_ROOM 96
#define WORDS_MAX 5
#define GRID_LENGTH 4
#define REPORT_LOWEST (-30)
#define REPORT_HIGHEST 30
#define HZ_PER_KHZ 1000
#define TX_PITCH_LOWEST 200
#define TX_PITCH_HIGHEST 3000
#define TX_PITCH_STEP 10
#define SECONDS_DIGITS 2
#define NOTHING_YET 0
// the parity of a slot: the first and third of the minute, or the others
#define FIRST_SLOTS 0
#define SECOND_SLOTS 1
#define EXCHANGE_WORDS_MAX 12
#define EXCHANGE_ROOM 64

enum state {
	IDLE,
	CALLING_CQ,
	CALLING_STN,
	WAITING,
	REPORT,
	ROGER_REPORT,
	ROGERS,
	SIGNOFF,
};

// what follows the two callsigns of a message
enum word {
	WORD_NONE,
	WORD_GRID,
	WORD_REPORT,
	WORD_ROGER_REPORT,
	WORD_RRR,
	WORD_RR73,
	WORD_73,
	WORD_OTHER,
};

struct heard {
	time_t slot;
	int snr;
	int is_cq;
	char to[CALL_ROOM];
	char from[CALL_ROOM];
	char grid[GRID_ROOM];
	char extra[WORD_ROOM];
	enum word word;
	int report;
};

static struct {
	enum state state;
	char dx[CALL_ROOM];
	char dx_grid[GRID_ROOM];
	int report_sent;
	int report_received;
	int has_report;
	int sends;
	int cq_calls;
	int waits;
	int quiet_slots;
	int is_logged;
	time_t next_slot;
	time_t decide_slot;
	char message[MESSAGE_ROOM];
	char freq[CONTROL_VALUE_MAX];
} qso;

// the words of the contact under way, each with how often it has passed
static struct {
	char word[WORD_ROOM];
	int count;
} exchange[EXCHANGE_WORDS_MAX];
static int exchange_count;

static struct heard heard[HEARD_MAX];
static char last_worked[CALL_ROOM];
static double last_worked_at;
static time_t auto_slot;
static int heard_count;
static unsigned long last_read;

/* what the operator has set */

static int is_ops(const char *choice) {
	return !strcmp(control_get("FT8_OPS"), choice);
}

static void upper(char *to, int room, const char *from) {
	int i;

	for (i = 0; i < room - 1 && from[i] != 0; i++)
		to[i] = (char)toupper((unsigned char)from[i]);
	to[i] = 0;
}

static int sends_max(void) {
	return control_get_int("FT8_REPEAT");
}

static void my_call(char *call) {
	upper(call, CALL_ROOM, control_get("MYCALLSIGN"));
}

static void my_grid(char *grid) {
	upper(grid, GRID_LENGTH + 1, control_get("MYGRID"));
}

static void say(const char *text) {
	printf("ft8: %s\n", text);
	fflush(stdout);
	panel_say(text);
}

// what the operator must put right before anything can be sent
static void warn(const char *text) {
	printf("ft8: %s\n", text);
	fflush(stdout);
	panel_warn(text);
}

/* reading a message */

static int is_grid(const char *w) {
	return strlen(w) == GRID_LENGTH && w[0] >= 'A' && w[0] <= 'R' && w[1] >= 'A'
		&& w[1] <= 'R' && isdigit((unsigned char)w[2]) && isdigit((unsigned char)w[3])
		&& strcmp(w, "RR73") != 0;
}

// a callsign that may be sent: letters and digits, at least one of each,
// three or more in all, and a / where it is a portable one
static int is_callsign(const char *w) {
	int letters = 0;
	int digits = 0;
	int i;

	for (i = 0; w[i] != 0; i++) {
		if (isdigit((unsigned char)w[i]))
			digits++;
		else if (w[i] >= 'A' && w[i] <= 'Z')
			letters++;
		else if (w[i] != '/')
			return 0;
	}
	return letters > 0 && digits > 0 && letters + digits >= CALL_LENGTH_MIN;
}

// every first message of ours carries the station's callsign and its grid.
// Without them nothing is begun, and the operator is told which is wanting
static int is_station_set(void) {
	char call[CALL_ROOM];
	char grid[GRID_ROOM];

	my_call(call);
	my_grid(grid);
	if (call[0] == 0) {
		warn("MYCALLSIGN is not set: enter it in SET");
		return 0;
	}
	if (!is_callsign(call)) {
		warn("MYCALLSIGN is not a callsign: correct it in SET");
		return 0;
	}
	if (grid[0] == 0) {
		warn("MYGRID is not set: enter it in SET");
		return 0;
	}
	if (!is_grid(grid)) {
		warn("MYGRID is not a locator such as MK97: correct it in SET");
		return 0;
	}
	return 1;
}

static int is_report(const char *w) {
	return (w[0] == '+' || w[0] == '-') && isdigit((unsigned char)w[1]);
}

// a callsign as it is compared: without the brackets of a hash
static void callsign_of(char *call, const char *word) {
	char bare[CALL_ROOM];
	int length;

	snprintf(bare, sizeof(bare), "%s", word);
	length = (int)strlen(bare);
	if (bare[0] == '<' && length > 1 && bare[length - 1] == '>') {
		bare[length - 1] = 0;
		snprintf(call, CALL_ROOM, "%s", bare + 1);
		return;
	}
	snprintf(call, CALL_ROOM, "%s", bare);
}

static enum word word_of(const char *w, int *report) {
	if (w == NULL || w[0] == 0)
		return WORD_NONE;
	if (!strcmp(w, "RR73"))
		return WORD_RR73;
	if (!strcmp(w, "RRR"))
		return WORD_RRR;
	if (!strcmp(w, "73"))
		return WORD_73;
	if (is_grid(w))
		return WORD_GRID;
	if (is_report(w)) {
		*report = atoi(w);
		return WORD_REPORT;
	}
	if (w[0] == 'R' && is_report(w + 1)) {
		*report = atoi(w + 1);
		return WORD_ROGER_REPORT;
	}
	return WORD_OTHER;
}

// the parts of a message: CQ, perhaps a word after it, the callsign and
// the grid; or the callsign it is to, the one it is from, and a word
static void parse(struct heard *h, const char *text) {
	char copy[MESSAGE_ROOM];
	char *word[WORDS_MAX];
	char *saved = NULL;
	int count = 0;

	memset(h->to, 0, sizeof(h->to));
	memset(h->from, 0, sizeof(h->from));
	h->grid[0] = 0;
	h->extra[0] = 0;
	h->is_cq = 0;
	h->word = WORD_OTHER;
	h->report = 0;
	snprintf(copy, sizeof(copy), "%s", text);
	for (word[count] = strtok_r(copy, " ", &saved); word[count] != NULL && count < WORDS_MAX - 1;
			word[count] = strtok_r(NULL, " ", &saved))
		count++;
	if (count < 2)
		return;
	if (!strcmp(word[0], "CQ")) {
		h->is_cq = 1;
		snprintf(h->to, sizeof(h->to), "CQ");
		if (count >= 3 && is_grid(word[count - 1])) {
			callsign_of(h->from, word[count - 2]);
			snprintf(h->grid, sizeof(h->grid), "%s", word[count - 1]);
		}
		else
			callsign_of(h->from, word[count - 1]);
		h->word = WORD_NONE;
		return;
	}
	if (count > 3)
		return;
	callsign_of(h->to, word[0]);
	callsign_of(h->from, word[1]);
	if (count == 3) {
		h->word = word_of(word[2], &h->report);
		snprintf(h->extra, sizeof(h->extra), "%s", word[2]);
	}
	else
		h->word = WORD_NONE;
	if (h->word == WORD_GRID)
		snprintf(h->grid, sizeof(h->grid), "%s", word[2]);
}

// what the modem has copied since the last look is kept, a slot or two of it
static void read_messages(void) {
	struct message m;

	while (message_next(&last_read, &m)) {
		if (m.is_sent || strcmp(m.mode, "FT8"))
			continue;
		if (heard_count == HEARD_MAX) {
			memmove(heard, heard + 1, (HEARD_MAX - 1) * sizeof(heard[0]));
			heard_count--;
		}
		parse(&heard[heard_count], m.text);
		heard[heard_count].slot = m.at;
		heard[heard_count].snr = m.snr_db;
		heard_count++;
	}
}

/* the exchange, on the panel's status line */

static void exchange_show(void) {
	char line[EXCHANGE_ROOM];
	char count[WORD_ROOM];
	char piece[3 * WORD_ROOM];
	int i;

	line[0] = 0;
	if (qso.dx[0] == 0) {
		panel_show_qso("");
		return;
	}
	snprintf(line, sizeof(line), "%s", qso.dx);
	for (i = 0; i < exchange_count; i++) {
		count[0] = 0;
		if (exchange[i].count > 1)
			snprintf(count, sizeof(count), " x%d", exchange[i].count);
		snprintf(piece, sizeof(piece), " > %.*s%.*s", WORD_ROOM - 1, exchange[i].word,
			WORD_ROOM - 1, count);
		strncat(line, piece, sizeof(line) - strlen(line) - 1);
	}
	panel_show_qso(line);
}

static void exchange_begin(void) {
	exchange_count = 0;
	exchange_show();
}

// a word that has passed, ours or theirs; one that has passed before is
// counted again
static void exchange_add(const char *word) {
	int i;

	if (word == NULL || word[0] == 0 || qso.dx[0] == 0)
		return;
	for (i = 0; i < exchange_count; i++) {
		if (!strcmp(exchange[i].word, word)) {
			exchange[i].count++;
			exchange_show();
			return;
		}
	}
	if (exchange_count == EXCHANGE_WORDS_MAX)
		return;
	snprintf(exchange[exchange_count].word, sizeof(exchange[0].word), "%s", word);
	exchange[exchange_count].count = 1;
	exchange_count++;
	exchange_show();
}

static void exchange_end(void) {
	exchange_count = 0;
	panel_show_qso("");
}

/* sending */

static int clamped_report(int snr) {
	if (snr < REPORT_LOWEST)
		return REPORT_LOWEST;
	if (snr > REPORT_HIGHEST)
		return REPORT_HIGHEST;
	return snr;
}

static time_t slot_start(double t) {
	return (time_t)(t / SLOT_SECONDS) * SLOT_SECONDS;
}

static int parity_of(time_t slot) {
	return (int)((slot / SLOT_SECONDS) % 2);
}

// the first slot of the parity given that we can still send in: this one,
// if it began no more than LATE_START_S ago, or the next of that parity
static time_t first_slot_of(int parity) {
	double now = ft8_time_now();
	time_t slot = slot_start(now);

	if (parity_of(slot) == parity && now - (double)slot <= LATE_START_S)
		return slot;
	slot += SLOT_SECONDS;
	if (parity_of(slot) != parity)
		slot += SLOT_SECONDS;
	return slot;
}

// the message is sent in our next slot; the answer is looked for in the
// slot after it. A message sent again is counted. When it cannot be sent
// the operator is told, and nothing more is to be said of it
static int send(enum state state, const char *message) {
	if (qso.state != state || strcmp(qso.message, message) != 0)
		qso.sends = 0;
	qso.state = state;
	snprintf(qso.message, sizeof(qso.message), "%s", message);
	qso.sends++;
	if (ft8_tx_arm(message, control_get_int("TX_PITCH"), qso.next_slot) < 0) {
		warn("the message cannot be sent as it is written");
		ft8_tx_cancel();
		qso.state = IDLE;
		return -1;
	}
	message_add_sent("FT8", qso.next_slot, control_get_int("TX_PITCH"), message);
	if (state != CALLING_CQ)
		exchange_add(strrchr(message, ' ') + 1);
	qso.decide_slot = qso.next_slot + SLOT_SECONDS;
	qso.next_slot += 2 * SLOT_SECONDS;
	return 0;
}

static void send_cq(void) {
	char message[MESSAGE_ROOM];
	char call[CALL_ROOM];
	char grid[GRID_ROOM];
	char text[SAY_ROOM];

	my_call(call);
	my_grid(grid);
	snprintf(message, sizeof(message), "CQ %s %s", call, grid);
	qso.cq_calls++;
	if (send(CALLING_CQ, message) < 0)
		return;
	if (is_ops("NORMAL"))
		snprintf(text, sizeof(text), "CQ, %d of %d", qso.cq_calls, CQ_CALLS_NORMAL);
	else
		snprintf(text, sizeof(text), "CQ, %d", qso.cq_calls);
	say(text);
}

// a message to the station being worked, with the word given, or with our
// grid if there is none
static void send_to_dx(enum state state, const char *word) {
	char message[MESSAGE_ROOM];
	char call[CALL_ROOM];
	char grid[GRID_ROOM];
	char text[SAY_ROOM];

	my_call(call);
	my_grid(grid);
	if (word == NULL)
		word = grid;
	snprintf(message, sizeof(message), "%s %s %s", qso.dx, call, word);
	if (send(state, message) < 0)
		return;
	snprintf(text, sizeof(text), "%s: %s, %d of %d", qso.dx, word, qso.sends, sends_max());
	say(text);
}

static void send_report(enum state state, int is_roger) {
	char word[WORD_ROOM];

	if (is_roger)
		snprintf(word, sizeof(word), "R%+03d", qso.report_sent);
	else
		snprintf(word, sizeof(word), "%+03d", qso.report_sent);
	send_to_dx(state, word);
}

static void send_rogers(void) {
	if (!strcmp(control_get("FT8_REPLY"), "RRR"))
		send_to_dx(ROGERS, "RRR");
	else
		send_to_dx(ROGERS, "RR73");
}

/* logging, and the end of a contact */

static void log_the_contact(void) {
	struct logbook_row row;
	char call[CALL_ROOM];
	char text[SAY_ROOM];
	struct tm utc;
	time_t now = (time_t)ft8_time_now();

	if (qso.is_logged)
		return;
	qso.is_logged = 1;
	memset(&row, 0, sizeof(row));
	gmtime_r(&now, &utc);
	snprintf(row.mode, sizeof(row.mode), "FT8");
	snprintf(row.freq, sizeof(row.freq), "%d", atoi(qso.freq) / HZ_PER_KHZ);
	strftime(row.qso_date, sizeof(row.qso_date), "%Y-%m-%d", &utc);
	strftime(row.qso_time, sizeof(row.qso_time), "%H%M", &utc);
	snprintf(row.rst_sent, sizeof(row.rst_sent), "%+03d", qso.report_sent);
	my_grid(row.exch_sent);
	snprintf(row.callsign_recv, sizeof(row.callsign_recv), "%s", qso.dx);
	snprintf(row.rst_recv, sizeof(row.rst_recv), "%+03d", qso.report_received);
	snprintf(row.exch_recv, sizeof(row.exch_recv), "%s", qso.dx_grid);
	my_call(call);
	if (logbook_add(&row, call) < 0)
		snprintf(text, sizeof(text), "%s: the logbook could not be written", qso.dx);
	else
		snprintf(text, sizeof(text), "%s logged, sent %s, received %s", qso.dx,
			row.rst_sent, row.rst_recv);
	say(text);
}

// a contact ends, logged or not; RUNNING calls CQ again
static void end_contact(const char *why) {
	char text[SAY_ROOM];

	if (why != NULL) {
		snprintf(text, sizeof(text), "%s: %s", qso.dx, why);
		say(text);
	}
	snprintf(last_worked, sizeof(last_worked), "%s", qso.dx);
	last_worked_at = ft8_time_now();
	qso.dx[0] = 0;
	qso.dx_grid[0] = 0;
	qso.has_report = 0;
	qso.is_logged = 0;
	exchange_end();
	if (is_ops("RUNNING")) {
		qso.cq_calls = 0;
		send_cq();
		return;
	}
	qso.state = IDLE;
}

static void stop(const char *why) {
	ft8_tx_cancel();
	qso.state = IDLE;
	qso.dx[0] = 0;
	exchange_end();
	if (why != NULL)
		say(why);
}

// the message is sent again, unless it has been sent as often as it may be
static void send_again(void) {
	char message[MESSAGE_ROOM];
	char text[SAY_ROOM];

	if (qso.sends >= sends_max() && qso.is_logged) {
		end_contact("no answer, given up");
		return;
	}
	if (qso.sends >= sends_max()) {
		end_contact("no answer, given up, not logged");
		return;
	}
	if (qso.state == CALLING_CQ) {
		send_cq();
		return;
	}
	snprintf(message, sizeof(message), "%s", qso.message);
	if (send(qso.state, message) < 0)
		return;
	snprintf(text, sizeof(text), "%s: %s again, %d of %d", qso.dx,
		strrchr(qso.message, ' ') + 1, qso.sends, sends_max());
	say(text);
}

/* the states */

// a station has called us, or answered our CQ, and is now the one worked
static void take_the_station(const struct heard *h) {
	char call[CALL_ROOM];

	my_call(call);
	snprintf(qso.dx, sizeof(qso.dx), "%s", h->from);
	if (h->grid[0] != 0)
		snprintf(qso.dx_grid, sizeof(qso.dx_grid), "%s", h->grid);
	qso.report_sent = clamped_report(h->snr);
	qso.is_logged = 0;
	qso.has_report = 0;
	snprintf(qso.freq, sizeof(qso.freq), "%s", control_get("FREQ"));
	exchange_begin();
}

static void take_the_report(const struct heard *h) {
	if (qso.has_report)
		return;
	qso.report_received = h->report;
	qso.has_report = 1;
}

// what X's message to us does, in the states where an answer to ours is
// waited for: CALLING CQ (once X is taken), CALLING STN and REPORT
static void answered(const struct heard *h) {
	switch (h->word) {
	case WORD_REPORT:
		take_the_report(h);
		send_report(ROGER_REPORT, 1);
		break;
	case WORD_ROGER_REPORT:
		take_the_report(h);
		send_rogers();
		if (strcmp(control_get("FT8_REPLY"), "RRR") != 0)
			log_the_contact();
		break;
	case WORD_RRR:
	case WORD_RR73:
	case WORD_73:
		if (qso.has_report) {
			log_the_contact();
			end_contact(NULL);
		}
		else
			end_contact("ended by the other station, not logged");
		break;
	default:
		send_report(REPORT, 0);
		break;
	}
}

// the message from the station worked to us, in that slot, if there is one;
// the last, if there is more than one. With no station worked yet, the
// first to us
static const struct heard *from_dx(time_t slot) {
	const struct heard *found = NULL;
	char call[CALL_ROOM];
	int i;

	my_call(call);
	for (i = 0; i < heard_count; i++) {
		if (heard[i].slot != slot || strcmp(heard[i].to, call) != 0)
			continue;
		if (qso.dx[0] == 0)
			return &heard[i];
		if (!strcmp(heard[i].from, qso.dx))
			found = &heard[i];
	}
	return found;
}

// whether the station worked was heard answering another, in that slot
static int is_dx_busy(time_t slot) {
	char call[CALL_ROOM];
	int i;

	my_call(call);
	for (i = 0; i < heard_count; i++) {
		if (heard[i].slot == slot && !heard[i].is_cq && !strcmp(heard[i].from, qso.dx)
				&& strcmp(heard[i].to, call) != 0)
			return 1;
	}
	return 0;
}

// what the station's slot brought. The slot after it is ours, and the one
// after that is the station's again, whatever is done now
static void decide(time_t slot) {
	const struct heard *h = from_dx(slot);

	qso.next_slot = slot + SLOT_SECONDS;
	qso.decide_slot = slot + 2 * SLOT_SECONDS;
	if (h != NULL && qso.state == CALLING_CQ)
		take_the_station(h);
	if (h != NULL)
		exchange_add(h->extra);
	switch (qso.state) {
	case CALLING_CQ:
		if (h != NULL)
			answered(h);
		else if (is_ops("NORMAL") && qso.cq_calls >= CQ_CALLS_NORMAL)
			stop("no answer to CQ");
		else
			send_cq();
		break;
	case CALLING_STN:
	case WAITING:
		if (h != NULL && h->word == WORD_GRID)
			send_report(REPORT, 0);
		else if (h != NULL)
			answered(h);
		else if (is_dx_busy(slot)) {
			ft8_tx_cancel();
			qso.state = WAITING;
			qso.waits = 0;
			say("answering another station, waiting");
		}
		else if (qso.state == WAITING) {
			qso.waits++;
			if (qso.waits >= WAIT_SLOTS_MAX)
				end_contact("did not come back, given up");
		}
		else
			send_again();
		break;
	case REPORT:
		if (h != NULL && h->word == WORD_GRID)
			send_again();
		else if (h != NULL)
			answered(h);
		else
			send_again();
		break;
	case ROGER_REPORT:
		if (h != NULL && (h->word == WORD_RRR || h->word == WORD_RR73)) {
			log_the_contact();
			send_to_dx(SIGNOFF, "73");
		}
		else if (h != NULL && h->word == WORD_73) {
			log_the_contact();
			end_contact(NULL);
		}
		else
			send_again();
		break;
	case ROGERS:
		if (h != NULL && h->word == WORD_73) {
			log_the_contact();
			end_contact(NULL);
		}
		else if (h != NULL && h->word == WORD_ROGER_REPORT)
			send_again();
		else if (qso.is_logged)
			end_contact(NULL);
		else if (++qso.quiet_slots >= QUIET_SLOTS_AFTER_RRR) {
			log_the_contact();
			end_contact(NULL);
		}
		break;
	case SIGNOFF:
		if (h != NULL && (h->word == WORD_RR73 || h->word == WORD_RRR) && qso.sends < 2)
			send_to_dx(SIGNOFF, "73");
		else
			end_contact(NULL);
		break;
	default:
		break;
	}
}

/* what starts a contact */

// the slots CQ is called in: those at :00 and :30 with TX1ST ON, the
// others with it OFF
static int cq_parity(void) {
	if (!strcmp(control_get("FT8_TX1ST"), "ON"))
		return FIRST_SLOTS;
	return SECOND_SLOTS;
}

static void call_cq(void) {
	if (is_ops("POUNCING")) {
		say("pouncing: CQ is not called");
		return;
	}
	if (!is_station_set())
		return;
	ft8_tx_cancel();
	memset(&qso, 0, sizeof(qso));
	snprintf(qso.freq, sizeof(qso.freq), "%s", control_get("FREQ"));
	qso.next_slot = first_slot_of(cq_parity());
	send_cq();
}

// the station of a message heard is answered: its CQ, the end of its last
// contact, or its call to us
static int answer(const struct heard *h, time_t slot, int is_auto) {
	char call[CALL_ROOM];
	int is_tail;
	int is_to_us;

	my_call(call);
	is_to_us = !strcmp(h->to, call);
	is_tail = !h->is_cq && !is_to_us
		&& (h->word == WORD_RR73 || h->word == WORD_73 || h->word == WORD_RRR);
	if (h->from[0] == 0 || !strcmp(h->from, "...") || !strcmp(h->from, call))
		return -1;
	if (is_ops("POUNCING") && !h->is_cq && !is_auto) {
		say("pouncing: only a CQ is answered");
		return -1;
	}
	if (!h->is_cq && !is_tail && !is_to_us) {
		say("that message cannot be answered");
		return -1;
	}
	if (!is_station_set())
		return -1;
	ft8_tx_cancel();
	memset(&qso, 0, sizeof(qso));
	take_the_station(h);
	if (is_to_us)
		exchange_add(h->extra);
	qso.next_slot = first_slot_of(1 - parity_of(slot));
	if (is_to_us && h->word != WORD_GRID && h->word != WORD_NONE) {
		answered(h);
		return 0;
	}
	if (is_to_us)
		send_report(REPORT, 0);
	else
		send_to_dx(CALLING_STN, NULL);
	return 0;
}

// a line from the panel: the second of the minute, the strength, the pitch
// and the text, with | between them
int ft8_qso_answer(const char *line) {
	char copy[MESSAGE_ROOM + WORD_ROOM];
	struct heard h;
	char *field[4];
	char *saved = NULL;
	int seconds;
	time_t slot;
	int i;

	snprintf(copy, sizeof(copy), "%s", line);
	field[0] = strtok_r(copy, "|", &saved);
	for (i = 1; i < 4; i++)
		field[i] = strtok_r(NULL, "|", &saved);
	if (field[3] == NULL || !strcmp(field[1], "TX"))
		return -1;
	seconds = atoi(field[0] + strlen(field[0]) - SECONDS_DIGITS);
	parse(&h, field[3]);
	h.snr = atoi(field[1]);
	slot = slot_start(ft8_time_now());
	if (parity_of(slot) != (seconds / SLOT_SECONDS) % 2)
		slot -= SLOT_SECONDS;
	h.slot = slot;
	return answer(&h, slot, 0);
}

// the newest message heard from the station that may be answered
int ft8_qso_answer_call(const char *callsign) {
	char call[CALL_ROOM];
	int i;

	upper(call, sizeof(call), callsign);
	read_messages();
	for (i = heard_count - 1; i >= 0; i--) {
		if (!strcmp(heard[i].from, call))
			return answer(&heard[i], heard[i].slot, 0);
	}
	say("nothing heard from that station");
	return -1;
}

// with AUTO on and no contact under way: the first station in the slot
// just searched that calls us is answered
static void answer_a_call(time_t slot) {
	char call[CALL_ROOM];
	int i;

	my_call(call);
	for (i = 0; i < heard_count; i++) {
		const struct heard *h = &heard[i];

		if (h->slot != slot || h->is_cq || strcmp(h->to, call) != 0)
			continue;
		if (h->word != WORD_GRID && h->word != WORD_REPORT && h->word != WORD_NONE)
			continue;
		if (!strcmp(h->from, last_worked) && ft8_time_now() - last_worked_at < AUTO_QUIET_FOR_S)
			continue;
		if (answer(h, slot, 1) == 0)
			return;
	}
}

/* the controls */

static int apply_ft8_do(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)why;
	(void)room;
	if (!strcmp(value, "CQ"))
		call_cq();
	if (!strcmp(value, "HALT"))
		stop("stopped");
	return 0;
}

static struct control ft8_ops = {
	.group = "STATION", .name = "FT8_OPS", .kind = CONTROL_CHOICE,
	.choices = "NORMAL/RUNNING/POUNCING", .value = "NORMAL",
};

static struct control ft8_reply = {
	.group = "STATION", .name = "FT8_REPLY", .kind = CONTROL_CHOICE,
	.choices = "RR73/RRR", .value = "RR73",
};

static struct control tx_pitch = {
	.group = "FT8", .name = "TX_PITCH", .kind = CONTROL_NUMBER,
	.min = TX_PITCH_LOWEST, .max = TX_PITCH_HIGHEST, .step = TX_PITCH_STEP,
	.value = "1500",
};

static struct control ft8_auto = {
	.group = "FT8", .name = "FT8_AUTO", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON", .value = "OFF",
};

static struct control ft8_repeat = {
	.group = "FT8", .name = "FT8_REPEAT", .kind = CONTROL_NUMBER,
	.min = REPEAT_LOWEST, .max = REPEAT_HIGHEST, .step = 1, .value = "5",
};

static struct control ft8_tx1st = {
	.group = "FT8", .name = "FT8_TX1ST", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON", .value = "ON",
};

static struct control ft8_do = {
	.group = "FT8", .name = "FT8_DO", .kind = CONTROL_ACTION,
	.choices = "CQ/HALT", .apply = apply_ft8_do,
};

void ft8_qso_open(void) {
	control_register(&ft8_ops);
	control_register(&ft8_reply);
	control_register(&tx_pitch);
	control_register(&ft8_auto);
	control_register(&ft8_repeat);
	control_register(&ft8_tx1st);
	control_register(&ft8_do);
}

// from the thread that owns the controls, many times a second: a contact
// ends if the mode or the frequency changes under it, and the station's
// slot is looked at once it has been searched, or once it is too late to
// wait for that
void ft8_qso_poll(void) {
	static char remembered[CALL_ROOM];
	char call[CALL_ROOM];
	time_t searched;

	//our callsign is known by its hash, so that a message to us that sends
	//it as a hash is seen to be to us
	my_call(call);
	if (strcmp(call, remembered) != 0) {
		snprintf(remembered, sizeof(remembered), "%s", call);
		ft8_callsign_remember(call);
	}
	read_messages();
	searched = ft8_searched_slot();
	if (qso.state == IDLE && searched != auto_slot) {
		auto_slot = searched;
		if (!strcmp(control_get("FT8_AUTO"), "ON") && !strcmp(control_get("MODE"), "FT8"))
			answer_a_call(searched);
	}
	if (qso.state == IDLE)
		return;
	if (strcmp(control_get("MODE"), "FT8") != 0) {
		stop("the mode was changed: stopped");
		return;
	}
	if (strcmp(control_get("FREQ"), qso.freq) != 0) {
		stop("the frequency was changed: stopped");
		return;
	}
	if (searched != NOTHING_YET && searched >= qso.decide_slot) {
		decide(qso.decide_slot);
		return;
	}
	if (ft8_time_now() >= (double)qso.decide_slot + DECIDED_BY_S)
		decide(qso.decide_slot);
}
