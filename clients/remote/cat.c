// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// cat.c — what another program asks of the radio in Hamlib's words, answered.
//
// WHAT IT IS FOR
//
// Programs that work a radio, WSJT-X, FreeDV, fldigi, the loggers, set its
// frequency and key it through Hamlib, and Hamlib has a way of doing that
// over a network connection: its "NET rigctl", radio number 2 in its list,
// which is what its own rigctld speaks, on port 4532. The author asked that
// the gateway answer it, so that such a program finds this radio as it
// would any other. The old sbitx program answered it too, in hamlib.c, with
// what gqrx answers; the list of what the radio can do, below, began there.
//
// The gateway, zbitx_remote.c, has the connections; this file has only the
// words, so that they can be tested without a radio or a network. It is
// given one line a program sent and what is known of the radio, and gives
// back what to answer and what, if anything, to tell the radio program.
//
// WHAT IS UNDERSTOOD
//
// A line is one command, by its letter or by its long name after a
// backslash, as Hamlib sends them:
//
//     f   \get_freq         the frequency, in Hz
//     F   \set_freq  HZ
//     m   \get_mode         the mode, and under it the width in Hz
//     M   \set_mode  MODE WIDTH
//     v   \get_vfo          VFOA or VFOB
//     V   \set_vfo   VFO   accepted, and the radio left as it is
//     t   \get_ptt          1 while sending, 0 while not
//     T   \set_ptt   0 or 1
//     s   \get_split_vfo    0, and under it the VFO
//     S   \set_split_vfo  0 VFO
//     _   \get_info
//     q   Q                 the program is leaving
//     \chk_vfo  \dump_state  \get_powerstat  \get_lock_mode
//
// A command that sets something is answered "RPRT 0", or "RPRT" and one of
// Hamlib's numbers for what went wrong: -1 for a value that cannot be, -11
// for what this radio does not do. Some programs name the VFO in every
// command, "F VFOA 14074000"; the name is passed over, the command being
// for the VFO in use.
//
// THE MODES
//
// The radio's USB, LSB, CW, CWR and AM are Hamlib's by the same names.
// Hamlib's PKTUSB and PKTLSB, which a program asks for when it is going to
// send its own tones, are the radio's USB and LSB: the tones come in as
// sound and are sent as speech would be. The radio's FT8 is its own
// working of FT8, with no other program in it; it is said to be PKTUSB,
// that being the nearest of Hamlib's. The width said with a mode is a round
// figure for it and not what the filter is set to; a width asked for is not
// acted on, the filters being set at the radio.
//
// THE VFO
//
// Which VFO is in use is said, and choosing one is accepted but changes
// nothing. Hamlib, each time a program connects through it, chooses VFO B
// and then A again, only to read what each is set to; a radio that did as
// it was told would jump to its other frequency and back whenever a
// program was started. The old sbitx program let it pass in the same way.
// The VFO is chosen at the radio.
//
// SPLIT
//
// Sending on one frequency and listening on another is done by whichever
// client is working the radio, which moves the frequency as it keys; the
// radio program has no such setting. So split is always said to be off,
// and turning it on is refused.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cat.h"

#define RPRT_OK "RPRT 0\n"
#define RPRT_INVALID "RPRT -1\n"
#define RPRT_NOT_DONE "RPRT -11\n"
#define HZ_LEAST 500000
#define HZ_MOST 30000000
#define WORD_ROOM 32
#define WORDS_MAX 4
#define WIDTH_CW_HZ 500
#define WIDTH_AM_HZ 6000
#define WIDTH_SPEECH_HZ 3000

