// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// cw_reader.c — CW read from the spectrum, the author's way.
//
// The author can read CW off the waterfall, and set out how a decoder
// should do it, which is what this is:
//
//   - each bin of each block of the spectrum is a single amplitude;
//   - the noise is the grass, which stays within a few dB of the least, and
//     a signal is a spike that pokes out of it: the ratio of a bin to the
//     grass, its signal to noise, says which is which, and is the envelope;
//   - the stronger the signal the higher its pedestal, and the threshold
//     that makes the envelope into marks and spaces follows the pedestal;
//   - the speed is learned from the dots and the dashes both, but only
//     within a span of 1 to 2 around the speed set, WPM: at 20, from 15 to
//     30, since over a wider span a fast sender's dash is a slow one's dot;
//   - a new sender's speed is not known until enough of it has been heard,
//     so the start of each over is kept until it is, and then decoded from
//     the beginning: the decoder is rewound, and the first letters are not
//     lost while it learns, as zbitxv2's lost them.
//
// It was worked out on a recording of a CW net on 40 m, 41 minutes of it,
// with fading and rising noise, played through it with
// tests/play_cw_record.c; on that it read more of the net's words than
// zbitxv2's decoder did, with a tenth of the stray letters. It is given,
// each tick of 10.7 ms, the ratio to the grass of the bins around the
// pitch, CW_READER_SPAN each side; modem_cw.c works them out from the
// receiver's own spectrum, a block at a time.
//
// THE STATION
//
// In a net the stations are not all on the pitch; one 140 Hz from it was
// what made zbitxv2's decoder go off the rails when it came on. So the
// reader follows the station within the span: for each bin it keeps the
// tops of its level, followed up quickly and down slowly, and the bottoms,
// followed down quickly and up in about a third of a second. A station
// keying CW swings between them, its bottoms dropping in every space; a
// carrier that is not keyed does not, its bottoms soon up at its tops, and
// the grass swings only a little. Of the bins that swing KEYED_SWING_DB or
// more, the one with the highest tops is listened to; another is taken
// only when its tops are SWITCH_DB higher, so that the reader does not hop
// about, or when the one listened to has stopped swinging. The envelope is
// that bin and the stronger of its two neighbours, in dB, averaged over
// three ticks, which takes out the spikes of the grass, a tick or two long,
// and keeps a dot at 30 words a minute, four ticks long.
//
// MARKS AND SPACES
//
// Two levels are followed on the envelope. The noise level is the grass:
// it follows the envelope down quickly and up slowly. The pedestal is the
// tops of the marks: it follows them up quickly and sinks slowly, over a
// couple of seconds, so that a weaker station after a strong one is
// followed. A mark begins above the middle of the two and ends below it,
// with HYSTERESIS_DB either side so that it does not chatter, and the
// middle is never less than half the squelch above the noise. While the
// pedestal is less than SQUELCH_DB over the noise there is no station, and
// there are no marks. The time of each edge is found between ticks, from
// where the envelope crossed the threshold, so that marks and spaces are
// measured more finely than the ticks.
//
// THE SPEED, AND THE REWIND
//
// The dot's length is the one that fits the marks and the shorter spaces
// best, the marks to one dot or three, the spaces to one or three: each
// length within the span is tried, and the one with the least error,
// relative to what it is fitted to, is taken. Over that span a dot and a
// dash cannot be taken for each other.
//
// An over is the marks and spaces between silences of SILENCE_S. Until
// TRAIN_MARKS marks of it have come, nothing is read; then the speed is
// fitted to them all, and the over is read from its beginning. After that
// each letter is read as its space ends, and the speed is fitted again to
// the last FIT_RUNS marks and spaces every TRAIN_MARKS marks. An over of
// fewer than MIN_MARKS marks is a burst of noise, and is not read at all.
//
// Before a letter is read, a mark or a space shorter than GLITCH_DOTS of a
// dot is taken to be noise and joined to what is around it. A mark under
// two dots is a dot, over it a dash; a space over two dots ends a letter,
// over WORD_DOTS a word: less than the seven of the book, since most
// operators send their word spaces short. A letter not in the table is
// shown as '*'.
//
// THE WORDS OF CW
//
// The author's list of the common abbreviations of CW, known_words, is used
// to guess words that were read wrong: each is taken as one longer symbol,
// its dots and dashes with the letter gaps in their places. What goes wrong
// in a word is mostly a gap: a gap inside a letter taken for one between
// letters, which splits the letter, or the other way, which runs two
// letters into one that is not in the table. The dots and dashes are
// mostly right. So when a word has ended and is not a known word, the known
// words with the same dots and dashes are tried, and each gap that one of
// them puts on the other side of the line costs how near the line it was,
// in dots; the one that costs least is said, if it costs less than
// GUESS_MOST_DOTS, and the word as read if none does. Callsigns read
// cleanly twice, with the pattern of a callsign, are learned as known
// words, the last CW_READER_LEARNED_MAX of them, so that one sent again and
// read badly is known.
//
// Each letter is said the moment it is read, so that the text runs as the
// station sends it; a guess is said after the word, in square brackets, as
// the author asked: "VU2E HA [VU2EHA]". The word before is kept until the
// next has ended, and the two are tried joined, across the gap between
// them, at a cost of how far that gap was over the line for a word; that
// is how "VU2E HA" is guessed to be VU2EHA, once VU2EHA has been read
// cleanly. A word of one or two of the shortest letters, E, T and I, is
// what noise is read as, and is not learned from.
//
// On the recording of the net, the guessing brought the known words found
// in the long part of it from 485 to 515, and the unknown letters from 116
// to 109.
//
// For the audio thread, a tick at a time; it keeps all it needs in the
// struct cw_reader, and its text waits there for cw_reader_take().

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cw_reader.h"

