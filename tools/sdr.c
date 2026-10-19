// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// sdr.c — the radio, run from a terminal, with its front panel.
//
// This opens the radio, tunes it, and then does what it is told, from three
// places at once: the terminal, the front panel, and clients on the network.
//
// The radio's settings are controls, in control.c's table: the radio's own,
// registered by radio.c, and each modem's. Every change goes through
// control_set(), wherever it comes from, and is passed on to everyone who
// did not make it: to each network client, to the front panel, and printed
// on the terminal. So a band chosen on the desktop moves the panel's dial,
// and a turn of the panel's knob moves the desktop's.
//
// Clients connect on port 8081. server.c says what they say.
//
// From the terminal it takes a setting as a client sends it, "FREQ 7074000",
// or a request in the words of sdr_request(), "r1:agc=FAST", which goes to
// the core as it is. A frequency or volume set that way, r1:freq= or
// r1:volume=, goes through the controls like any other change.
//
// TRANSMITTING
//
// The radio transmits while its control TX is ON, which a client or the
// terminal sets, "TX ON", or the PTT: in USB, LSB and 2TONE, the modes
// that take a microphone's switch, the radio transmits while the PTT is
// held, as zbitxv2 did. In CW the same contact is a paddle and is the
// keyer's. A change of the PTT is taken when it has held for 30 ms, so
// that its contacts' bounce does not put the radio in and out of transmit.
//
// A modem may ask for the transmitter itself, as CW's does from the moment
// a paddle closes until its hang time after the last element: the radio
// transmits while the modem of the mode in use says it has something to
// send. modem.h says how a modem says so.
//
// The program is often run from another computer, where a lost connection
// or a forgotten "TX ON" would leave the transmitter on. So it does not
// transmit for longer than two minutes at a stretch: after that it goes
// back to receive and says so, and a PTT still held has to be let go
// before it will transmit again. And however the program ends, by "quit",
// by its input ending or by a signal, it goes to receive first.
//
// What the operator sets is kept in data/user_settings.ini and is there at
// the next start: the frequency and the mode, the callsign, the keyer's
// speed and the rest; settings.c says what is kept and when it is written.
// It is written once more as the program ends. A frequency, a mode or a
// volume given when the program is started comes after it and is what the
// radio starts with.
//
// "log=VU2" lists the newest entries in the logbook, data/sbitx.db, with
// stations whose callsign begins VU2, and "log=" with any station, on the
// terminal and to the panel, whose LOG screen asks for the same;
// panel_log.c and logbook.c say how.
//
// A contact in CW or on the voice is logged by hand: "CALL VU2XZ", "SENT
// 599", "RECV 579", and "EXCH" and "NR" for the exchanges, then "LOGGER
// SAVE", as the panel's LOG screen does; "LOGGER WIPE" clears them.
// logger.c says what an entry needs.
//
// What the speaker plays is also sent out on the radio itself, as RTP, for
// other programs to hear: rtp_audio.c says what and to where. "report"
// prints how loud the loudest of it was since it was last asked.
//
// What is measured, the signal's strength as the radio receives and the
// forward power and the SWR as it transmits, and the battery, is not
// printed as it changes, five times a second; "report" prints it. radio.c
// and meter.c say what the numbers are and who measures them.
//
// What the radio copies off the air, FT8's messages, is printed on the
// terminal as it is copied, a line to each: the mode, the time in UTC it
// was sent at, its strength in dB, its pitch in Hz and its text. The same
// goes to the panel; messages.c and panel_messages.c say how.
//
// In FT8, "FT8_DO CQ" calls CQ and "FT8_DO HALT" stops, as the panel's
// buttons do, and "answer=G0XYZ" answers the newest message heard from
// G0XYZ, as marking it on the panel and clicking the knob does; ft8_qso.c
// says what follows.
//
// In CW what the reader reads off the air runs through the panel's status
// line, and what zbitxv2's decoder reads is printed a line at a time, "cw
// goertzel: CQ CQ DE VU2ESE"; panel_cw.c says how.
//
// "WIFI SCAN" lists the WiFi access points the radio can hear, on the
// terminal and to the panel, whose WiFi screen asks for it; panel_wifi.c
// says how.
//
// The radio's time is the kernel's when that can be trusted and what the
// operator gave on the panel when it cannot; hw/radio_clock.c says how it
// is judged, and the radio's control CLOCK says the time each minute. The
// panel is sent the time in UTC each second, which it shows under its list
// of controls: the author wanted the time in sight, for FT8's slots.
//
// The panel has a status bar, a line of text. The radio puts there what it
// found at the start check, and why it refused a change the panel asked
// for. hw/panel.c's panel_say() sends it.
//
// From the panel it takes a new value for any control, over the link that
// hw/panel.c keeps. When the radio starts, and when the panel says HELLO,
// which it does when it starts, the panel is told what controls there are
// and then their values, and after that every change that it did not make
// itself. panel_controls.c does the telling.
//
// Four times a second it sends the panel a line of the spectrum, for the
// panel's graph and waterfall. The line is 144 points across 25 kHz, the
// tuned frequency in the middle, as zbitxv2 shows it: each point is the
// strongest of the three or four bins it stands for, in dB, with 30 added so
// that the noise sits a little above the bottom. spectrum_plot runs from
// the highest frequency down, so the line is turned round to have the
// lowest frequency first.
//
// SPAN is the program's own control, for the panel: 25K, 10K, 6K or 2.5K is
// how much of the spectrum the line shows, the tuned frequency staying in
// the middle. The author's panel steps it when the waterfall is touched.
// It is in the group PANEL, so that no client's list shows it. STEP, the
// panel's tuning step, is there too, kept by the radio for the panel, and
// so are the VFO the panel has in use and each VFO's dial.
//
// Ten times a second, while a network client has asked for it, it sends the
// same spectrum at the full resolution of the bins, 534 points, one to a
// bin. server.c says how. While the radio transmits it sends them instead the
// envelope of what is being sent, which the core keeps, as zbitxv2's window
// drew it in the spectrum's place.
//
// Some words are the program's own. "report" prints how the sound card and
// the link have been doing, and the largest sample sent to the transmitter
// in the last transmission, in dB below the sound card's full scale, with
// the drive times the band's scale it went at: what the sound card was
// given, so that a transmitter that is weak can be told to be weak in the
// program or after it. "panel=off" stops the radio polling the panel
// and "panel=on" starts it again. "spectrum=off" stops the spectrum being
// sent and "spectrum=on" starts it again. They are there so that the noise
// each makes in the receiver can be told by ear from the rest. "quit", or
// the end of the input, closes the radio.
//
// "vfo=off" and "bfo=off" switch off the radio's first oscillator and its
// second, and "vfo=on" and "bfo=on" switch them on again. They are for
// finding where a noise gets into the receiver. With both off, nothing from
// the antenna or the first IF can reach the sound card, so a noise that is
// still heard is getting in at the second IF or after it. While an
// oscillator is off, tuning switches it on again, since tuning sets it.
// Switching one on resets the chip's PLLs, as the radio does when it starts.
//
// "record=600" writes everything the sound card gives, both channels as
// they come, for 600 seconds, into data/record_<UTC time>.raw, with a note
// of how the radio was set beside it in a .txt; "record=0" stops it. It is
// for working on the decoders with signals heard off the air, played back
// through the receiver as tests/hw_stand_ins.c plays a test's. Ten minutes
// is about 460 MB.
//
// "txrecord=20" writes what is sent, for the next 20 seconds of
// transmitting, into data/txrecord_<UTC time>.raw with its note: for each
// block, the samples the modem made and those the transmitter was given,
// after the sideband's filter. It is for checking a modem's signal with a
// reference decoder, at each step on its way out; core_sdr.c says why it
// was added. "txrecord=0" stops it.
//
// "play=data/record_20261004_023102.raw" plays such a recording back, in
// real time: each block of it takes the place of what the sound card
// heard, both channels, before the receiver has it, so that the decoders,
// the panel and the speaker have the recording as if it were on the air.
// The mode, the pitch and the speed are first set as the note beside it
// says they were. "play=0" stops it; it stops by itself at the end.
//
// "snapshot" is for the times the sound and the spectrum go wrong and a
// restart puts them right. It saves the samples of the next sixteen blocks
// just as the sound card gave them, both channels, in data/snapshot.raw,
// 32-bit numbers, the receiver's and then the microphone's for each frame,
// and prints for each channel its largest sample, its mean, how many of its
// samples are zero, and how many are the same as the one before. Samples
// that are zero or repeated in runs were wrong before the radio touched
// them, which is the card's doing and not the transform's.
//
// "audio=reset" and "si5351=reset" are for the same fault, to find which
// part of a restart it is that cures it, without restarting. The first
// closes the sound card and opens it again, the levels kept. The second
// sets the Si5351 up from nothing, as the radio does when it starts, and
// puts both oscillators back on their frequencies. Neither touches a number
// of the receiver's own. They are for use in receive: take a snapshot, give
// one of them, and take another.
//
// THE CHECK AT THE START
//
// The same fault, the author says, is there from the start or not at all.
// So three seconds after it starts the program takes such a snapshot by
// itself, measures the ripple in its spectrum, tools/start_check.c says
// how, and prints what it found and what it guesses: "start check: ripple
// 4.7 dB at 8.0 kHz, I2S sync error at this start: yes, HUMPS SUSPECTED".
// The line is added to data/start_checks.log with the date and the time,
// and the snapshot is kept beside it under the same date and time, so that
// what the radio guessed at a start can be set against what was seen.
//
// When it suspects the humps it closes the sound card and opens it again,
// which is what cures them, and three seconds later measures once more and
// says what it found then; and again, up to START_CHECK_REOPENINGS times,
// if they are still there. The author let it do so on 2 October 2026, when
// on three starts in a day the radio had guessed the humps, each time at
// 4.4 dB or more where a good start measures under 2.6, each time the
// author had seen them, and each time reopening the card had cured them.
// It measures again because nothing shows that a second opening is always
// a good one, which was the author's objection to reopening the card as a
// matter of course at every start.
//
// Whether the kernel logged "I2S SYNC error!" as this run's stream started
// is noted with it, since that is a lead on the cause; hw/audio.c says
// what is known. It is read from the kernel's log, by way of dmesg.
//
// "measure=5" measures for five seconds how strong what reaches the sound
// card is, within 3 kHz either side of the tuned frequency, and then prints
// it in dB. It looks at the spectrum twenty times a second and averages the
// power in each bin. It prints the mean over the bins, and the middle one
// of them by size, which a few steady carriers do not move. With both
// oscillators off, what is measured is what the radio makes for itself, and
// that is how the panel's hash is given a number. The number is for
// comparing one setting with another; it is not referred to any standard.
//
//     sdr [hz] [mode] [volume]
//
// Without arguments it opens as radio.c opens, on 7088 kHz, LSB. The
// passband follows the mode.
//
// The program waits on the terminal for a few milliseconds at a time and
// looks to the panel in between. hw/panel.c decides whether it is time to
// poll.