// what the radio can do, as Hamlib's rigctld would say it of a radio: the
// protocol's version, the radio's number in Hamlib's list, the ITU region;
// the frequencies it receives on and those it sends on, each with its
// modes, its least and greatest power in mW, and its VFOs; the tuning
// steps; the filters; and a run of noughts for the things it has none of.
// The modes are a sum: AM 0x1, CW 0x2, USB 0x4, LSB 0x8, CWR 0x80, PKTLSB
// 0x400, PKTUSB 0x800.
static const char state_dump[] =
	"0\n"
	"2\n"
	"1\n"
	"500000 30000000 0xc8f -1 -1 0x3 0x0\n"
	"0 0 0 0 0 0 0\n"
	"500000 30000000 0xc8f 1000 40000 0x3 0x0\n"
	"0 0 0 0 0 0 0\n"
	"0xc8f 1\n"
	"0 0\n"
	"0x82 500\n"
	"0x1 6000\n"
	"0xc0c 3000\n"
	"0 0\n"
	"0\n"
	"0\n"
	"0\n"
	"0\n"
	"0\n"
	"0\n"
	"0\n"
	"0\n"
	"0\n"
	"0\n"
	"0\n"
	"0\n";

struct name {
	const char *long_name;
	char letter;
};

static const struct name names[] = {
	{ "get_freq", 'f' }, { "set_freq", 'F' },
	{ "get_mode", 'm' }, { "set_mode", 'M' },
	{ "get_vfo", 'v' }, { "set_vfo", 'V' },
	{ "get_ptt", 't' }, { "set_ptt", 'T' },
	{ "get_split_vfo", 's' }, { "set_split_vfo", 'S' },
	{ "get_info", '_' }, { "quit", 'q' },
	{ "chk_vfo", 'c' }, { "dump_state", 'd' },
	{ "get_powerstat", 'p' }, { "get_lock_mode", 'k' },
};

// the letter a command goes by here, or 0 for one that is not understood.
// The four that have only long names are given letters of this file's own,
// which stand for nothing when they come alone
static char letter_of(const char *command) {
	size_t i;

	if (command[0] != '\\') {
		if (command[1] != 0 || strchr("fFmMvVtTsS_qQ", command[0]) == NULL)
			return 0;
		return command[0] == 'Q' ? 'q' : command[0];
	}
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (!strcmp(command + 1, names[i].long_name))
			return names[i].letter;
	return 0;
}

static int is_vfo_name(const char *word) {
	return !strncmp(word, "VFO", 3) || !strcmp(word, "currVFO") || !strcmp(word, "Main")
		|| !strcmp(word, "Sub");
}

static const char *mode_said(const char *mode) {
	if (!strcmp(mode, "FT8") || !strcmp(mode, "JTTY"))
		return "PKTUSB";
	if (!strcmp(mode, "2TONE") || mode[0] == 0)
		return "USB";
	return mode;
}

static int width_of(const char *mode) {
	if (!strncmp(mode, "CW", 2))
		return WIDTH_CW_HZ;
	if (!strcmp(mode, "AM"))
		return WIDTH_AM_HZ;
	return WIDTH_SPEECH_HZ;
}

// the radio's mode for one of Hamlib's, or NULL where it has none
static const char *mode_asked(const char *mode) {
	static const char *const same[] = { "USB", "LSB", "CW", "CWR", "AM" };
	size_t i;

	if (!strcmp(mode, "PKTUSB"))
		return "USB";
	if (!strcmp(mode, "PKTLSB"))
		return "LSB";
	for (i = 0; i < sizeof(same) / sizeof(same[0]); i++)
		if (!strcmp(mode, same[i]))
			return same[i];
	return NULL;
}