#define SWING_FAST 0.2f
#define SWING_SLOW 0.01f
#define BOTTOM_UP 0.03f
#define SWITCH_DB 3.0f
#define KEYED_SWING_DB 6.0f
#define SMOOTH_TICKS 3
#define NOISE_DOWN 0.1f
#define NOISE_UP 0.01f
#define PEDESTAL_UP 0.3f
#define PEDESTAL_DECAY 0.005f
#define NO_STATION_DB 6.0f
#define SQUELCH_DB 8.0f
#define HYSTERESIS_DB 1.5f
#define FLOOR_RATIO 1e-12f
#define STARTING_NOISE_DB 3.0f
#define SILENCE_S 1.5
#define MIN_MARKS 4
#define TRAIN_MARKS 8
#define FIT_RUNS 120
#define FIT_STEPS 80
#define SLOWEST 0.75
#define FASTEST 1.5
#define SECONDS_A_DOT_AT_1_WPM 1.2
#define DOTS_A_DASH 3.0
#define LONGEST_SPACE_FITTED 5.0
#define WORST_ERROR 1.0
#define GLITCH_DOTS 0.4
#define DOT_OR_DASH 2.0
#define WORD_DOTS 4.5
#define UNKNOWN_LETTER "*"
// a guess is not made when the gaps that differ add up to more than this,
// in dots
#define GUESS_MOST_DOTS 1.0
#define GUESS_NONE 1.0e9
#define PREFIX_LONGEST 4
#define SEEN_TO_BE_KNOWN 2
#define SUFFIX_LONGEST 4
#define NOISE_WORD_LONGEST 2

struct morse {
	const char *code;
	const char *letter;
};