#include <math.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "board.h"
#include "panel.h"
#include "panel_controls.h"
#include "panel_log.h"
#include "panel_messages.h"
#include "panel_cw.h"
#include "radio_clock.h"
#include "rtp_audio.h"
#include "rtp_microphone.h"
#include "rtp_voip.h"
#include "ft8_qso.h"
#include "logger.h"
#include "messages.h"
#include "meter.h"
#include "panel_wifi.h"
#include "control.h"
#include "modem.h"
#include "radio.h"
#include "server.h"
#include "si5351.h"
#include "sdr.h"
#include "settings.h"
#include "start_check.h"

#define BFO_CLOCK       1
#define VFO_CLOCK       2

#define LINE_MAX_LENGTH 128
#define RESPONSE_MAX    100

#define TERMINAL        0
#define WAIT_ON_TERMINAL_MS 2
#define MS_PER_SECOND   1000
#define SECONDS_A_DAY   86400
#define NS_PER_MS       1000000

#define FREQ_IS         "r1:freq="
#define VOLUME_IS       "r1:volume="

#define SNAPSHOT_FILE   "data/snapshot.raw"
#define SNAPSHOT_CHANNELS 2
#define SETTINGS_FILE "data/user_settings.ini"
#define LOGBOOK_FILE "data/sbitx.db"
#define LOG_FOR "log="
#define RECORD_FOR "record="
#define TX_RECORD_FOR "txrecord="
#define PLAY_FOR "play="
#define NOTE_LINE_ROOM 256
#define NOTE_VALUE_ROOM 16
#define RECORD_NAME_ROOM 96
#define RECORD_STAMP_ROOM 20
#define SAMPLE_RATE_HZ 96000
#define ANSWER "answer="

#define START_CHECK_AFTER_MS 3000
#define START_CHECK_REOPENINGS 3
#define START_CHECK_LOG "data/start_checks.log"
#define START_CHECK_NAME_MAX 64
#define KERNEL_LOG_COMMAND "dmesg"
#define KERNEL_LOG_LINE_MAX 400
#define SYNC_ERROR_TEXT "I2S SYNC error"
// the kernel's times and the program's are not quite the same clock
#define SYNC_ERROR_SLACK_S 2.0
#define HZ_PER_KHZ 1000.0
#define TENTHS 10.0
#define HUNDREDTHS 100.0
#define TX_PEAK_ASKED "stat:tx_peak="

#define TX_LIMIT_MS     120000
// NEW_COOKIE MAKING is put back to - after this long, if no gateway did
#define NEW_COOKIE_WAIT_MS 5000
#define NOT_ASKED 0
#define PTT_SETTLED_MS  30
#define NOT_TRANSMITTING 0

#define SPECTRUM_EVERY_MS 250
#define SPECTRUM_FIRST_BIN 1269
#define SPECTRUM_BINS   534
#define SPECTRUM_ADDED_DB 30
#define SPECTRUM_HIGHEST 255
#define SPECTRUM_HZ_PER_BIN 46.875
#define HZ_PER_K 1000.0
#define VFO_DIAL_MAX_HZ 30000000
#define CLIENT_SPECTRUM_EVERY_MS 100
// mod_display has a least and a greatest for each point. 50 either way
// was the height zbitxv2's window gave it, and is the top of the line sent
// to clients; 2TONE comes to 21
#define MODULATION_POINTS (MOD_MAX / 2)
#define MODULATION_FULL_SCALE 50
#define MODULATION_HIGHEST 255

// what is measured: 3 kHz either side of the tuned frequency, which is bin
// 1536 of the spectrum, a bin being 46.875 Hz
#define MEASURE_FOR     "measure="
#define MEASURED_FIRST_BIN 1472
#define MEASURED_BINS   128
#define MEASURE_EVERY_MS 50
#define SMALLEST_POWER  1e-30

static volatile sig_atomic_t is_done;
static long tx_began_ms;
static int is_ptt_transmitting;
static long voip_tx_began_ms;
static long new_cookie_asked_ms;
static int is_ptt_to_be_let_go;
static int ptt_level;
static int ptt_level_seen;
static long ptt_level_since_ms;
static int is_sending_spectrum = 1;
static long next_spectrum_ms;
static uint8_t spectrum_line[LINK_SPECTRUM_POINTS];
static long next_client_spectrum_ms;
static uint8_t client_spectrum_line[SPECTRUM_BINS];
static uint8_t client_modulation_line[MODULATION_POINTS];

static double measured_power[MEASURED_BINS];
static int measured_times;
static int is_measuring;
static long next_measure_ms;
static long measure_until_ms;

static long now_ms(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * MS_PER_SECOND + now.tv_nsec / NS_PER_MS;
}

static void report(void) {
	int reads;
	int failures;
	int forward;
	int reflected;
	struct audio_report sound;
	struct panel_report link;
	char asked[] = TX_PEAK_ASKED;
	char answer[RESPONSE_MAX];

	audio_get_report(&sound);
	printf("blocks %lu, gaps %lu, dropped %lu, overflows %lu, "
		"longest wait %ld us, queued %ld frames\n",
		sound.blocks, sound.playback_gaps, sound.blocks_dropped,
		sound.capture_overruns, sound.longest_wait_us, sound.queued_frames);
	if (!sound.is_realtime)
		printf("the audio thread did not get real-time priority\n");
	if (!sound.is_running)
		printf("the audio thread has stopped\n");

	panel_get_report(&link);
	printf("panel: polls %lu, answers %lu, no answer %lu, spoiled %lu, "
		"repeated %lu, words %lu, not taken %lu\n",
		link.polls, link.answers, link.no_answers, link.frames_spoiled,
		link.frames_repeated, link.words, link.words_not_taken);
	if (!link.is_polling)
		printf("the panel is not being polled\n");
	else if (!link.is_present)
		printf("the panel does not answer\n");
	sdr_request(asked, answer);
	printf("transmitter: peak %s\n", answer);
	printf("sound sent out on the radio: loudest %d percent of full scale since last asked\n",
		rtp_audio_peak_percent());
	printf("speech taken in on the radio to transmit: %d packets since last asked\n",
		rtp_microphone_packets());
	printf("the owner's speech sent to VoIP: %d packets since last asked\n",
		rtp_voip_packets_sent());
	printf("meter: power %.1f W, swr %.1f, battery %.2f V, smeter %d\n",
		control_get_int("POWER") / TENTHS, control_get_int("SWR") / TENTHS,
		control_get_int("BATTERY") / HUNDREDTHS, control_get_int("SMETER"));
	meter_bridge_seen(&reads, &failures, &forward, &reflected);
	printf("swr bridge since last asked: read %d times, no answer %d times, "
		"greatest forward count %d with reflected %d\n", reads, failures, forward, reflected);
}

static int max_bin_level(int first_bin, int after_last_bin) {
	int strongest = spectrum_plot[first_bin];
	int bin;

	for (bin = first_bin + 1; bin < after_last_bin; bin++) {
		if (spectrum_plot[bin] > strongest)
			strongest = spectrum_plot[bin];
	}
	return strongest;
}

static uint8_t spectrum_level(int bin) {
	int level = spectrum_plot[bin] + SPECTRUM_ADDED_DB;

	if (level < 0)
		level = 0;
	if (level > SPECTRUM_HIGHEST)
		level = SPECTRUM_HIGHEST;
	return (uint8_t)level;
}

// SPAN is how much of the spectrum the panel's line shows, the tuned
// frequency staying in the middle: "25K" is all of it, "2.5K" a tenth
static int span_bins = SPECTRUM_BINS;

static int apply_span(struct control *c, const char *value, char *why, int room) {
	int bins = (int)(atof(value) * HZ_PER_K / SPECTRUM_HZ_PER_BIN + 0.5);

	(void)c;
	(void)why;
	(void)room;
	if (bins > SPECTRUM_BINS)
		bins = SPECTRUM_BINS;
	if (bins < 1)
		bins = 1;
	span_bins = bins;
	return 0;
}