// One line a program sent is answered. answer has CAT_ANSWER_ROOM bytes and
// for_radio CAT_FOR_RADIO_ROOM; for_radio is left empty, or is a line for
// the radio program, and what is known of the radio is changed to match,
// the radio program not saying a change back to the one that made it.
// CAT_QUIT says the program is leaving, and there is then no answer.
int cat_answer(const char *asked, struct cat_radio *radio, char *answer, char *for_radio) {
	char words[WORDS_MAX][WORD_ROOM];
	const char *mode;
	char *value;
	char *end;
	double hz;
	int count;
	char letter;

	answer[0] = 0;
	for_radio[0] = 0;
	memset(words, 0, sizeof(words));
	count = sscanf(asked, "%31s %31s %31s %31s", words[0], words[1], words[2], words[3]);
	if (count < 1)
		return CAT_ANSWERED;
	letter = letter_of(words[0]);
	// the VFO, where a program names it, is passed over; but not where the
	// VFO is what is being chosen
	value = words[1];
	if (letter != 'V' && is_vfo_name(words[1]))
		value = words[2];

	switch (letter) {
	case 'f':
		snprintf(answer, CAT_ANSWER_ROOM, "%ld\n", radio->hz);
		break;
	case 'F':
		hz = strtod(value, &end);
		if (end == value || hz < HZ_LEAST || hz > HZ_MOST) {
			snprintf(answer, CAT_ANSWER_ROOM, "%s", RPRT_INVALID);
			break;
		}
		radio->hz = lround(hz);
		snprintf(for_radio, CAT_FOR_RADIO_ROOM, "FREQ %ld\n", radio->hz);
		snprintf(answer, CAT_ANSWER_ROOM, "%s", RPRT_OK);
		break;
	case 'm':
		snprintf(answer, CAT_ANSWER_ROOM, "%s\n%d\n", mode_said(radio->mode),
			width_of(radio->mode));
		break;
	case 'M':
		mode = mode_asked(value);
		if (mode == NULL) {
			snprintf(answer, CAT_ANSWER_ROOM, "%s", RPRT_INVALID);
			break;
		}
		// a radio working FT8 by itself is left to it by a program that
		// asks only for what FT8 is said to be
		if (strcmp(mode, radio->mode)
				&& !(!strcmp(radio->mode, "FT8") && !strcmp(value, "PKTUSB"))) {
			snprintf(radio->mode, sizeof(radio->mode), "%s", mode);
			snprintf(for_radio, CAT_FOR_RADIO_ROOM, "MODE %s\n", mode);
		}
		snprintf(answer, CAT_ANSWER_ROOM, "%s", RPRT_OK);
		break;
	case 'v':
		snprintf(answer, CAT_ANSWER_ROOM, "VFO%c\n", radio->vfo == 'B' ? 'B' : 'A');
		break;
	case 'V':
		// accepted and not acted on; THE VFO, above, says why
		snprintf(answer, CAT_ANSWER_ROOM, "%s",
			strcmp(value, "VFOA") && strcmp(value, "VFOB") && strcmp(value, "currVFO")
			? RPRT_INVALID : RPRT_OK);
		break;
	case 't':
		snprintf(answer, CAT_ANSWER_ROOM, "%d\n", radio->is_sending ? 1 : 0);
		break;
	case 'T':
		// Hamlib's 1, 2 and 3 are all "send", by the microphone or by the
		// data input, which here are the one way in
		if (value[0] < '0' || value[0] > '3' || value[1] != 0) {
			snprintf(answer, CAT_ANSWER_ROOM, "%s", RPRT_INVALID);
			break;
		}
		radio->is_sending = value[0] != '0';
		snprintf(for_radio, CAT_FOR_RADIO_ROOM, "TX %s\n", radio->is_sending ? "ON" : "OFF");
		snprintf(answer, CAT_ANSWER_ROOM, "%s", RPRT_OK);
		break;
	case 's':
		snprintf(answer, CAT_ANSWER_ROOM, "0\nVFO%c\n", radio->vfo == 'B' ? 'B' : 'A');
		break;
	case 'S':
		snprintf(answer, CAT_ANSWER_ROOM, "%s", strcmp(value, "0") ? RPRT_NOT_DONE : RPRT_OK);
		break;
	case '_':
		snprintf(answer, CAT_ANSWER_ROOM, "zbitxre\n");
		break;
	case 'c':
		// "0": this end does not want the VFO named in every command
		snprintf(answer, CAT_ANSWER_ROOM, "0\n");
		break;
	case 'd':
		snprintf(answer, CAT_ANSWER_ROOM, "%s", state_dump);
		break;
	case 'p':
		snprintf(answer, CAT_ANSWER_ROOM, "1\n");
		break;
	case 'k':
		snprintf(answer, CAT_ANSWER_ROOM, "0\n");
		break;
	case 'q':
		return CAT_QUIT;
	default:
		snprintf(answer, CAT_ANSWER_ROOM, "%s", RPRT_NOT_DONE);
		break;
	}
	return CAT_ANSWERED;
}