static const struct morse morse[] = {
	{".-", "A"}, {"-...", "B"}, {"-.-.", "C"}, {"-..", "D"}, {".", "E"},
	{"..-.", "F"}, {"--.", "G"}, {"....", "H"}, {"..", "I"}, {".---", "J"},
	{"-.-", "K"}, {".-..", "L"}, {"--", "M"}, {"-.", "N"}, {"---", "O"},
	{".--.", "P"}, {"--.-", "Q"}, {".-.", "R"}, {"...", "S"}, {"-", "T"},
	{"..-", "U"}, {"...-", "V"}, {".--", "W"}, {"-..-", "X"}, {"-.--", "Y"},
	{"--..", "Z"}, {"-----", "0"}, {".----", "1"}, {"..---", "2"},
	{"...--", "3"}, {"....-", "4"}, {".....", "5"}, {"-....", "6"},
	{"--...", "7"}, {"---..", "8"}, {"----.", "9"}, {".-.-.-", "."},
	{"--..--", ","}, {"..--..", "?"}, {"-..-.", "/"}, {"-...-", "<BT>"},
	{".-.-.", "<AR>"}, {"-....-", "-"}, {".--.-.", "@"}, {"...-.-", "<SK>"},
	{"-.--.", "<KN>"}, {".-...", "<AS>"}, {"........", "<HH>"}, {"-.--.-", ")"},
};

// the words of CW, which a word read is matched against: the author's list
// of the common abbreviations
static const char *const known_words[] = {
	"CQ", "DE", "K", "UR", "73", "TNX", "RST", "R", "TU", "BT", "OM", "FB", "PSE",
	"NAME", "QTH", "RIG", "ANT", "WX", "HR", "FER", "ES", "BK", "CUL", "AGN",
	"GUD", "HPE", "NW", "QSL", "QRM", "QRN", "QRP", "DX", "HW", "SRI", "STN", "OP",
	"WKD", "HI", "GA", "GE", "GM", "DR", "GL", "SN", "TMW", "VY", "WID", "XYL",
	"YL", "QRL", "QRZ", "QSB", "QSY", "QRT", "QRU", "QRV", "QRQ", "QRS", "AR",
	"KN", "SK", "AS", "HH", "5NN", "BCNU", "CU", "CUAGN", "RPT", "CFM", "PWR",
	"ABT", "B4", "OB", "OC", "OT", "RX", "TX", "TKS", "TRX", "XCVR", "XMTR", "HF",
	"VHF", "UHF", "CW", "SSB", "FM", "RTTY", "FT8", "POTA", "SOTA", "SIG", "SIGS",
	"CONDX", "TEMP", "DEG", "CL", "CLG", "CLD", "CBA",
};
#define KNOWN_COUNT ((int)(sizeof(known_words) / sizeof(known_words[0])))
#define MORSE_COUNT ((int)(sizeof(morse) / sizeof(morse[0])))

static void say(struct cw_reader *r, const char *text) {
	while (*text != 0 && r->text_count < CW_READER_TEXT_ROOM - 1)
		r->text[r->text_count++] = *text++;
}

static const char *code_of(char letter) {
	char one[2] = { letter, 0 };
	int i;

	for (i = 0; i < MORSE_COUNT; i++) {
		if (!strcmp(morse[i].letter, one))
			return morse[i].code;
	}
	return NULL;
}

static const char *letter_of(const char *code) {
	int i;

	for (i = 0; i < MORSE_COUNT; i++) {
		if (!strcmp(code, morse[i].code))
			return morse[i].letter;
	}
	return UNKNOWN_LETTER;
}

/* the station, and its envelope */