// STEP is the tuning step, the same for every client, the panel, the
// window and the web page: the author ruled on 16 October 2026 that STEP
// and VFO be common, where they had been each client's own. Each client
// still does its own tuning, by the step; the radio keeps it, and remembers
// it from one start to the next. It applies nothing here. Its group is
// still PANEL, where the panel looks for it.
static struct control step = {
	.group = "PANEL", .name = "STEP", .kind = CONTROL_CHOICE,
	.choices = "10H/100H/500H/1K/10K",
	.value = "100H",
};

// the VFO in use, and each VFO's dial, kept for every client in the same
// way: "the VFO cannot be different for different clients", the author's
// words. A client that changes it tells the radio both dials and the VFO,
// and tunes to the new one's dial; the others follow. 0 is a VFO that has
// not been used yet. The radio's own frequency is the dial of the VFO in
// use.
static struct control vfo = {
	.group = "PANEL", .name = "VFO", .kind = CONTROL_CHOICE,
	.choices = "A/B",
	.value = "A",
};

static struct control vfo_a = {
	.group = "PANEL", .name = "VFO_A", .kind = CONTROL_NUMBER,
	.min = 0, .max = VFO_DIAL_MAX_HZ, .step = 1,
	.value = "0",
};

static struct control vfo_b = {
	.group = "PANEL", .name = "VFO_B", .kind = CONTROL_NUMBER,
	.min = 0, .max = VFO_DIAL_MAX_HZ, .step = 1,
	.value = "0",
};

// CONNECTIONS are what the radio has to do with the world outside itself,
// and the owner's switches for them. The author's design: to the owner it
// is one radio, so they are among its settings, on a screen of their own
// under SET, and not in a window of the program that acts on them.
//
//   REMOTE     whether the radio may be worked from afar, through
//              my.sbitx.net
//   STREAMING  whether anyone may listen to it there, and see what it
//              copies in FT8
//   BLUETOOTH  whether its sound is played into a paired Bluetooth
//              earphone; it is with the other two below
//
// Both are on until the owner turns them off, the author's ruling of 17
// October 2026: a radio added at my.sbitx.net should be seen on line, and
// the registration form asks the owner to tick that both are ON. They are
// kept from one start to the next, so a radio whose owner had them off
// keeps them off. They are here as STEP is: the radio program keeps them and
// does nothing with them. The program that does, the gateway,
// clients/remote/zbitx_remote.c, is a client like any other: it is told
// when one changes, and it sets REMOTE_STATUS, which is what there is to
// say of the link to my.sbitx.net and is not kept. They are a plain on and
// off, set at the radio and nowhere else, which the author asked for.
static struct control remote = {
	.group = "CONNECTIONS", .name = "REMOTE", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "ON",
};

static struct control streaming = {
	.group = "CONNECTIONS", .name = "STREAMING", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "ON",
};

// SERVER is the service the radio is put on line with, my.sbitx.net unless
// the owner has put another in data/user_settings.ini, where the radio
// writes it with the other settings: zbitxre is open, and anyone may run
// the service, accounts/, on a server of their own, the author's ruling of
// 18 October 2026. The gateway reads it from that file when it starts, and
// so do setup/set_clock.sh and setup/panel_firmware.sh. It is in no list,
// and is set at the radio only, from the file or the terminal: a client
// that set it wrong, from afar, would leave the radio unreachable
static struct control server = {
	.group = "SERVICE", .name = "SERVER", .kind = CONTROL_TEXT,
	.value = "my.sbitx.net", .is_set_at_the_radio = 1,
};

static struct control remote_status = {
	.group = "REMOTE", .name = "REMOTE_STATUS", .kind = CONTROL_TEXT,
};

// BLUETOOTH is the third of the CONNECTIONS: whether the radio's sound is
// played into a Bluetooth earphone or speaker that has been paired with
// the radio's computer. The gateway sees to it as it does the others, and
// BLUETOOTH_STATUS is what it has to say of it, which is not kept
static struct control bluetooth = {
	.group = "CONNECTIONS", .name = "BLUETOOTH", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "OFF",
};

static struct control bluetooth_status = {
	.group = "REMOTE", .name = "BLUETOOTH_STATUS", .kind = CONTROL_TEXT,
};

// Other programs on the radio, WSJT-X and FreeDV among them, are always
// given two sound devices to hear the receiver by and to send by: the
// gateway sees to it, with setup/sound_card.py, whenever this program is
// up. It was a switch, SOUND_CARD, until the author ruled on 16 October
// 2026 that it should always be on. SOUND_CARD_STATUS is the names of the
// two devices, to be chosen in the other program, or why they are not
// there.
static struct control sound_card_status = {
	.group = "REMOTE", .name = "SOUND_CARD_STATUS", .kind = CONTROL_TEXT,
};

// CAT is the fifth: whether other programs may set the radio's frequency
// and mode and key it, through Hamlib's "NET rigctl". The gateway answers
// them; clients/remote/cat.c says how. CAT_STATUS is the address and port
// to give the other program
static struct control cat = {
	.group = "CONNECTIONS", .name = "CAT", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "OFF",
};

static struct control cat_status = {
	.group = "REMOTE", .name = "CAT_STATUS", .kind = CONTROL_TEXT,
};

// VOIP: whether the radio is on VoIP at my.sbitx.net, where
// the dial is the room. ONLINE, another's speech on the radio's frequency
// is heard in any mode; and in the mode VOIP, the PTT sends the
// owner's speech there and not to the transmitter. modems/modem_voip.c
// says what the author asked for. It is kept from one start to the next,
// his ruling: it is how a radio is reached when its owner is not at it.
// The gateway does the talking to my.sbitx.net, and VOIP_STATUS is what
// it says: the channel and how many radios are in it, "7074 KHZ, 3 HERE",
// or why the owner's speech was not passed, "BUSY VU3XYZ". It was among
// the CONNECTIONS until the author moved it, on 17 October 2026, to the
// list of the mode VOIP, where the owner is when they want it
static struct control voip = {
	.group = "VOIP", .name = "VOIP", .kind = CONTROL_CHOICE,
	.choices = "OFFLINE/ONLINE",
	.value = "OFFLINE",
};

// BREAK_IN: whether another's speech over VoIP breaks in on the other
// modes, ON as the author ruled on 16 October 2026, or is heard in the mode
// VOIP alone. He asked for it on 17 October 2026, in VOIP's list. It is
// kept from one start to the next
static struct control break_in = {
	.group = "VOIP", .name = "BREAK_IN", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "ON",
};

static struct control voip_status = {
	.group = "VOIP", .name = "VOIP_STATUS", .kind = CONTROL_TEXT,
};

// VOIP_TX is the PTT in the mode VOIP: ON while the owner is speaking over
// the internet. A client's PTT sets it as the radio's own PTT does; it is
// apart from TX so that every client can show it apart, in a colour of
// its own and not transmitting's red, the author's ruling: the finals are
// not keyed. VOIP_SPEAKER is the callsign of whoever is being heard over
// VoIP now, for the line above the spectrum, and nothing while nobody is
static int apply_voip_tx(struct control *c, const char *value, char *why, int room) {
	(void)c;
	if (!strcmp(value, "ON") && (strcmp(control_get("MODE"), "VOIP")
			|| strcmp(control_get("VOIP"), "ONLINE"))) {
		snprintf(why, room, "only in the mode VOIP, with VOIP ONLINE");
		return -1;
	}
	return 0;
}

static struct control voip_tx = {
	.group = "VOIP", .name = "VOIP_TX", .kind = CONTROL_CHOICE,
	.choices = "OFF/ON",
	.value = "OFF", .apply = apply_voip_tx,
};

static struct control voip_speaker = {
	.group = "VOIP", .name = "VOIP_SPEAKER", .kind = CONTROL_TEXT,
};

// RADIO_KEY is the radio's key for my.sbitx.net, the cookie the gateway
// keeps, "abcd-jklm-wxyz", which the owner types at the website to add the
// radio. The gateway says it whenever it is up. It was shown in
// REMOTE_STATUS, beside whether the radio was connected, until the author
// found the one control doing two things confusing, on 16 October 2026.
// It is not kept among the settings: the gateway has it in data/remote.key.
// A tap on it in the window or on the panel asks whether to make a new
// one; a yes sets NEW_COOKIE to MAKING
static struct control radio_key = {
	.group = "CONNECTIONS", .name = "RADIO_KEY", .kind = CONTROL_TEXT,
};

// UPDATE brings the radio up to the newest zbitxre: the author asked on 16
// October 2026 for it among the settings, to run the updates, the panel's
// firmware with them, without a terminal. A tap on it in the window or on
// the panel asks first, in a box, whether to update; a yes sets it to
// UPDATING, and CANCEL, from the view of the update, stops it. It is in the
// STATION group, as the settings are, so that the window lists it in SET.
//
// The radio runs setup/update.sh --if-new itself, as a child whose output
// it reads through a pipe until the pipe closes: the author's way, on 17
// October 2026, after the update had been started apart from the radio,
// had stopped it and started it again in a new terminal, which was handed
// the radio's port for clients and held it, so that the new radio program
// could not have it and the window waited on a port nobody answered. The
// script only fetches and builds. Each line it says is UPDATE_STATUS, which
// the window and the panel show in a view of their own until the update
// ends: the author asked the day before to see it happening and whether
// it succeeded. Its last begins "DONE: ", "FAILED: " or "CANCELLED: ", or,
// when something new was built and it ends with UPDATE_BUILT, the radio
// waits until it is not transmitting, closes what it has open and becomes
// the new radio program in its place, by exec, in the same terminal, with
// UPDATED_ENV saying so. The new one gives the panel its firmware before
// it opens the panel, and then says "DONE: now ..." and how the panel's
// firmware went for
// UPDATE_SAID_FOR_MS, so that a window that connects after the restart is
// told how it ended and one that connects later is not.
#define UPDATE_SCRIPT "setup/update.sh"
#define PANEL_FIRMWARE_SCRIPT "setup/panel_firmware.sh --from-radio"
#define BUILT_FROM_FILE "data/built_from"
#define BUILT_FROM_ROOM 16
#define UPDATED_ENV "ZBITXRE_UPDATED"
#define UPDATE_BUILT 0
#define UPDATE_SAID_FOR_MS (5 * 60 * 1000L)
// how long an update's end is kept when the radio did not restart, for
// the views that are open to show it; one that opens later is not shown
// an old update's end
#define UPDATE_ENDED_SAID_FOR_MS 30000L
#define UPDATE_LINE_ROOM 256
#define OPEN_FILES_MOST 1024
// what update.sh before this one left for the radio it started again,
// read once, for the first update of a radio from the old way to this
#define UPDATE_RESULT "data/update_result"