static float envelope_of(struct cw_reader *r, const float *ratio) {
	int neighbour;
	int best = 1;
	int is_keyed;
	float sum = 0;
	float db;
	int i;

	for (i = 0; i < CW_READER_BINS; i++) {
		db = 10.0f * log10f(ratio[i] + FLOOR_RATIO);
		if (r->recent_count == 0) {
			r->top_db[i] = db;
			r->bottom_db[i] = db;
		}
		if (db > r->top_db[i])
			r->top_db[i] += SWING_FAST * (db - r->top_db[i]);
		else
			r->top_db[i] += SWING_SLOW * (db - r->top_db[i]);
		if (db < r->bottom_db[i])
			r->bottom_db[i] += SWING_FAST * (db - r->bottom_db[i]);
		else
			r->bottom_db[i] += BOTTOM_UP * (db - r->bottom_db[i]);
	}
	//the station is the bin with the highest tops among those that swing:
	//a carrier that is not keyed stays at its top, and is passed over
	//a station that no longer swings gives way to the best that does
	best = r->station;
	is_keyed = r->top_db[best] - r->bottom_db[best] >= KEYED_SWING_DB;
	for (i = 1; i < CW_READER_BINS - 1; i++) {
		if (r->top_db[i] - r->bottom_db[i] < KEYED_SWING_DB)
			continue;
		if (!is_keyed || r->top_db[i] > r->top_db[best] + SWITCH_DB) {
			best = i;
			is_keyed = 1;
		}
		else if (r->top_db[i] > r->top_db[best] && best != r->station)
			best = i;
	}
	r->station = best;
	neighbour = r->station + 1;
	if (ratio[r->station - 1] > ratio[r->station + 1])
		neighbour = r->station - 1;
	r->recent_db[r->recent_count % SMOOTH_TICKS] =
		10.0f * log10f((ratio[r->station] + ratio[neighbour]) / 2 + FLOOR_RATIO);
	r->recent_count++;
	for (i = 0; i < SMOOTH_TICKS; i++)
		sum += r->recent_db[i];
	return sum / SMOOTH_TICKS;
}

static void follow_the_levels(struct cw_reader *r, float db) {
	if (!r->is_levels_started) {
		r->noise_db = STARTING_NOISE_DB;
		r->pedestal_db = STARTING_NOISE_DB;
		r->is_levels_started = 1;
	}
	if (db < r->noise_db)
		r->noise_db += NOISE_DOWN * (db - r->noise_db);
	else if (db < (r->noise_db + r->pedestal_db) / 2
			|| r->pedestal_db - r->noise_db < NO_STATION_DB)
		r->noise_db += NOISE_UP * (db - r->noise_db);
	if (db > r->pedestal_db)
		r->pedestal_db += PEDESTAL_UP * (db - r->pedestal_db);
	else
		r->pedestal_db += PEDESTAL_DECAY * (r->noise_db - r->pedestal_db);
}

/* reading an over */

static double fit_error(const struct cw_reader_run *runs, int count, double unit) {
	double error = 0;
	double e1;
	double e3;
	int fitted = 0;
	int i;

	for (i = 0; i < count; i++) {
		if (!runs[i].is_mark && runs[i].seconds > LONGEST_SPACE_FITTED * unit)
			continue;
		e1 = fabs(runs[i].seconds - unit) / unit;
		e3 = fabs(runs[i].seconds - DOTS_A_DASH * unit) / (DOTS_A_DASH * unit);
		if (e3 < e1)
			e1 = e3;
		if (e1 > WORST_ERROR)
			e1 = WORST_ERROR;
		error += e1;
		fitted++;
	}
	if (fitted == 0)
		return WORST_ERROR;
	return error / fitted;
}

// the dot's length that fits the marks and the spaces best, within the
// span around WPM
static double fit_the_unit(const struct cw_reader *r, int from, int to) {
	double set = SECONDS_A_DOT_AT_1_WPM / r->wpm;
	double shortest = set / FASTEST;
	double longest = set / SLOWEST;
	double best = set;
	double best_error = WORST_ERROR + 1;
	double unit;
	double error;
	int step;

	if (to - from > FIT_RUNS)
		from = to - FIT_RUNS;
	for (step = 0; step < FIT_STEPS; step++) {
		unit = shortest + (longest - shortest) * step / (FIT_STEPS - 1);
		error = fit_error(r->runs + from, to - from, unit);
		if (error < best_error) {
			best_error = error;
			best = unit;
		}
	}
	return best;
}

// the over's runs from emitted on, with the glitches joined to what is
// around them, read into letters; a letter is said once the space after it
// has ended, and emitted moves past it. With is_ended, the last letter is
// said too
/* words, and guessing them */

// a word's dots and dashes, with a 1 after each mark that ends a letter;
// 0 if it has a letter that is not in the table
static int marks_of_word(const char *word, char *marks, char *ends, int room) {
	const char *code;
	int count = 0;
	int length;

	for (; *word != 0; word++) {
		code = code_of(*word);
		if (code == NULL)
			return 0;
		length = (int)strlen(code);
		if (count + length >= room)
			return 0;
		memcpy(marks + count, code, length);
		memset(ends + count, 0, length);
		count += length;
		ends[count - 1] = 1;
	}
	marks[count] = 0;
	return count;
}

static int is_known(const struct cw_reader *r, const char *word) {
	int i;

	for (i = 0; i < KNOWN_COUNT; i++) {
		if (!strcmp(word, known_words[i]))
			return 1;
	}
	for (i = 0; i < r->learned_count; i++) {
		if (!strcmp(word, r->learned[i]) && r->learned_seen[i] >= SEEN_TO_BE_KNOWN)
			return 1;
	}
	return 0;
}

// what it costs to read the word's marks as the known word: for each gap
// that the known word puts on the other side of the line between a gap
// inside a letter and one between letters, how close the gap was to it
static double cost_as(const struct cw_reader_word *w, const char *word) {
	char marks[2 * CW_READER_WORD_MARKS + 1];
	char ends[2 * CW_READER_WORD_MARKS + 1];
	double cost = 0;
	int count = marks_of_word(word, marks, ends, sizeof(marks));
	int is_end_read;
	int i;

	if (count == 0 || count != w->mark_count || memcmp(marks, w->marks, count) != 0)
		return GUESS_NONE;
	for (i = 0; i < count - 1; i++) {
		is_end_read = w->gaps[i] > DOT_OR_DASH;
		if (ends[i] && !is_end_read)
			cost += DOT_OR_DASH - w->gaps[i];
		if (!ends[i] && is_end_read && w->gaps[i] > WORD_DOTS)
			cost += w->gaps[i] - WORD_DOTS;
		else if (!ends[i] && is_end_read)
			cost += w->gaps[i] - DOT_OR_DASH;
	}
	return cost;
}

// the known word the marks of w are most likely to be, and what it costs;
// NULL if none is close enough
static const char *best_guess(const struct cw_reader *r, const struct cw_reader_word *w,
		double *best) {
	const char *chosen = NULL;
	const char *known;
	double cost;
	int i;

	*best = GUESS_MOST_DOTS;
	for (i = 0; i < KNOWN_COUNT + r->learned_count; i++) {
		known = known_words[0];
		if (i < KNOWN_COUNT)
			known = known_words[i];
		else if (r->learned_seen[i - KNOWN_COUNT] >= SEEN_TO_BE_KNOWN)
			known = r->learned[i - KNOWN_COUNT];
		else
			continue;
		cost = cost_as(w, known);
		if (cost < *best) {
			*best = cost;
			chosen = known;
		}
	}
	return chosen;
}

// a word made only of the shortest letters, E, T and I, one or two of them,
// is what noise reads as, and is not said
static int is_noise_word(const char *word) {
	int length = (int)strlen(word);
	int i;

	if (length > NOISE_WORD_LONGEST)
		return 0;
	for (i = 0; i < length; i++) {
		if (word[i] != 'E' && word[i] != 'T' && word[i] != 'I')
			return 0;
	}
	return 1;
}

static int is_letter(char c) {
	return c >= 'A' && c <= 'Z';
}

static int is_digit(char c) {
	return c >= '0' && c <= '9';
}