static struct control update_status = {
	.group = "UPDATE", .name = "UPDATE_STATUS", .kind = CONTROL_TEXT,
};

static struct control update = {
	.group = "STATION", .name = "UPDATE", .kind = CONTROL_CHOICE,
	.choices = "-/UPDATING/CANCEL",
	.value = "-",
};

static void set_setting(const char *name, const char *value, int origin);

static pid_t update_pid;
static int update_fd = -1;
static char update_line[UPDATE_LINE_ROOM];
static int update_line_used;
static int update_has_ended;
static int is_restart_due;
static int is_restarting;
static int is_waiting_for_tx;
static long update_said_until_ms;
// set at the start of a radio program that an update started, by exec
static char updated_said[CONTROL_VALUE_MAX];

static void update_say(const char *text) {
	printf("update: %s\n", text);
	fflush(stdout);
	set_setting("UPDATE_STATUS", text, ORIGIN_NONE);
}

// update.sh, as a child of this program in a session of its own, so that
// CANCEL can stop it and all it has started; it is given nothing of this
// program's but the pipe for what it says
static void update_start(void) {
	int ends[2];
	pid_t child;

	if (update_pid != 0 || is_restart_due || pipe(ends) < 0)
		return;
	child = fork();
	if (child < 0) {
		close(ends[0]);
		close(ends[1]);
		return;
	}
	if (child == 0) {
		setsid();
		dup2(ends[1], STDOUT_FILENO);
		dup2(ends[1], STDERR_FILENO);
		for (int fd = 3; fd < OPEN_FILES_MOST; fd++)
			close(fd);
		setenv("ZBITXRE_FROM_RADIO", "1", 1);
		execl("/bin/bash", "bash", UPDATE_SCRIPT, "--if-new", (char *)NULL);
		_exit(1);
	}
	close(ends[1]);
	fcntl(ends[0], F_SETFL, fcntl(ends[0], F_GETFL) | O_NONBLOCK);
	update_fd = ends[0];
	update_pid = child;
	update_line_used = 0;
	update_has_ended = 0;
	update_say("looking for a newer version");
}

// a line of update.sh's: its own steps, which it begins "update: ", are
// shown, without that; anything else, a compiler's warnings or git's, is
// for the terminal only
static void update_heard(char *line) {
	static const char *const own = "update: ";

	if (strncmp(line, own, strlen(own))) {
		if (line[0] != 0)
			printf("%s\n", line);
		return;
	}
	line += strlen(own);
	update_has_ended = !strncmp(line, "DONE: ", 6) || !strncmp(line, "FAILED: ", 8)
		|| !strncmp(line, "CANCELLED: ", 11);
	update_say(line);
	if (update_has_ended)
		update_said_until_ms = now_ms() + UPDATE_ENDED_SAID_FOR_MS;
}

static void update_ended(int status) {
	if (WIFEXITED(status) && WEXITSTATUS(status) == UPDATE_BUILT && !update_has_ended) {
		is_restart_due = 1;
		return;
	}
	if (!update_has_ended) {
		update_say("FAILED: the update stopped");
		update_said_until_ms = now_ms() + UPDATE_ENDED_SAID_FOR_MS;
	}
}

// what update.sh has said since the last look, and its end when the pipe
// closes
static void update_read(void) {
	char got[UPDATE_LINE_ROOM];
	ssize_t count;
	int status = 0;

	if (update_fd < 0)
		return;
	while ((count = read(update_fd, got, sizeof(got))) > 0) {
		for (ssize_t i = 0; i < count; i++) {
			if (got[i] == '\n' || got[i] == '\r') {
				update_line[update_line_used] = 0;
				update_heard(update_line);
				update_line_used = 0;
			} else if (update_line_used < UPDATE_LINE_ROOM - 1)
				update_line[update_line_used++] = got[i];
		}
	}
	if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return;
	close(update_fd);
	update_fd = -1;
	waitpid(update_pid, &status, 0);
	update_pid = 0;
	update_ended(status);
}

static void update_cancel(void) {
	if (update_pid != 0) {
		kill(-update_pid, SIGTERM);
		update_say("cancelling");
		return;
	}
	if (is_restart_due) {
		is_restart_due = 0;
		is_waiting_for_tx = 0;
		update_say("CANCELLED: what was built is used at the next start");
		update_said_until_ms = now_ms() + UPDATE_ENDED_SAID_FOR_MS;
	}
}

static void follow_update(long now) {
	if (update_said_until_ms != 0 && now > update_said_until_ms) {
		update_said_until_ms = 0;
		set_setting("UPDATE_STATUS", "", ORIGIN_NONE);
	}
	update_read();
	if (!strcmp(control_get("UPDATE"), "CANCEL")) {
		set_setting("UPDATE", "-", ORIGIN_NONE);
		update_cancel();
	}
	if (!strcmp(control_get("UPDATE"), "UPDATING")) {
		set_setting("UPDATE", "-", ORIGIN_NONE);
		update_said_until_ms = 0;
		update_start();
	}
	if (!is_restart_due || is_restarting)
		return;
	if (!strcmp(control_get("TX"), "ON")) {
		if (!is_waiting_for_tx)
			update_say("waiting for TX OFF to restart");
		is_waiting_for_tx = 1;
		return;
	}
	update_say("restarting the radio");
	is_restarting = 1;
	is_done = 1;
}

// this program becomes the one just built, in its place: the same process,
// in the same terminal, with nothing open but what it was started with
static void restart_as_built(char **argv) {
	// a commit's short name, as update.sh writes it
	char built[BUILT_FROM_ROOM] = "";
	char said[CONTROL_VALUE_MAX];
	FILE *file = fopen(BUILT_FROM_FILE, "r");

	if (file != NULL) {
		if (fgets(built, sizeof(built), file) == NULL)
			built[0] = 0;
		built[strcspn(built, "\n")] = 0;
		fclose(file);
	}
	snprintf(said, sizeof(said), "now %s", built);
	setenv(UPDATED_ENV, said, 1);
	for (int fd = 3; fd < OPEN_FILES_MOST; fd++)
		close(fd);
	execv(argv[0], argv);
	printf("update: the new radio program did not start: %s\n", strerror(errno));
}

// at the start of a radio program that an update started: the panel is
// given its firmware, while nothing here has it open. What the script says
// goes to the terminal, and its last line, "outcome: ", into the update's
// last step: the author found on 19 October 2026 that an update said
// nothing of the panel's firmware, given, as before, or failed. An sbitx's
// says none, and its last step is the commit alone
static void updated_take(void) {
	const char *said = getenv(UPDATED_ENV);
	static const char *const mark = "outcome: ";
	char line[UPDATE_LINE_ROOM];
	char outcome[UPDATE_LINE_ROOM] = "";
	FILE *file;

	if (said == NULL) {
		// the old way's end, once
		file = fopen(UPDATE_RESULT, "r");
		if (file == NULL)
			return;
		if (fgets(updated_said, sizeof(updated_said), file) != NULL)
			updated_said[strcspn(updated_said, "\n")] = 0;
		fclose(file);
		unlink(UPDATE_RESULT);
		return;
	}
	file = popen(PANEL_FIRMWARE_SCRIPT, "r");
	while (file != NULL && fgets(line, sizeof(line), file) != NULL) {
		line[strcspn(line, "\n")] = 0;
		if (!strncmp(line, mark, strlen(mark)))
			snprintf(outcome, sizeof(outcome), "%s", line + strlen(mark));
		else
			printf("%s\n", line);
	}
	if (file != NULL)
		pclose(file);
	fflush(stdout);
	if (outcome[0] != 0)
		snprintf(updated_said, sizeof(updated_said), "DONE: %s; %s", said, outcome);
	else
		snprintf(updated_said, sizeof(updated_said), "DONE: %s", said);
	unsetenv(UPDATED_ENV);
}

// NEW_COOKIE makes the radio a new key, for when the old one has gone where
// it should not: the author's way, a new key made on the radio and typed
// into its owner's list at my.sbitx.net. The window and the panel ask
// first, on a tap on RADIO_KEY; this is what they set on a yes, and is in
// no list of their own, in the group KEY. The gateway makes the key, shows
// it in RADIO_KEY and puts this back to -
static struct control new_cookie = {
	.group = "KEY", .name = "NEW_COOKIE", .kind = CONTROL_CHOICE,
	.choices = "-/MAKING",
	.value = "-",
};