// whether a word has the pattern of a callsign: a prefix of one to three
// letters and digits with a letter among them, a digit, and a suffix of
// one to four letters, and perhaps a stroke and more after it: VU2EHA,
// 4X5JK, VU2EHA/P
static int is_a_callsign(const char *word) {
	int prefix = 0;
	int has_letter = 0;
	int suffix = 0;
	int i = 0;

	while (word[i] != 0 && (is_letter(word[i]) || is_digit(word[i])) && prefix <= PREFIX_LONGEST) {
		if (is_letter(word[i]))
			has_letter = 1;
		prefix++;
		i++;
		// the area digit is the last digit before the suffix's letters
		if (is_digit(word[i - 1]) && is_letter(word[i]) && prefix >= 2)
			break;
	}
	if (prefix < 2 || !has_letter || !is_digit(word[i - 1]))
		return 0;
	while (is_letter(word[i])) {
		suffix++;
		i++;
	}
	if (suffix < 1 || suffix > SUFFIX_LONGEST)
		return 0;
	return word[i] == 0 || word[i] == '/';
}

// a callsign read cleanly is kept, the last CW_READER_LEARNED_MAX of
// them, and is a known word once it has been read cleanly twice: a part of
// one, read once, is not taken for one
static void learn(struct cw_reader *r, const char *word) {
	int i;

	if ((int)strlen(word) >= CW_READER_LEARNED_ROOM || !is_a_callsign(word))
		return;
	for (i = 0; i < r->learned_count; i++) {
		if (!strcmp(word, r->learned[i])) {
			r->learned_seen[i]++;
			return;
		}
	}
	snprintf(r->learned[r->learned_next], CW_READER_LEARNED_ROOM, "%.*s",
		CW_READER_LEARNED_ROOM - 1, word);
	r->learned_seen[r->learned_next] = 1;
	r->learned_next = (r->learned_next + 1) % CW_READER_LEARNED_MAX;
	if (r->learned_count < CW_READER_LEARNED_MAX)
		r->learned_count++;
}

// a guess, in square brackets, after the letters already said
static void say_a_guess(struct cw_reader *r, const char *guess) {
	say(r, "[");
	say(r, guess);
	say(r, "] ");
}

// the guess for a word whose letters have been said: the known word its
// marks are, if the gaps that differ were close enough to the line, and it
// is not what was read; NULL if there is none
static const char *guess_for(struct cw_reader *r, const struct cw_reader_word *w) {
	double cost;
	const char *guess;

	if (w->text_count == 0 || is_known(r, w->text))
		return NULL;
	guess = best_guess(r, w, &cost);
	if (guess != NULL && !strcmp(guess, w->text))
		return NULL;
	return guess;
}

// a word has ended; its letters have been said as they came. The guess
// for it, if there is one, is said after it in square brackets, as the
// author asked. The word before it is kept, so that the two can be tried
// as one, joined across the gap between them, a word gap that was not, as
// "VU2E HA [VU2EHA]"; the join is tried first
static void finish_the_word(struct cw_reader *r) {
	struct cw_reader_word joined;
	struct cw_reader_word *held = &r->held;
	struct cw_reader_word *w = &r->word;
	double cost;
	const char *guess = NULL;

	w->text[w->text_count] = 0;
	if (w->text_count == 0)
		return;
	say(r, " ");
	if (held->text_count > 0 && held->mark_count + w->mark_count <= CW_READER_WORD_MARKS
			&& held->text_count + w->text_count < CW_READER_WORD_TEXT) {
		joined = *held;
		memcpy(joined.marks + joined.mark_count, w->marks, w->mark_count);
		memcpy(joined.gaps + joined.mark_count, w->gaps, w->mark_count * sizeof(w->gaps[0]));
		joined.mark_count += w->mark_count;
		memcpy(joined.text + joined.text_count, w->text, w->text_count + 1);
		joined.text_count += w->text_count;
		if (!is_known(r, held->text) || !is_known(r, w->text))
			guess = best_guess(r, &joined, &cost);
	}
	if (guess != NULL) {
		say_a_guess(r, guess);
		held->text_count = 0;
		held->mark_count = 0;
	}
	else {
		guess = guess_for(r, w);
		if (guess != NULL)
			say_a_guess(r, guess);
		else if (!is_noise_word(w->text))
			learn(r, w->text);
		*held = *w;
	}
	w->text_count = 0;
	w->mark_count = 0;
}

// at the end of an over, nothing is kept to be joined
static void say_the_held_word(struct cw_reader *r) {
	r->held.text_count = 0;
	r->held.mark_count = 0;
}

// a letter read: its marks and the gaps between them, in dots, and the gap
// after it, go into the word
static void add_a_letter(struct cw_reader *r, const char *code, const double *gaps,
		double gap_after) {
	const char *letter = letter_of(code);
	int count = (int)strlen(code);
	int i;

	struct cw_reader_word *w = &r->word;

	for (i = 0; i < count && w->mark_count < CW_READER_WORD_MARKS; i++) {
		w->marks[w->mark_count] = code[i];
		w->gaps[w->mark_count] = gap_after;
		if (i < count - 1)
			w->gaps[w->mark_count] = gaps[i];
		w->mark_count++;
	}
	say(r, letter);
	while (*letter != 0 && w->text_count < CW_READER_WORD_TEXT - 1)
		w->text[w->text_count++] = *letter++;
	w->text[w->text_count] = 0;
}

static void read_the_over(struct cw_reader *r, int is_ended, double space_now) {
	char code[CW_READER_CODE_ROOM];
	double gaps[CW_READER_CODE_ROOM];
	int code_count = 0;
	double glitch = GLITCH_DOTS * r->unit_s;
	int is_mark;
	double seconds;
	int i = r->emitted;
	int last;

	while (i < r->run_count) {
		is_mark = r->runs[i].is_mark;
		seconds = r->runs[i].seconds;
		last = i;
		// a glitch, and what follows it of the same kind, join this run
		while (last + 2 < r->run_count && r->runs[last + 1].seconds < glitch
				&& r->runs[last + 2].is_mark == is_mark) {
			seconds += r->runs[last + 1].seconds + r->runs[last + 2].seconds;
			last += 2;
		}
		if (!is_ended && last + 1 >= r->run_count && is_mark)
			return;
		if (is_mark && seconds >= glitch && code_count < CW_READER_CODE_ROOM - 1) {
			code[code_count++] = '.';
			if (seconds > DOT_OR_DASH * r->unit_s)
				code[code_count - 1] = '-';
		}
		if (!is_mark && seconds > DOT_OR_DASH * r->unit_s && code_count > 0) {
			code[code_count] = 0;
			add_a_letter(r, code, gaps, seconds / r->unit_s);
			if (seconds > WORD_DOTS * r->unit_s)
				finish_the_word(r);
			code_count = 0;
			r->emitted = last + 1;
		}
		else if (!is_mark && seconds > DOT_OR_DASH * r->unit_s)
			r->emitted = last + 1;
		else if (!is_mark && code_count > 0)
			gaps[code_count - 1] = seconds / r->unit_s;
		i = last + 1;
	}
	// the space under way, which has not ended, ends a letter once it is
	// long enough, so that the last letter of an over is not kept waiting
	if (code_count > 0 && (is_ended || space_now > DOT_OR_DASH * r->unit_s)) {
		code[code_count] = 0;
		add_a_letter(r, code, gaps, space_now / r->unit_s);
		r->emitted = r->run_count;
		r->is_letter_said = 1;
	}
	if ((r->is_letter_said && !is_ended && space_now > WORD_DOTS * r->unit_s
			&& !r->is_word_said) || is_ended) {
		//the gap after the word's last mark is the space as long as it is now
		if (r->word.mark_count > 0 && !is_ended)
			r->word.gaps[r->word.mark_count - 1] = space_now / r->unit_s;
		finish_the_word(r);
		r->is_word_said = 1;
	}
}