static struct control span = {
	.group = "PANEL", .name = "SPAN", .kind = CONTROL_CHOICE,
	.choices = "25K/10K/6K/2.5K",
	.value = "25K", .apply = apply_span,
};

// with a narrow span there are fewer bins than points, and a bin is then
// shown at more than one point
static void spectrum_line_fill(void) {
	int span_first_bin = SPECTRUM_FIRST_BIN + (SPECTRUM_BINS - span_bins) / 2;
	int first_bin;
	int after_last_bin;
	int level;
	int point;

	for (point = 0; point < LINK_SPECTRUM_POINTS; point++) {
		first_bin = span_first_bin + (point * span_bins) / LINK_SPECTRUM_POINTS;
		after_last_bin = span_first_bin
			+ ((point + 1) * span_bins) / LINK_SPECTRUM_POINTS;
		if (after_last_bin <= first_bin)
			after_last_bin = first_bin + 1;
		level = max_bin_level(first_bin, after_last_bin) + SPECTRUM_ADDED_DB;
		if (level < 0)
			level = 0;
		if (level > SPECTRUM_HIGHEST)
			level = SPECTRUM_HIGHEST;
		spectrum_line[LINK_SPECTRUM_POINTS - 1 - point] = (uint8_t)level;
	}
}

static void spectrum_send(long now) {
	if (!is_sending_spectrum || now < next_spectrum_ms)
		return;
	next_spectrum_ms = now + SPECTRUM_EVERY_MS;
	spectrum_line_fill();
	panel_set_bytes(LINK_SPECTRUM, spectrum_line, sizeof(spectrum_line));
}

// the envelope of what is being sent, for the clients to draw where the
// spectrum was: of each half millisecond's least and greatest, the one
// further from zero, with 255 for the top of zbitxv2's picture and for
// anything over it
static void client_modulation_send(void) {
	int point;
	int least;
	int greatest;
	int level;

	for (point = 0; point < MODULATION_POINTS; point++) {
		least = -mod_display[2 * point];
		greatest = mod_display[2 * point + 1];
		level = greatest;
		if (least > level)
			level = least;
		level = (level * MODULATION_HIGHEST) / MODULATION_FULL_SCALE;
		if (level < 0)
			level = 0;
		if (level > MODULATION_HIGHEST)
			level = MODULATION_HIGHEST;
		client_modulation_line[point] = (uint8_t)level;
	}
	server_send_modulation(client_modulation_line, MODULATION_POINTS);
}

static void client_spectrum_send(long now) {
	int bin;

	if (now < next_client_spectrum_ms || !server_wants_spectrum())
		return;
	next_client_spectrum_ms = now + CLIENT_SPECTRUM_EVERY_MS;
	if (!strcmp(control_get("TX"), "ON")) {
		client_modulation_send();
		return;
	}
	for (bin = 0; bin < SPECTRUM_BINS; bin++)
		client_spectrum_line[SPECTRUM_BINS - 1 - bin]
			= spectrum_level(SPECTRUM_FIRST_BIN + bin);
	server_send_spectrum((int)(SPECTRUM_BINS * SPECTRUM_HZ_PER_BIN),
		client_spectrum_line, SPECTRUM_BINS);
}

// one channel of a snapshot, summed up: channel 0 is the receiver's
static void print_snapshot_channel(const char *name, const int32_t *samples,
		int channel) {
	long long sum = 0;
	long largest = 0;
	int zeros = 0;
	int repeats = 0;
	int32_t sample;
	int32_t before = 0;
	int frame;

	for (frame = 0; frame < AUDIO_SNAPSHOT_FRAMES; frame++) {
		sample = samples[frame * SNAPSHOT_CHANNELS + channel];
		sum += sample;
		if (labs((long)sample) > largest)
			largest = labs((long)sample);
		if (sample == 0)
			zeros++;
		if (frame > 0 && sample == before)
			repeats++;
		before = sample;
	}
	printf("snapshot: %s largest %ld, mean %lld, zero %d, repeated %d, of %d\n",
		name, largest, sum / AUDIO_SNAPSHOT_FRAMES, zeros, repeats,
		AUDIO_SNAPSHOT_FRAMES);
}

static double started_at_s;
static long start_check_at_ms;
static int is_start_check_due;
static int is_start_check_waiting;
static int start_check_reopenings;

static double monotonic_s(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec + now.tv_nsec / (double)(NS_PER_MS * MS_PER_SECOND);
}

// whether the kernel has logged an I2S sync error since this program started
static int was_sync_error_logged(void) {
	char line[KERNEL_LOG_LINE_MAX];
	FILE *log = popen(KERNEL_LOG_COMMAND, "r");
	double at_s;
	int found = 0;

	if (log == NULL)
		return 0;
	while (fgets(line, sizeof(line), log) != NULL) {
		if (strstr(line, SYNC_ERROR_TEXT) == NULL)
			continue;
		if (sscanf(line, "[%lf]", &at_s) == 1
				&& at_s >= started_at_s - SYNC_ERROR_SLACK_S)
			found = 1;
	}
	pclose(log);
	return found;
}

// the humps are suspected: the sound card is closed and opened again, and
// the check is made once more in a while, unless that has been done as
// often as it is to be
static void reopen_the_card_and_check_again(void) {
	if (start_check_reopenings >= START_CHECK_REOPENINGS) {
		printf("start check: the humps are still suspected after the sound card "
			"was reopened %d times\n", start_check_reopenings);
		fflush(stdout);
		return;
	}
	start_check_reopenings++;
	printf("start check: reopening the sound card\n");
	fflush(stdout);
	if (audio_reopen() < 0) {
		printf("start check: the sound card did not open again\n");
		return;
	}
	start_check_at_ms = now_ms() + START_CHECK_AFTER_MS;
	is_start_check_due = 1;
}

// the snapshot taken at the start is measured, and what was found is
// printed, logged and kept; if the humps are suspected the sound card is
// reopened
static void start_check_report(const int32_t *samples) {
	char when[START_CHECK_NAME_MAX];
	char name[START_CHECK_NAME_MAX * 2];
	char said[KERNEL_LOG_LINE_MAX];
	char for_panel[LINK_SAY_MAX + 1];
	const char *guess = "looks normal";
	const char *sync = "no";
	time_t now = time(NULL);
	double period_hz;
	double ripple_db;
	int is_suspected;
	FILE *file;

	ripple_db = start_check_ripple_db(samples, AUDIO_SNAPSHOT_FRAMES,
		SNAPSHOT_CHANNELS, &period_hz);
	if (ripple_db > START_CHECK_SUSPECT_DB)
		guess = "HUMPS SUSPECTED";
	if (was_sync_error_logged())
		sync = "yes";
	strftime(when, sizeof(when), "%Y-%m-%d_%H-%M-%S", localtime(&now));
	snprintf(said, sizeof(said),
		"start check: ripple %.1f dB at %.1f kHz, I2S sync error at this start: %s, %s",
		ripple_db, period_hz / HZ_PER_KHZ, sync, guess);
	printf("%s\n", said);
	fflush(stdout);
	snprintf(for_panel, sizeof(for_panel), "start: ripple %.1f dB, %s", ripple_db, guess);
	panel_say(for_panel);
	is_suspected = ripple_db > START_CHECK_SUSPECT_DB;

	file = fopen(START_CHECK_LOG, "a");
	if (file != NULL) {
		fprintf(file, "%s %s\n", when, said);
		fclose(file);
	}
	snprintf(name, sizeof(name), "data/start_check_%s.raw", when);
	file = fopen(name, "wb");
	if (file != NULL) {
		fwrite(samples, sizeof(int32_t), AUDIO_SNAPSHOT_FRAMES * SNAPSHOT_CHANNELS, file);
		fclose(file);
	}
	if (is_suspected)
		reopen_the_card_and_check_again();
}

static void start_check_poll(long now) {
	// the check is of the sound card's first blocks: it waits for the card
	if (!is_start_check_due || now < start_check_at_ms || !sdr_is_audio_open())
		return;
	is_start_check_due = 0;
	is_start_check_waiting = 1;
	audio_take_snapshot();
}

// a recording: every block from the sound card for so many seconds, both
// channels as they came, written out from the ring audio.c keeps them in
static FILE *record_file;
static long record_blocks_left;
static long record_blocks_written;

static void record_stop(void) {
	audio_record(0);
	fclose(record_file);
	record_file = NULL;
	printf("record: done, %ld blocks written, %ld lost\n", record_blocks_written,
		audio_record_dropped());
	fflush(stdout);
}

// the file is named by the time it began, in UTC, and beside it is a note of
// how the radio was set, to read it back by
static void record_start(int seconds) {
	char name[RECORD_NAME_ROOM];
	char stamp[RECORD_STAMP_ROOM];
	FILE *note;
	time_t now = time(NULL);
	struct tm utc;

	if (record_file != NULL)
		record_stop();
	if (seconds <= 0)
		return;
	gmtime_r(&now, &utc);
	strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &utc);
	snprintf(name, sizeof(name), "data/record_%s.raw", stamp);
	record_file = fopen(name, "wb");
	if (record_file == NULL) {
		printf("record: could not write %s\n", name);
		return;
	}
	snprintf(name, sizeof(name), "data/record_%s.txt", stamp);
	note = fopen(name, "w");
	if (note != NULL) {
		fprintf(note, "began %s UTC, for %d seconds\n", stamp, seconds);
		fprintf(note, "frequency %s Hz, mode %s, pitch %s Hz, wpm %s, bw_cw %s Hz\n",
			control_get("FREQ"), control_get("MODE"), control_get("PITCH"),
			control_get("WPM"), control_get("BW_CW"));
		fprintf(note, "board %s, centre bin %d\n", board_in_use()->name,
			board_in_use()->centre_bin);
		fprintf(note, "samples: %d a second, int32, little endian, two channels, "
			"the receiver's then the microphone's, %d frames to a block\n",
			SAMPLE_RATE_HZ, AUDIO_BLOCK_FRAMES);
		fclose(note);
	}
	record_blocks_left = ((long)seconds * SAMPLE_RATE_HZ + AUDIO_BLOCK_FRAMES - 1)
		/ AUDIO_BLOCK_FRAMES;
	record_blocks_written = 0;
	audio_record(1);
	printf("record: %d seconds into data/record_%s.raw\n", seconds, stamp);
	fflush(stdout);
}