static void end_the_over(struct cw_reader *r) {
	if (r->marks >= MIN_MARKS) {
		if (!r->is_trained)
			r->unit_s = fit_the_unit(r, 0, r->run_count);
		read_the_over(r, 1, 0);
	}
	say_the_held_word(r);
	r->run_count = 0;
	r->marks = 0;
	r->marks_at_fit = 0;
	r->emitted = 0;
	r->is_trained = 0;
	r->is_letter_said = 0;
	r->is_word_said = 0;
}

// a mark or a space has ended
static void add_a_run(struct cw_reader *r, int is_mark, double seconds) {
	if (r->run_count == 0 && !is_mark)
		return;
	if (r->run_count == CW_READER_RUNS_MAX)
		end_the_over(r);
	if (is_mark) {
		r->is_letter_said = 0;
		r->is_word_said = 0;
	}
	r->runs[r->run_count].is_mark = is_mark;
	r->runs[r->run_count].seconds = seconds;
	r->run_count++;
	if (!is_mark)
		return;
	r->marks++;
	if (r->marks - r->marks_at_fit < TRAIN_MARKS)
		return;
	r->unit_s = fit_the_unit(r, 0, r->run_count);
	r->marks_at_fit = r->marks;
	r->is_trained = 1;
}

/* what the rest of the radio calls */

void cw_reader_init(struct cw_reader *r, int wpm, double tick_s) {
	memset(r, 0, sizeof(*r));
	r->wpm = wpm;
	r->tick_s = tick_s;
	r->station = CW_READER_SPAN + 1;
	r->unit_s = SECONDS_A_DOT_AT_1_WPM / wpm;
}

// a new WPM moves the span of speeds the reader looks in, and nothing else:
// the callsigns learned, the station, the levels and the over being read
// are kept. If the over has not set its speed yet, it starts from the new
// one
void cw_reader_set_wpm(struct cw_reader *r, int wpm) {
	if (wpm <= 0)
		return;
	r->wpm = wpm;
	if (!r->is_trained)
		r->unit_s = SECONDS_A_DOT_AT_1_WPM / wpm;
}

// a tick: the ratio to the grass of each of the CW_READER_BINS bins around
// the pitch, the pitch's in the middle
void cw_reader_tick(struct cw_reader *r, const float *ratio) {
	float db = envelope_of(r, ratio);
	float middle;
	float threshold;
	float fraction;
	double edge;
	int is_open;

	follow_the_levels(r, db);
	middle = (r->noise_db + r->pedestal_db) / 2;
	if (middle < r->noise_db + SQUELCH_DB / 2)
		middle = r->noise_db + SQUELCH_DB / 2;
	is_open = r->pedestal_db - r->noise_db >= SQUELCH_DB;
	threshold = middle - HYSTERESIS_DB;
	if (!r->is_mark)
		threshold = middle + HYSTERESIS_DB;
	if ((!r->is_mark && db > threshold && is_open) || (r->is_mark && (db < threshold || !is_open))) {
		fraction = 1;
		if (db != r->previous_db)
			fraction = (threshold - r->previous_db) / (db - r->previous_db);
		if (fraction < 0)
			fraction = 0;
		if (fraction > 1)
			fraction = 1;
		edge = r->now_s + fraction * r->tick_s;
		add_a_run(r, r->is_mark, edge - r->edge_s);
		r->edge_s = edge;
		r->is_mark = !r->is_mark;
	}
	r->now_s += r->tick_s;
	r->previous_db = db;
	if (!r->is_mark && r->run_count > 0 && r->now_s - r->edge_s > SILENCE_S)
		end_the_over(r);
	else if (r->is_trained && !r->is_mark)
		read_the_over(r, 0, r->now_s - r->edge_s);
	else if (r->is_trained)
		read_the_over(r, 0, 0);
}

// what has been read since the last call; says how many characters
int cw_reader_take(struct cw_reader *r, char *text, int room) {
	int count = r->text_count;

	if (count > room - 1)
		count = room - 1;
	memcpy(text, r->text, count);
	text[count] = 0;
	memmove(r->text, r->text + count, r->text_count - count);
	r->text_count -= count;
	return count;
}