static void record_save(void) {
	const int32_t *block;

	if (record_file == NULL)
		return;
	while (record_blocks_left > 0 && (block = audio_record_next()) != NULL) {
		fwrite(block, sizeof(int32_t), AUDIO_BLOCK_FRAMES * SNAPSHOT_CHANNELS, record_file);
		audio_record_taken();
		record_blocks_left--;
		record_blocks_written++;
	}
	if (record_blocks_left == 0)
		record_stop();
}

// a recording of what is sent: for so many seconds of transmitting, each
// block the modem made and the transmitter was given, as core_sdr.c keeps
// them in its ring. Only blocks sent are recorded, so the seconds are
// seconds on the air, and "txrecord=0" stops it early
static FILE *tx_record_file;
static long tx_record_blocks_left;
static long tx_record_blocks_written;

static void tx_record_stop(void) {
	tx_record(0);
	fclose(tx_record_file);
	tx_record_file = NULL;
	printf("txrecord: done, %ld blocks written, %ld lost\n", tx_record_blocks_written,
		tx_record_dropped());
	fflush(stdout);
}

static void tx_record_start(int seconds) {
	char name[RECORD_NAME_ROOM];
	char stamp[RECORD_STAMP_ROOM];
	FILE *note;
	time_t now = time(NULL);
	struct tm utc;

	if (tx_record_file != NULL)
		tx_record_stop();
	if (seconds <= 0)
		return;
	gmtime_r(&now, &utc);
	strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &utc);
	snprintf(name, sizeof(name), "data/txrecord_%s.raw", stamp);
	tx_record_file = fopen(name, "wb");
	if (tx_record_file == NULL) {
		printf("txrecord: could not write %s\n", name);
		return;
	}
	snprintf(name, sizeof(name), "data/txrecord_%s.txt", stamp);
	note = fopen(name, "w");
	if (note != NULL) {
		fprintf(note, "began %s UTC, for %d seconds of transmitting\n", stamp, seconds);
		fprintf(note, "frequency %s Hz, mode %s, drive %s, mic %s\n",
			control_get("FREQ"), control_get("MODE"), control_get("DRIVE"),
			control_get("MIC"));
		fprintf(note, "board %s, centre bin %d\n", board_in_use()->name,
			board_in_use()->centre_bin);
		fprintf(note, "samples: %d a second, float32, little endian, two channels, "
			"the modem's as its tx() made them, then the transmitter's as the "
			"sound card is given them, %d frames to a block, only while "
			"transmitting\n", SAMPLE_RATE_HZ, AUDIO_BLOCK_FRAMES);
		fclose(note);
	}
	tx_record_blocks_left = ((long)seconds * SAMPLE_RATE_HZ + AUDIO_BLOCK_FRAMES - 1)
		/ AUDIO_BLOCK_FRAMES;
	tx_record_blocks_written = 0;
	tx_record(1);
	printf("txrecord: %d seconds of transmitting into data/txrecord_%s.raw\n", seconds,
		stamp);
	fflush(stdout);
}

static void tx_record_save(void) {
	const float *block;

	if (tx_record_file == NULL)
		return;
	while (tx_record_blocks_left > 0 && (block = tx_record_next()) != NULL) {
		fwrite(block, sizeof(float), AUDIO_BLOCK_FRAMES * 2, tx_record_file);
		tx_record_taken();
		tx_record_blocks_left--;
		tx_record_blocks_written++;
	}
	if (tx_record_blocks_left == 0)
		tx_record_stop();
}

static void set_setting(const char *name, const char *value, int origin);

// a recording played back: the main loop keeps the ring full from the
// file, and the audio thread takes a block from it in place of each the
// card gives
static FILE *play_file;
static long play_blocks;

static void play_stop(void) {
	audio_play(0);
	fclose(play_file);
	play_file = NULL;
	printf("play: done, %ld blocks, %ld not ready in time\n", play_blocks,
		audio_play_underruns());
	fflush(stdout);
}

// the radio is set as the note beside the recording says it was: the mode,
// the pitch and the speed, so that it listens as it did
static void set_as_recorded(const char *raw_path) {
	char path[RECORD_NAME_ROOM];
	char line[NOTE_LINE_ROOM];
	char value[NOTE_VALUE_ROOM];
	char *at;
	FILE *note;
	size_t length = strlen(raw_path);

	if (length < strlen(".raw") || length >= sizeof(path))
		return;
	snprintf(path, sizeof(path), "%.*s.txt", (int)(length - strlen(".raw")), raw_path);
	note = fopen(path, "r");
	if (note == NULL)
		return;
	while (fgets(line, sizeof(line), note) != NULL) {
		at = strstr(line, "mode ");
		if (at != NULL && sscanf(at + strlen("mode "), "%15[A-Z0-9]", value) == 1)
			set_setting("MODE", value, ORIGIN_TERMINAL);
		at = strstr(line, "pitch ");
		if (at != NULL && sscanf(at + strlen("pitch "), "%15[0-9]", value) == 1)
			set_setting("PITCH", value, ORIGIN_TERMINAL);
		at = strstr(line, "wpm ");
		if (at != NULL && sscanf(at + strlen("wpm "), "%15[0-9]", value) == 1)
			set_setting("WPM", value, ORIGIN_TERMINAL);
	}
	fclose(note);
}

static void play_start(const char *path) {
	if (play_file != NULL)
		play_stop();
	if (path[0] == 0 || !strcmp(path, "0"))
		return;
	play_file = fopen(path, "rb");
	if (play_file == NULL) {
		printf("play: %s cannot be read\n", path);
		return;
	}
	set_as_recorded(path);
	play_blocks = 0;
	audio_play(1);
	printf("play: %s, in place of what the sound card hears\n", path);
	fflush(stdout);
}

static void play_fill(void) {
	int32_t block[AUDIO_BLOCK_FRAMES * SNAPSHOT_CHANNELS];

	if (play_file == NULL)
		return;
	while (audio_play_has_room()) {
		if (fread(block, sizeof(int32_t), AUDIO_BLOCK_FRAMES * SNAPSHOT_CHANNELS, play_file)
				!= AUDIO_BLOCK_FRAMES * SNAPSHOT_CHANNELS) {
			if (audio_play_is_dry())
				play_stop();
			return;
		}
		audio_play_put(block);
		play_blocks++;
	}
}

static void snapshot_save(void) {
	const int32_t *samples = audio_get_snapshot();
	FILE *file;

	if (samples == NULL)
		return;
	if (is_start_check_waiting) {
		is_start_check_waiting = 0;
		start_check_report(samples);
		return;
	}
	print_snapshot_channel("receiver", samples, 0);
	print_snapshot_channel("microphone", samples, 1);
	file = fopen(SNAPSHOT_FILE, "wb");
	if (file == NULL) {
		printf("snapshot: could not write %s\n", SNAPSHOT_FILE);
		return;
	}
	fwrite(samples, sizeof(int32_t), AUDIO_SNAPSHOT_FRAMES * SNAPSHOT_CHANNELS, file);
	fclose(file);
	printf("snapshot: saved in %s\n", SNAPSHOT_FILE);
	fflush(stdout);
}

static void measure_start(int seconds, long now) {
	int i;

	for (i = 0; i < MEASURED_BINS; i++)
		measured_power[i] = 0;
	measured_times = 0;
	next_measure_ms = now;
	measure_until_ms = now + seconds * MS_PER_SECOND;
	is_measuring = 1;
}

static int compare_doubles(const void *a, const void *b) {
	double first = *(const double *)a;
	double second = *(const double *)b;

	if (first < second)
		return -1;
	if (first > second)
		return 1;
	return 0;
}

static void print_measurement(void) {
	double mean = 0;
	int i;

	for (i = 0; i < MEASURED_BINS; i++) {
		measured_power[i] /= measured_times;
		mean += measured_power[i];
	}
	mean /= MEASURED_BINS;
	qsort(measured_power, MEASURED_BINS, sizeof(double), compare_doubles);
	printf("measured: mean %.1f dB, middle %.1f dB, over %d bins and %d looks\n",
		10 * log10(mean + SMALLEST_POWER),
		10 * log10(measured_power[MEASURED_BINS / 2] + SMALLEST_POWER),
		MEASURED_BINS, measured_times);
	fflush(stdout);
}

static void measure_update(long now) {
	int i;

	if (!is_measuring || now < next_measure_ms)
		return;
	if (now >= measure_until_ms) {
		is_measuring = 0;
		print_measurement();
		return;
	}
	next_measure_ms = now + MEASURE_EVERY_MS;
	for (i = 0; i < MEASURED_BINS; i++)
		measured_power[i] += (double)fft_bins[MEASURED_FIRST_BIN + i]
			* fft_bins[MEASURED_FIRST_BIN + i];
	measured_times++;
}

static void on_control_changed(const struct control *c, int origin) {
	server_broadcast(c, origin);
	if (origin != ORIGIN_PANEL)
		panel_controls_changed(c);
	settings_changed(c, now_ms());
	if (!strcmp(c->group, "METER"))
		return;
	printf("%s %s\n", c->name, c->value);
	fflush(stdout);
}

static void set_setting(const char *name, const char *value, int origin) {
	char why[CONTROL_WHY_MAX];

	if (control_set(name, value, origin, why, sizeof(why)) < 0) {
		printf("ERROR %s %s\n", name, why);
		if (origin == ORIGIN_PANEL) {
			panel_say(why);
			// the panel has shown the change it asked for: it is told
			// the value the radio kept, or it goes on showing the other
			for (int i = 0; i < control_count(); i++) {
				if (!strcmp(control_at(i)->name, name))
					panel_controls_changed(control_at(i));
			}
		}
	}
}

static void on_panel_record(int control, int32_t value) {
	char text[CONTROL_VALUE_MAX];
	const char *name;

	if (control == LINK_HELLO) {
		printf("panel: HELLO, version %d\n", (int)value);
		panel_controls_describe();
		fflush(stdout);
		return;
	}
	name = panel_controls_took(control, value, text, sizeof(text));
	if (name != NULL)
		set_setting(name, text, ORIGIN_PANEL);
	fflush(stdout);
}

// a text from the panel, which comes in parts; when it is whole the control
// is set
static void on_panel_bytes(int control, const uint8_t *bytes, int count) {
	char text[LINK_TEXT_MAX + 1];
	const char *name;

	name = panel_controls_took_bytes(control, bytes, count, text, sizeof(text));
	if (name != NULL)
		set_setting(name, text, ORIGIN_PANEL);
	panel_wifi_took_bytes(control, bytes, count, now_ms());
	panel_log_took_bytes(control, bytes, count);
	panel_messages_took_bytes(control, bytes, count);
	fflush(stdout);
}

static int begins_with(const char *line, const char *with) {
	return strncmp(line, with, strlen(with)) == 0;
}

static void handle_command(char *line) {
	char response[RESPONSE_MAX];

	if (line[0] == 0)
		return;
	if (strcmp(line, "quit") == 0) {
		is_done = 1;
		return;
	}
	if (strcmp(line, "report") == 0) {
		report();
		return;
	}
	// "say <text>": the text on the panel's status line and to every
	// client
	if (begins_with(line, "say ")) {
		panel_say(line + strlen("say "));
		return;
	}
	if (begins_with(line, PLAY_FOR)) {
		play_start(line + strlen(PLAY_FOR));
		return;
	}
	if (begins_with(line, TX_RECORD_FOR)) {
		tx_record_start(atoi(line + strlen(TX_RECORD_FOR)));
		return;
	}
	if (begins_with(line, RECORD_FOR)) {
		record_start(atoi(line + strlen(RECORD_FOR)));
		return;
	}
	if (strcmp(line, "snapshot") == 0) {
		audio_take_snapshot();
		printf("ok\n");
		return;
	}
	if (strcmp(line, "panel=off") == 0) {
		panel_set_polling(0);
		printf("ok\n");
		return;
	}
	if (strcmp(line, "panel=on") == 0) {
		panel_set_polling(1);
		printf("ok\n");
		return;
	}
	if (strcmp(line, "spectrum=off") == 0) {
		is_sending_spectrum = 0;
		printf("ok\n");
		return;
	}
	if (strcmp(line, "spectrum=on") == 0) {
		is_sending_spectrum = 1;
		printf("ok\n");
		return;
	}
	if (begins_with(line, MEASURE_FOR)) {
		measure_start(atoi(line + strlen(MEASURE_FOR)), now_ms());
		printf("ok\n");
		return;
	}
	if (strcmp(line, "vfo=off") == 0) {
		si5351_clock_off(VFO_CLOCK);
		printf("ok\n");
		return;
	}
	if (strcmp(line, "vfo=on") == 0) {
		si5351_set_freq(VFO_CLOCK, oscillator_frequency());
		si5351_reset_plls();
		printf("ok\n");
		return;
	}
	if (strcmp(line, "bfo=off") == 0) {
		si5351_clock_off(BFO_CLOCK);
		printf("ok\n");
		return;
	}
	if (strcmp(line, "bfo=on") == 0) {
		si5351_set_freq(BFO_CLOCK, bfo_freq);
		si5351_reset_plls();
		printf("ok\n");
		return;
	}
	if (begins_with(line, ANSWER)) {
		ft8_qso_answer_call(line + strlen(ANSWER));
		return;
	}
	if (begins_with(line, LOG_FOR)) {
		panel_log_list(line + strlen(LOG_FOR));
		printf("ok\n");
		return;
	}
	if (strcmp(line, "audio=reset") == 0) {
		if (audio_reopen() < 0)
			printf("audio=reset: the sound card did not open again\n");
		else
			printf("ok\n");
		return;
	}
	if (strcmp(line, "si5351=reset") == 0) {
		si5351_init();
		si5351_set_freq(BFO_CLOCK, bfo_freq);
		si5351_set_freq(VFO_CLOCK, oscillator_frequency());
		si5351_reset_plls();
		printf("ok\n");
		return;
	}
	if (begins_with(line, FREQ_IS)) {
		set_setting("FREQ", line + strlen(FREQ_IS), ORIGIN_TERMINAL);
		return;
	}
	if (begins_with(line, VOLUME_IS)) {
		set_setting("AUDIO", line + strlen(VOLUME_IS), ORIGIN_TERMINAL);
		return;
	}
	if (strchr(line, '=') == NULL && strchr(line, ' ') != NULL) {
		char *value = strchr(line, ' ');
		*value = 0;
		set_setting(line, value + 1, ORIGIN_TERMINAL);
		return;
	}
	sdr_request(line, response);
	printf("%s\n", response);
}

static void read_terminal(void) {
	static char line[LINE_MAX_LENGTH];
	static int length;
	char c;
	int got;

	got = read(TERMINAL, &c, 1);
	while (got == 1) {
		if (c == '\n' || c == '\r') {
			line[length] = 0;
			length = 0;
			handle_command(line);
			fflush(stdout);
			return;
		}
		if (length < LINE_MAX_LENGTH - 1)
			line[length++] = c;
		got = read(TERMINAL, &c, 1);
	}
	if (got == 0)
		is_done = 1;
}

// whether the PTT is held, once its level has stayed the same for
// PTT_SETTLED_MS
static int is_ptt_held(long now) {
	int level = board_ptt_closed();

	if (level != ptt_level_seen) {
		ptt_level_seen = level;
		ptt_level_since_ms = now;
	}
	if (now - ptt_level_since_ms >= PTT_SETTLED_MS)
		ptt_level = ptt_level_seen;
	return ptt_level;
}

// whether the modem of the mode in use has something to send
static int is_modem_wanting_tx(void) {
	const struct modem *m = modem_find(control_get("MODE"));

	return m != NULL && m->wants_tx != NULL && m->wants_tx();
}

// VoIP: in the mode VOIP the PTT is VOIP_TX and not TX; another's speech
// is heard while VOIP is ONLINE and the radio receives, in any mode while
// BREAK_IN is ON: the author ruled on 16 October 2026 that break-in is not
// for the voice modes alone, and that it stays so with the sound devices for other programs
// always there. So a program such as WSJT-X or fldigi hears the speech in
// place of the band while someone speaks, until there is a mixer, which
// the author has put off. NEW_COOKIE's first tap is let go if the second
// does not come
static void follow_voip(long now) {
	static int was_held;
	char heard[CONTROL_VALUE_MAX];
	int is_online = !strcmp(control_get("VOIP"), "ONLINE");
	int is_voip_mode = !strcmp(control_get("MODE"), "VOIP");
	int is_held = is_voip_mode && is_ptt_held(now);
	int is_speaking = !strcmp(control_get("VOIP_TX"), "ON");

	if (is_held && !was_held && !is_speaking)
		set_setting("VOIP_TX", "ON", ORIGIN_NONE);
	if (!is_held && was_held && is_speaking)
		set_setting("VOIP_TX", "OFF", ORIGIN_NONE);
	was_held = is_held;
	is_speaking = !strcmp(control_get("VOIP_TX"), "ON");
	if (!is_speaking)
		voip_tx_began_ms = NOT_TRANSMITTING;
	else if (voip_tx_began_ms == NOT_TRANSMITTING)
		voip_tx_began_ms = now;
	// the mode left, VOIP gone offline, or held too long by a client that
	// went away: the owner's speech stops
	if (is_speaking && (!is_voip_mode || !is_online || now - voip_tx_began_ms > TX_LIMIT_MS)) {
		set_setting("VOIP_TX", "OFF", ORIGIN_NONE);
		is_speaking = 0;
	}
	rtp_voip_set_speaking(is_speaking);
	rtp_voip_set_hearing(is_online && strcmp(control_get("TX"), "ON")
		&& (is_voip_mode || !strcmp(control_get("BREAK_IN"), "ON")));
	rtp_voip_heard_callsign(heard, sizeof(heard));
	if (strcmp(heard, control_get("VOIP_SPEAKER")))
		set_setting("VOIP_SPEAKER", heard, ORIGIN_NONE);

	if (!strcmp(control_get("NEW_COOKIE"), "-"))
		new_cookie_asked_ms = NOT_ASKED;
	else if (new_cookie_asked_ms == NOT_ASKED)
		new_cookie_asked_ms = now;
	else if (now - new_cookie_asked_ms > NEW_COOKIE_WAIT_MS)
		set_setting("NEW_COOKIE", "-", ORIGIN_NONE);
}

static int is_ptt_mode(void) {
	const char *mode = control_get("MODE");

	return !strcmp(mode, "USB") || !strcmp(mode, "LSB") || !strcmp(mode, "2TONE")
		|| !strcmp(mode, "RADE1") || !strcmp(mode, "RADE2");
}

// the PTT, or a modem with something to send, puts the radio in transmit
// and takes it out again, and no transmission is let run past TX_LIMIT_MS
static void follow_ptt(long now) {
	static int was_held;
	const struct modem *m;
	int is_held = is_ptt_held(now) && is_ptt_mode();
	int is_keyed;

	// a modem that ends its over with something of its own is told when
	// the PTT that keyed the radio is let go, and may then keep the radio
	// sending for as long as it needs
	if (was_held && !is_held && is_ptt_transmitting) {
		m = modem_find(control_get("MODE"));
		if (m != NULL && m->tx_end != NULL)
			m->tx_end();
	}
	was_held = is_held;
	is_keyed = is_held || is_modem_wanting_tx();

	if (!is_keyed)
		is_ptt_to_be_let_go = 0;
	if (strcmp(control_get("TX"), "ON")) {
		tx_began_ms = NOT_TRANSMITTING;
		is_ptt_transmitting = 0;
		if (is_keyed && !is_ptt_to_be_let_go) {
			set_setting("TX", "ON", ORIGIN_NONE);
			is_ptt_transmitting = 1;
		}
		return;
	}
	if (tx_began_ms == NOT_TRANSMITTING)
		tx_began_ms = now;
	if (is_ptt_transmitting && !is_keyed) {
		set_setting("TX", "OFF", ORIGIN_NONE);
		return;
	}
	if (now - tx_began_ms > TX_LIMIT_MS) {
		printf("transmitting for more than %d seconds: back to receive\n",
			TX_LIMIT_MS / MS_PER_SECOND);
		set_setting("TX", "OFF", ORIGIN_NONE);
		is_ptt_to_be_let_go = 1;
	}
}

static void on_signal(int signal_number) {
	(void)signal_number;
	is_done = 1;
}

// the panel is told the seconds of the day, in UTC, as each second begins.
// The radio's clock is asked only when the kernel's second has changed,
// since asking it reads a file
static void utc_send(void) {
	static time_t looked;
	static time_t sent;
	time_t now;

	if (time(NULL) == looked)
		return;
	looked = time(NULL);
	if (radio_clock_now(&now) == RADIO_CLOCK_NOT_SET) {
		if (sent != 0)
			panel_set(LINK_UTC, LINK_UTC_NOT_SET);
		sent = 0;
		return;
	}
	if (now == sent)
		return;
	sent = now;
	panel_set(LINK_UTC, (int32_t)(now % SECONDS_A_DAY));
}

// what has been copied off the air since this was last called
static void messages_print(void) {
	static unsigned long last_printed;
	struct message m;
	struct tm utc;
	int printed = 0;

	while (message_next(&last_printed, &m)) {
		gmtime_r(&m.at, &utc);
		if (m.is_sent)
			printf("%s %02d%02d%02d  TX %4d  %s\n", m.mode, utc.tm_hour, utc.tm_min,
				utc.tm_sec, m.pitch_hz, m.text);
		else
			printf("%s %02d%02d%02d %+03d %4d  %s\n", m.mode, utc.tm_hour, utc.tm_min,
				utc.tm_sec, m.snr_db, m.pitch_hz, m.text);
		printed = 1;
	}
	if (printed)
		fflush(stdout);
}

// a client's click on a message, as the panel's is
static void answer_message(const char *line) {
	ft8_qso_answer(line);
}

// the sound card, tried again each AUDIO_RETRY_MS until it opens: until
// then the panel's status line and every client say so, again each
// AUDIO_SAID_EVERY_MS, and when it opens they are told that too.
// core_sdr.c says why the radio starts without it
#define AUDIO_RETRY_MS 1000
#define AUDIO_SAID_EVERY_MS 10000
static void follow_sound_card(long now) {
	static long tried_ms;
	static long said_ms;
	static int was_missing;

	if (sdr_is_audio_open() || now - tried_ms < AUDIO_RETRY_MS)
		return;
	tried_ms = now;
	if (sdr_open_audio()) {
		printf("the sound card is open\n");
		fflush(stdout);
		if (was_missing)
			panel_say("The sound card is open");
		start_check_at_ms = now + START_CHECK_AFTER_MS;
		return;
	}
	was_missing = 1;
	if (said_ms == 0 || now - said_ms >= AUDIO_SAID_EVERY_MS) {
		said_ms = now;
		printf("the sound card did not open; trying again every second\n");
		fflush(stdout);
		panel_warn("Sound card not found; trying again");
	}
}

static void main_loop(void) {
	struct pollfd polls[1 + SERVER_POLLS];
	int count;

	while (!is_done) {
		spectrum_send(now_ms());
		client_spectrum_send(now_ms());
		measure_update(now_ms());
		follow_sound_card(now_ms());
		follow_update(now_ms());
		start_check_poll(now_ms());
		snapshot_save();
		record_save();
		tx_record_save();
		play_fill();
		follow_ptt(now_ms());
		follow_voip(now_ms());
		panel_poll(now_ms(), on_panel_record, on_panel_bytes);
		panel_controls_poll();
		settings_poll(now_ms());
		panel_wifi_poll(now_ms());
		panel_log_poll();
		messages_print();
		utc_send();
		panel_cw_poll(now_ms());
		ft8_qso_poll();
		panel_messages_poll();
		server_send_messages();
		server_send_log();
		radio_poll(now_ms());
		meter_poll(now_ms());
		rtp_audio_poll();
		rtp_microphone_poll();
		rtp_voip_poll();

		polls[0].fd = TERMINAL;
		polls[0].events = POLLIN;
		polls[0].revents = 0;
		count = 1 + server_get_pollfds(polls + 1);
		if (poll(polls, count, WAIT_ON_TERMINAL_MS) <= 0)
			continue;
		server_handle_events(polls + 1, count - 1);
		if (polls[0].revents & (POLLIN | POLLHUP))
			read_terminal();
	}
}

int main(int argc, char **argv) {
	const char *hz;
	const char *mode;
	const char *volume;

	hz = NULL;
	mode = NULL;
	volume = NULL;
	if (argc > 1)
		hz = argv[1];
	if (argc > 2)
		mode = argv[2];
	if (argc > 3)
		volume = argv[3];

	updated_take();
	started_at_s = monotonic_s();
	start_check_at_ms = now_ms() + START_CHECK_AFTER_MS;
	is_start_check_due = 1;
	if (setup() < 0) {
		printf("the radio did not open\n");
		return 1;
	}
	printf("radio open on the %s\n", board_in_use()->name);

	if (rtp_audio_open() < 0)
		printf("the radio's sound is not being sent out on the radio: no socket for it\n");
	if (rtp_microphone_open() < 0)
		printf("speech from afar cannot be transmitted: port %d on the radio is taken\n",
			RTP_MICROPHONE_PORT);
	if (rtp_voip_open() < 0)
		printf("the owner's speech cannot go to VoIP: no socket for it\n");
	panel_open();
	radio_open();
	meter_open();
	control_register(&span);
	control_register(&step);
	control_register(&vfo);
	control_register(&vfo_a);
	control_register(&vfo_b);
	// RADIO_KEY is the first of the CONNECTIONS, the author's order: it
	// is what an owner comes to the screen for, to add the radio
	control_register(&radio_key);
	control_register(&remote);
	control_register(&streaming);
	control_register(&server);
	control_register(&remote_status);
	control_register(&bluetooth);
	control_register(&bluetooth_status);
	control_register(&sound_card_status);
	control_register(&cat);
	control_register(&cat_status);
	control_register(&new_cookie);
	control_register(&update);
	control_register(&update_status);
	control_register(&voip);
	control_register(&break_in);
	control_register(&voip_status);
	control_register(&voip_tx);
	control_register(&voip_speaker);
	panel_wifi_open();
	panel_log_open(LOGBOOK_FILE);
	ft8_qso_open();
	logger_open();
	settings_set_extras(radio_save_bands, radio_load_band);
	settings_load(SETTINGS_FILE);
	control_on_change(on_control_changed);
	if (updated_said[0] != 0) {
		set_setting("UPDATE_STATUS", updated_said, ORIGIN_NONE);
		update_said_until_ms = now_ms() + UPDATE_SAID_FOR_MS;
	}
	if (hz != NULL)
		set_setting("FREQ", hz, ORIGIN_TERMINAL);
	if (mode != NULL)
		set_setting("MODE", mode, ORIGIN_TERMINAL);
	if (volume != NULL)
		set_setting("AUDIO", volume, ORIGIN_TERMINAL);
	panel_controls_describe();

	server_on_answer(answer_message);
	panel_also_tell(server_say, server_warn, server_show_qso);
	panel_cw_also_tell(server_send_cw);
	panel_cw_also_tell_sent(server_send_cw_sent);
	if (server_open(SERVER_PORT) < 0)
		printf("clients cannot connect: port %d is not free\n", SERVER_PORT);
	else
		printf("clients connect on port %d\n", SERVER_PORT);
	fflush(stdout);

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);
	main_loop();
	set_setting("TX", "OFF", ORIGIN_NONE);
	settings_save();
	report();
	server_close();
	teardown();
	if (is_restarting) {
		restart_as_built(argv);
		return 1;
	}
	return 0;
}
