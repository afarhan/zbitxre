// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// zbitx_remote.c — the radio's way out to my.sbitx.net, so that its owner
// can reach it from anywhere.
//
//     zbitx_remote [--server my.sbitx.net] [--port 8081] [--radio-port 8081]
//                  [--key data/remote.key] [--microphone-port 5006]
//                  [--bluetooth-script setup/bluetooth_audio.sh]
//                  [--sound-card-script setup/sound_card.py]
//                  [--cat-port 4532]
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
// then ten, then every fifteen; while the website does not know the radio,
// it is tried every quarter of a minute. The ceiling was a minute until
// the zbitx, on a Wi-Fi that lost a packet in ten, failed a few tries
// after a boot and then sat for minutes saying NO ANSWER FROM SERVER
// between tries, while the author waited for it.
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
// as RTP to a multicast group; rtp_audio.c says how. That is the sound
// whole, 16-bit at 48000 samples a second, for other programs on the radio
// to have; what goes across the internet is the telephone's, mu-law at
// 8000, and telephone.c turns the one into the other here, both ways. While somebody afar is
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
// WHAT THE OWNER ALLOWS
//
// There are two switches, among the radio's CONNECTIONS, and they are the
// owner's, set at the radio: REMOTE, whether the radio may be worked from
// afar through my.sbitx.net, and STREAMING, whether anyone may listen to
// it there. With either on, this program is on line, and tells the
// service which, "ALLOWS remote streaming", when it comes on line and
// whenever one is changed. With both off it holds no connection to
// anything outside the radio. The service turns away what is not allowed;
// so does this program, which opens no way to the radio program for
// anyone while REMOTE is off, lets go at once of whoever is working it
// when REMOTE is turned off, and sends nothing of what the radio copies
// in FT8 while STREAMING is off. The radio's sound it sends when the
// service asks, with either on: the owner working the radio from afar has
// to hear it, and the service knows who is asking.
//
// A BLUETOOTH EARPHONE
//
// The third of the radio's CONNECTIONS is BLUETOOTH: the radio's sound in
// an earphone or a speaker paired with the radio's computer, which the
// author asked to have among them. setup/bluetooth_audio.sh does the
// playing, and says how; this program runs it. With BLUETOOTH on it runs
// the script's "start" at once and again every ten seconds, since the
// script does nothing if it is playing already and an earphone may be
// switched on after the radio is; with BLUETOOTH turned off it runs
// "stop". The script ends what it says with a line that begins "STATUS ",
// and the rest of that line is what is shown, BLUETOOTH_STATUS: PLAYING,
// NO EARPHONE CONNECTED, or with the switch off, OFF. The script is not
// waited for:
// it is run apart and what it says is read when it has said it, so that
// the radio's sound on its way to my.sbitx.net is not held up for it.
// Pairing the earphone is done from the desktop's Bluetooth menu. This
// has nothing to do with my.sbitx.net and works with REMOTE and STREAMING
// off.
//
// A SOUND CARD FOR OTHER PROGRAMS
//
// The fourth is SOUND_CARD: two sound devices that are not sound cards,
// for another program on the radio to hear the receiver by and to send
// by, WSJT-X or FreeDV or any that works a radio through its sound.
// setup/sound_card.py makes them and carries the sound, and says how; it
// is run for this switch as the earphone's script is for BLUETOOTH, and
// SOUND_CARD_STATUS is what it says: the names of the two devices to
// choose in the other program.
//
// CAT, FOR OTHER PROGRAMS TO SET THE RADIO BY
//
// The fifth is CAT. A program that is given the radio's sound has also to
// set its frequency and to key it, and the programs do that through
// Hamlib. With CAT on, this program listens on port 4532 and answers what
// Hamlib's own rigctld would: in the other program the radio to choose is
// "Hamlib NET rigctl", and the address is what CAT_STATUS shows, which the
// author asked for, "192.168.1.82:4532". cat.c has the words and says what
// is understood; here are the connections. What a program sets is told to
// the radio program as any client tells it, and what it asks is answered
// from what the radio program last said, which this program is told as any
// client is, so that nothing waits.
//
// The port is open to the network the radio is on and not only to the
// radio itself, the address being shown so that a program on another
// computer in the shack can be given it. The radio program's own port is
// open to that network already, and whoever can reach it can do all that
// this allows and more. Up to CAT_CLIENTS_MAX programs may be connected.
// One that keyed the radio and goes without letting it go, or is cut off
// by CAT being turned off, has it let go for it. On the radio itself the
// address 127.0.0.1 does as well as the one shown. If something else has
// the port, CAT_STATUS says so and the port is tried again every ten
// seconds.
//
// WHAT THE RADIO COPIES IN FT8
//
// The author asked that the service be sent whatever the radio copies in
// FT8 and keep it, for anyone to see what a radio is hearing; what a radio
// hears is as public as what it plays. So this program asks the radio
// program for its messages, as a client that shows them does, and sends
// each one on the connection that is kept, with where the dial was:
// "DECODE 14074000 043015|-12|1797|CQ VU2ESE MK97". It does so for as long
// as it is on line, which is for as long as the owner has REMOTE on; what
// the radio copies while it is not is not sent. Should this program lose
// the radio program and find it again, it is told its last messages over
// again; the service knows one it has had already.
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
// VOIP
//
// The sixth of the CONNECTIONS is VOIP, ONLINE or OFFLINE: my.sbitx.net's
// repeater, where the dial is the room. accounts/voip.py has what the
// author asked for, and the packet. With VOIP online this program is on
// line as it is for REMOTE, says "voip" among what the owner allows, and is
// given a stream by the service, "VOIP STREAM <id> <nonce>", with two keys
// made as the sound's are, of the words "zbitx-voip-up-1" for what it
// sends and "zbitx-voip-down-1" for what it is sent. Every quarter of a
// minute, and within a second of the dial moving to another kilohertz, it
// sends a packet that says where the radio is tuned: that puts the radio
// in its channel, and keeps the way back through a home's router open.
//
// The owner's speech, in the mode VOIP with the PTT down, comes from the
// radio program to this program's port on the radio, VOIP_PORT, as RTP of
// the whole sound; rtp_voip.c sends it. It is made the telephone's and sent
// on, with the dial's frequency. Another's speech, from the service, is
// checked by its tag and its number, and by its frequency, which has to be
// in the channel the radio is tuned to; it is then handed to the radio
// program's port for speech, as the owner's speech to transmit is, but
// under the label that says it is to be heard, with the speaker's
// callsign, which the service wrote in from the speaker's account.
//
// What the service says of the channel is shown in VOIP_STATUS: "7074 KHZ,
// 3 HERE", how many radios are in it, the author's rule being a count and
// no names; and for a few seconds after the owner's speech was not passed
// on, why, "BUSY VU3XYZ" when another holds the channel, or "BUSY ONE
// MINUTE", "BUSY WAIT", "BUSY CHANNEL FULL", "BUSY SERVICE FULL".
//
// A NEW COOKIE
//
// The owner can have the radio make a new cookie, with NEW_COOKIE among the
// CONNECTIONS, which the radio program asks for two taps of: the author's
// way of putting right a cookie that has gone where it should not, a new
// key made on the radio and typed into the owner's list at my.sbitx.net,
// on the radio's Keys page, and the radio keeps its name. This program
// makes it, lets go of the service, and shows it in REMOTE_STATUS until the
// website has it.
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
#include <sys/wait.h>

#include "cat.h"
#include "rtp_audio.h"
#include "rtp_microphone.h"
#include "rtp_voip.h"
#include "sha256.h"
#include "telephone.h"

#define DEFAULT_SERVER "my.sbitx.net"
#define DEFAULT_PORT "8081"
#define DEFAULT_RADIO_PORT "8081"
#define MESSAGE_WORD "MESSAGE "
#define ALLOWED_ROOM 64
#define DEFAULT_BLUETOOTH_SCRIPT "setup/bluetooth_audio.sh"
#define DEFAULT_SOUND_CARD_SCRIPT "setup/sound_card.py"
#define HELPER_EVERY_MS 10000
#define HELPER_STATUS_WORD "STATUS "
// where the radio program listens for speech: RTP_MICROPHONE_PORT, as a word
#define DEFAULT_MICROPHONE_PORT "5006"
#define DEFAULT_CAT_PORT "4532"
// where the radio program sends the owner's speech for VoIP: RTP_VOIP_PORT
#define DEFAULT_VOIP_PORT "5007"
#define VOIP_STATUS_CONTROL "VOIP_STATUS"
#define VOIP_KIND_TUNED 0
#define VOIP_KIND_SPEECH 3
#define VOIP_CODEC_MULAW 0
#define VOIP_CALLSIGN_BYTES 10
// the stream's id, kind, codec, callsign, frequency, number, sound, tag
#define VOIP_HEAD_BYTES (1 + 1 + VOIP_CALLSIGN_BYTES + 4 + 4)
#define VOIP_PACKET_BYTES (STREAM_ID_BYTES + VOIP_HEAD_BYTES + TELEPHONE_FRAME_SAMPLES + TAG_BYTES)
#define VOIP_TUNED_EVERY_MS 15000
#define VOIP_TUNED_SOON_MS 1000
#define VOIP_BUSY_SHOWN_MS 3000
#define VOIP_AHEAD_MOST 3000
#define HZ_A_CHANNEL 1000
#define CAT_CLIENTS_MAX 4
#define CAT_WAITING_MOST 4
#define CAT_STATUS_CONTROL "CAT_STATUS"
// an address that is nowhere, kept for writing about networks; nothing is
// sent to it
#define NOWHERE "192.0.2.1"
#define NOWHERE_PORT 9
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
// across the internet the sound is the telephone's: RTP of 160 bytes of
// mu-law; on the radio it is the whole, RTP_PACKET_BYTES. telephone.c
#define NET_RTP_BYTES (RTP_HEADER_BYTES + TELEPHONE_FRAME_SAMPLES)
#define SOUND_PACKET_BYTES (STREAM_ID_BYTES + NET_RTP_BYTES + TAG_BYTES)
#define RTP_VERSION_2 0x80
#define RTP_PAYLOAD_PCMU 0
#define RTP_SOURCE_BYTES 4
#define LOOPBACK "127.0.0.1"
#define STEP_WAIT_MS 4000
#define LOOP_WAIT_MS 200
#define RADIO_RETRY_MS 2000
#define PING_EVERY_MS 60000
#define SILENCE_MOST_MS 150000
#define RETRY_FIRST_MS 5000
#define RETRY_MOST_MS 15000
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

// a program that sets the radio through Hamlib
struct cat_client {
	int fd;
	struct lines lines;
	int has_keyed;
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
static const char *cat_port = DEFAULT_CAT_PORT;
static const char *voip_port = DEFAULT_VOIP_PORT;

static const char *key_file = DEFAULT_KEY_FILE;

static enum state state = OFF;
static int is_wanted;
static int is_remote_allowed;
static int is_streaming_allowed;
static int is_voip_wanted;
static char allowed_told[ALLOWED_ROOM];
// a switch that a script does the work of: the switch's control and the one
// that says what came of it, the script, and how the last run of it stands
struct helper {
	const char *control;
	const char *status_control;
	const char *script;
	int is_wanted;
	int is_on;
	pid_t pid;
	int said_fd;
	char said[TEXT_ROOM];
	int said_used;
	long next_ms;
	char status[STATUS_ROOM];
};

static struct helper bluetooth = {
	.control = "BLUETOOTH", .status_control = "BLUETOOTH_STATUS",
	.script = DEFAULT_BLUETOOTH_SCRIPT, .said_fd = NOT_OPEN,
};
static struct helper sound_card = {
	.control = "SOUND_CARD", .status_control = "SOUND_CARD_STATUS",
	.script = DEFAULT_SOUND_CARD_SCRIPT, .said_fd = NOT_OPEN,
};
static int is_cat_wanted;
static int cat_listen_fd = NOT_OPEN;
static long cat_next_ms;
static char cat_status[STATUS_ROOM];
static struct cat_radio cat_radio = { .vfo = 'A' };
static struct cat_client cat_clients[CAT_CLIENTS_MAX];

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
static uint32_t sound_samples;
static int speech_out = NOT_OPEN;
static struct sockaddr_in speech_port;
static int is_speech_begun;
static uint16_t speech_sequence;
// what the telephone's filters keep, for each stream: telephone.c
static struct telephone sound_telephone;
static struct telephone speech_telephone;
// VoIP: the service's socket and the radio program's, the stream's id and
// keys, the numbers sent and taken, where the dial was said to be, and
// what the service said of the channel
static int voip_fd = NOT_OPEN;
static int voip_radio_fd = NOT_OPEN;
static uint8_t voip_id[STREAM_ID_BYTES];
static char voip_up_key[SHA256_HEX_ROOM];
static char voip_down_key[SHA256_HEX_ROOM];
static uint32_t voip_sent;
static uint32_t voip_taken;
static int is_voip_taken_begun;
static uint16_t voip_heard_sequence;
static long voip_tuned_ms;
static long voip_channel_told;
static char voip_room[STATUS_ROOM];
static char voip_busy[STATUS_ROOM];
static long voip_busy_ms;
static char voip_status[STATUS_ROOM];
static struct telephone voip_spoken_telephone;
static struct telephone voip_heard_telephone;
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
static char failed_because[TEXT_ROOM];

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
	error = getaddrinfo(host, service, &hints, &found);
	if (error != 0) {
		snprintf(failed_because, sizeof(failed_because), "%s not found: %s", host,
			gai_strerror(error));
		return NOT_OPEN;
	}
	fd = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
	if (fd < 0) {
		snprintf(failed_because, sizeof(failed_because), "no socket: %s", strerror(errno));
		freeaddrinfo(found);
		return NOT_OPEN;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	if (connect(fd, found->ai_addr, found->ai_addrlen) < 0 && errno != EINPROGRESS) {
		snprintf(failed_because, sizeof(failed_because), "%s: %s", host, strerror(errno));
		freeaddrinfo(found);
		close(fd);
		return NOT_OPEN;
	}
	freeaddrinfo(found);
	connected.fd = fd;
	connected.events = POLLOUT;
	if (poll(&connected, 1, STEP_WAIT_MS) <= 0
			|| getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error != 0) {
		snprintf(failed_because, sizeof(failed_because), "%s: %s", host,
			error != 0 ? strerror(error) : "no answer in time");
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

static void sessions_close(void);
static void cookie_renew(void);

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
				snprintf(cookie, sizeof(cookie), "%.*s", COOKIE_LENGTH, line);
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

static int no_answer(const char *because) {
	snprintf(failed_because, sizeof(failed_because), "%s", because);
	return AUTH_NO_ANSWER;
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
		return no_answer("no CHALLENGE came");
	if (sscanf(line, "CHALLENGE %64s", challenge) != 1)
		return no_answer("not a CHALLENGE");
	if (getrandom(random_bytes, sizeof(random_bytes), 0) != sizeof(random_bytes))
		return no_answer("no random bytes for the nonce");
	for (i = 0; i < NONCE_BYTES; i++)
		snprintf(nonce + 2 * i, 3, "%02x", random_bytes[i]);
	snprintf(text, sizeof(text), "zbitx-auth-1 radio %s %s %s", callsign, challenge, nonce);
	hmac_sha256_hex(stretched, text, proof);
	snprintf(line, sizeof(line), "AUTH radio %s %s %s\n", callsign, nonce, proof);
	if (send_text(fd, line) < 0)
		return no_answer("AUTH could not be sent");
	if (read_line_waiting(fd, line, sizeof(line)) < 0)
		return no_answer("no answer to AUTH");
	if (!strncmp(line, "ERROR AUTH", strlen("ERROR AUTH")))
		return AUTH_REFUSED;
	if (sscanf(line, "WELCOME %39s %80s", name, theirs) != 2)
		return no_answer("not a WELCOME");
	snprintf(text, sizeof(text), "zbitx-auth-1 server %s %s %s", callsign, challenge, nonce);
	hmac_sha256_hex(stretched, text, proof);
	if (!is_same(proof, theirs))
		return no_answer("the service's proof is wrong");
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

/* what a script does for a switch: a Bluetooth earphone, a sound card for
   other programs */

// what there is to say of a switch is put in the control that shows it,
// if it is not what was said last; said has STATUS_ROOM bytes
static void status_say(const char *control, char *said, const char *text) {
	char line[TEXT_ROOM];

	if (!strcmp(text, said))
		return;
	snprintf(said, STATUS_ROOM, "%s", text);
	printf("remote: %s %s\n", control, said);
	fflush(stdout);
	if (radio_fd == NOT_OPEN)
		return;
	snprintf(line, sizeof(line), "%s %s\n", control, said);
	if (send_text(radio_fd, line) < 0)
		close_fd(&radio_fd);
}

static void helper_say(struct helper *h, const char *text) {
	status_say(h->status_control, h->status, text);
}

// the script is run apart, with one word, and what it says will be read
static void helper_run(struct helper *h, const char *word) {
	int said[2];

	if (h->said_fd != NOT_OPEN || pipe(said) < 0)
		return;
	h->pid = fork();
	if (h->pid < 0) {
		close(said[0]);
		close(said[1]);
		return;
	}
	if (h->pid == 0) {
		dup2(said[1], STDOUT_FILENO);
		dup2(said[1], STDERR_FILENO);
		close(said[0]);
		close(said[1]);
		execl(h->script, h->script, word, (char *)NULL);
		printf("%sCOULD NOT BE RUN\n", HELPER_STATUS_WORD);
		_exit(EXIT_FAILURE);
	}
	close(said[1]);
	fcntl(said[0], F_SETFL, O_NONBLOCK);
	h->said_fd = said[0];
	h->said_used = 0;
}

// what the script has said so far is read; when it has finished, the line
// it began with "STATUS " is what there is to show
static void helper_hear(struct helper *h) {
	char *status;
	ssize_t got;

	if (h->said_fd == NOT_OPEN)
		return;
	got = read(h->said_fd, h->said + h->said_used, sizeof(h->said) - 1 - h->said_used);
	if (got > 0) {
		h->said_used += (int)got;
		return;
	}
	if (got < 0 && (errno == EAGAIN || errno == EINTR))
		return;
	h->said[h->said_used] = 0;
	close_fd(&h->said_fd);
	waitpid(h->pid, NULL, 0);
	status = strstr(h->said, HELPER_STATUS_WORD);
	if (!h->is_wanted)
		helper_say(h, "OFF");
	else if (status == NULL)
		helper_say(h, "COULD NOT BE STARTED");
	else {
		status += strlen(HELPER_STATUS_WORD);
		status[strcspn(status, "\r\n")] = 0;
		helper_say(h, status);
	}
}

static void helper_poll(struct helper *h, long now) {
	helper_hear(h);
	if (h->said_fd != NOT_OPEN)
		return;
	if (h->is_wanted && (!h->is_on || now >= h->next_ms)) {
		h->is_on = 1;
		h->next_ms = now + HELPER_EVERY_MS;
		helper_run(h, "start");
	}
	if (!h->is_wanted && h->is_on) {
		h->is_on = 0;
		helper_run(h, "stop");
	}
}

// a switch among the radio's CONNECTIONS is heard of: "BLUETOOTH ON"
static void helper_follow(struct helper *h, const char *line) {
	size_t length = strlen(h->control);

	if (strncmp(line, h->control, length) || line[length] != ' ')
		return;
	h->is_wanted = !strcmp(line + length + 1, "ON");
}

// what there is to say is said afresh, to a radio program just found
static void helper_say_again(struct helper *h) {
	char said[STATUS_ROOM];

	snprintf(said, sizeof(said), "%s", h->status[0] ? h->status : "OFF");
	h->status[0] = 0;
	helper_say(h, said);
}

/* CAT: programs that set the radio through Hamlib; cat.c has the words */

// the address this computer is reached at on its network, "192.168.1.82",
// or its own, 127.0.0.1, if it is on none. It is what the system would
// send from to somewhere far off; nothing is sent
static void local_address(char *text, int room) {
	struct sockaddr_in far;
	struct sockaddr_in here;
	socklen_t length = sizeof(here);
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	snprintf(text, room, "%s", LOOPBACK);
	if (fd < 0)
		return;
	memset(&far, 0, sizeof(far));
	far.sin_family = AF_INET;
	far.sin_port = htons(NOWHERE_PORT);
	inet_pton(AF_INET, NOWHERE, &far.sin_addr);
	if (connect(fd, (struct sockaddr *)&far, sizeof(far)) == 0
			&& getsockname(fd, (struct sockaddr *)&here, &length) == 0)
		inet_ntop(AF_INET, &here.sin_addr, text, room);
	close(fd);
}

static void cat_client_close(struct cat_client *c) {
	if (c->fd == NOT_OPEN)
		return;
	// a program that goes with the radio keyed has it let go for it
	if (c->has_keyed && radio_fd != NOT_OPEN) {
		send_text(radio_fd, "TX OFF\n");
		cat_radio.is_sending = 0;
	}
	c->has_keyed = 0;
	close_fd(&c->fd);
	printf("remote: a program has left CAT\n");
	fflush(stdout);
}

static void cat_close(void) {
	int i;

	for (i = 0; i < CAT_CLIENTS_MAX; i++)
		cat_client_close(&cat_clients[i]);
	close_fd(&cat_listen_fd);
}

static int cat_listen(void) {
	struct sockaddr_in here;
	int on = 1;
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0)
		return NOT_OPEN;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	memset(&here, 0, sizeof(here));
	here.sin_family = AF_INET;
	here.sin_addr.s_addr = htonl(INADDR_ANY);
	here.sin_port = htons((uint16_t)atoi(cat_port));
	if (bind(fd, (struct sockaddr *)&here, sizeof(here)) < 0 || listen(fd, CAT_WAITING_MOST) < 0) {
		close(fd);
		return NOT_OPEN;
	}
	fcntl(fd, F_SETFL, O_NONBLOCK);
	return fd;
}

// a program that has connected is given a place, if there is one
static void cat_accept(void) {
	struct sockaddr_in from;
	socklen_t length = sizeof(from);
	char address[INET_ADDRSTRLEN] = "";
	int on = 1;
	int fd = accept(cat_listen_fd, (struct sockaddr *)&from, &length);
	int i;

	if (fd < 0)
		return;
	for (i = 0; i < CAT_CLIENTS_MAX && cat_clients[i].fd != NOT_OPEN; i++)
		;
	if (i == CAT_CLIENTS_MAX) {
		close(fd);
		return;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
	cat_clients[i].fd = fd;
	cat_clients[i].lines.used = 0;
	cat_clients[i].has_keyed = 0;
	inet_ntop(AF_INET, &from.sin_addr, address, sizeof(address));
	printf("remote: a program has come for CAT, from %s\n", address);
	fflush(stdout);
}

// what a program has asked is answered, and the radio program told what it
// set
static void cat_client_poll(struct cat_client *c) {
	char line[LINE_ROOM];
	char answer[CAT_ANSWER_ROOM];
	char for_radio[CAT_FOR_RADIO_ROOM];
	int got;

	if (c->fd == NOT_OPEN)
		return;
	got = take_line(&c->lines, c->fd, line, sizeof(line));
	while (got > 0) {
		if (cat_answer(line, &cat_radio, answer, for_radio) == CAT_QUIT) {
			got = -1;
			break;
		}
		if (for_radio[0] != 0 && radio_fd != NOT_OPEN) {
			if (!strcmp(for_radio, "TX ON\n"))
				c->has_keyed = 1;
			if (!strcmp(for_radio, "TX OFF\n"))
				c->has_keyed = 0;
			if (send_text(radio_fd, for_radio) < 0)
				close_fd(&radio_fd);
		}
		if (answer[0] != 0 && send_text(c->fd, answer) < 0) {
			got = -1;
			break;
		}
		got = take_line(&c->lines, c->fd, line, sizeof(line));
	}
	if (got < 0)
		cat_client_close(c);
}

// what the radio program says of itself is kept, to answer from
static void cat_hear(const char *line) {
	int i;

	if (!strcmp(line, "CAT ON"))
		is_cat_wanted = 1;
	if (!strcmp(line, "CAT OFF"))
		is_cat_wanted = 0;
	sscanf(line, "FREQ %ld", &cat_radio.hz);
	sscanf(line, "MODE %15s", cat_radio.mode);
	if (!strcmp(line, "VFO A") || !strcmp(line, "VFO B"))
		cat_radio.vfo = line[strlen("VFO ")];
	if (!strcmp(line, "TX ON"))
		cat_radio.is_sending = 1;
	if (!strcmp(line, "TX OFF")) {
		cat_radio.is_sending = 0;
		// the radio has gone back to receive of itself, or by another
		// hand; no program here is left holding it
		for (i = 0; i < CAT_CLIENTS_MAX; i++)
			cat_clients[i].has_keyed = 0;
	}
}

static void cat_poll(long now) {
	char address[INET_ADDRSTRLEN];
	char said[STATUS_ROOM];
	int i;

	if (!is_cat_wanted) {
		cat_close();
		status_say(CAT_STATUS_CONTROL, cat_status, "OFF");
		cat_next_ms = 0;
		return;
	}
	// the port is opened, and where it is to be found is said, afresh
	// every so often: the radio may join a network after it is turned on
	if (now >= cat_next_ms) {
		cat_next_ms = now + HELPER_EVERY_MS;
		if (cat_listen_fd == NOT_OPEN)
			cat_listen_fd = cat_listen();
		if (cat_listen_fd == NOT_OPEN)
			snprintf(said, sizeof(said), "PORT %.5s IS IN USE", cat_port);
		else {
			local_address(address, sizeof(address));
			snprintf(said, sizeof(said), "%s:%.5s", address, cat_port);
		}
		status_say(CAT_STATUS_CONTROL, cat_status, said);
	}
	if (cat_listen_fd == NOT_OPEN)
		return;
	cat_accept();
	for (i = 0; i < CAT_CLIENTS_MAX; i++)
		cat_client_poll(&cat_clients[i]);
}

// a message the radio has copied in FT8 goes to the service, with where
// the dial is, if this program is on line
static void decode_pass_on(const char *message) {
	char line[LINE_ROOM];

	if (state != ONLINE || line_fd == NOT_OPEN || tuned_hz[0] == 0 || !is_streaming_allowed)
		return;
	snprintf(line, sizeof(line), "DECODE %s %.200s\n", tuned_hz, message);
	send_text(line_fd, line);
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
		// its messages copied in FT8 are asked for, to be sent on
		send_text(radio_fd, "messages on\n");
		// what there is to say is said afresh to a radio program just found
		snprintf(said, sizeof(said), "%s", status_said);
		status_said[0] = 0;
		say_status(said);
		helper_say_again(&bluetooth);
		helper_say_again(&sound_card);
		snprintf(said, sizeof(said), "%s", cat_status[0] ? cat_status : "OFF");
		cat_status[0] = 0;
		status_say(CAT_STATUS_CONTROL, cat_status, said);
		voip_status[0] = 0;
	}
	got = take_line(&radio_lines, radio_fd, line, sizeof(line));
	while (got > 0) {
		if (!strcmp(line, "REMOTE ON"))
			is_remote_allowed = 1;
		if (!strcmp(line, "REMOTE OFF"))
			is_remote_allowed = 0;
		if (!strcmp(line, "STREAMING ON"))
			is_streaming_allowed = 1;
		if (!strcmp(line, "STREAMING OFF"))
			is_streaming_allowed = 0;
		if (!strcmp(line, "VOIP ONLINE"))
			is_voip_wanted = 1;
		if (!strcmp(line, "VOIP OFFLINE"))
			is_voip_wanted = 0;
		is_wanted = is_remote_allowed || is_streaming_allowed || is_voip_wanted;
		if (!strcmp(line, "NEW_COOKIE MAKING"))
			cookie_renew();
		helper_follow(&bluetooth, line);
		helper_follow(&sound_card, line);
		cat_hear(line);
		sscanf(line, "FREQ %15s", tuned_hz);
		sscanf(line, "MODE %15s", tuned_mode);
		if (!strncmp(line, MESSAGE_WORD, strlen(MESSAGE_WORD)))
			decode_pass_on(line + strlen(MESSAGE_WORD));
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
		is_remote_allowed = 0;
		is_streaming_allowed = 0;
		is_voip_wanted = 0;
		is_wanted = 0;
		bluetooth.is_wanted = 0;
		sound_card.is_wanted = 0;
		is_cat_wanted = 0;
	}
	// with remote operation switched off, whoever is working the radio
	// from afar is let go at once
	if (!is_remote_allowed)
		sessions_close();
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
	telephone_begin(&sound_telephone);
	telephone_begin(&speech_telephone);

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

// every packet of sound that has come from the radio program is made the
// telephone's, telephone.c, and sent on with the stream's id before it and
// its tag after. It keeps the number the radio program gave it, so that the
// service still sees which did not come
static void sound_pass_on(void) {
	uint8_t whole[RTP_PACKET_BYTES + 1];
	uint8_t packet[SOUND_PACKET_BYTES];
	uint8_t *rtp = packet + STREAM_ID_BYTES;
	uint8_t hash[SHA256_BYTES];
	ssize_t got;

	if (sound_in == NOT_OPEN)
		return;
	memcpy(packet, stream_id, STREAM_ID_BYTES);
	got = recv(sound_in, whole, sizeof(whole), 0);
	while (got > 0) {
		if (got == RTP_PACKET_BYTES) {
			rtp[0] = RTP_VERSION_2;
			rtp[1] = RTP_PAYLOAD_PCMU;
			rtp[2] = whole[2];
			rtp[3] = whole[3];
			sound_samples += TELEPHONE_FRAME_SAMPLES;
			rtp[4] = (uint8_t)(sound_samples >> 24);
			rtp[5] = (uint8_t)(sound_samples >> 16);
			rtp[6] = (uint8_t)(sound_samples >> 8);
			rtp[7] = (uint8_t)sound_samples;
			memcpy(rtp + 8, whole + 8, RTP_SOURCE_BYTES);
			telephone_from_whole(&sound_telephone, whole + RTP_HEADER_BYTES, rtp + RTP_HEADER_BYTES);
			hmac_sha256((const uint8_t *)stream_key, strlen(stream_key), packet,
				STREAM_ID_BYTES + NET_RTP_BYTES, hash);
			memcpy(packet + STREAM_ID_BYTES + NET_RTP_BYTES, hash, TAG_BYTES);
			send(sound_out, packet, sizeof(packet), MSG_NOSIGNAL);
		}
		got = recv(sound_in, whole, sizeof(whole), 0);
	}
}

// every packet of speech the service has sent is handed to the radio
// program, if its tag is right and it is ahead of the last
static void speech_pass_on(void) {
	uint8_t packet[SOUND_PACKET_BYTES + 1];
	uint8_t whole[RTP_MICROPHONE_LABEL_BYTES + RTP_PACKET_BYTES];
	uint8_t *labelled;
	uint8_t hash[SHA256_BYTES];
	uint32_t stamp;
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
			STREAM_ID_BYTES + NET_RTP_BYTES, hash);
		// compared without stopping at the first byte that differs, so that
		// how long it takes says nothing of how nearly right a guess was
		differing = 0;
		for (i = 0; i < TAG_BYTES; i++)
			differing |= (uint8_t)(hash[i] ^ packet[STREAM_ID_BYTES + NET_RTP_BYTES + i]);
		if (differing)
			continue;
		sequence = (uint16_t)(packet[STREAM_ID_BYTES + 2] << 8 | packet[STREAM_ID_BYTES + 3]);
		if (is_speech_begun && (uint16_t)(sequence - speech_sequence - 1) >= SPEECH_AHEAD_MOST)
			continue;
		is_speech_begun = 1;
		speech_sequence = sequence;
		// the radio program is handed the whole sound under its label: what
		// kind it is, speech to transmit, and whose, the radio's own owner's
		memset(whole, 0, RTP_MICROPHONE_LABEL_BYTES);
		whole[0] = RTP_MICROPHONE_TO_TRANSMIT;
		memcpy(whole + RTP_MICROPHONE_KIND_BYTES, callsign,
			strnlen(callsign, RTP_MICROPHONE_CALLSIGN_BYTES));
		labelled = whole + RTP_MICROPHONE_LABEL_BYTES;
		labelled[0] = RTP_VERSION_2;
		labelled[1] = RTP_AUDIO_PAYLOAD_TYPE;
		labelled[2] = packet[STREAM_ID_BYTES + 2];
		labelled[3] = packet[STREAM_ID_BYTES + 3];
		stamp = (uint32_t)sequence * WHOLE_FRAME_SAMPLES;
		labelled[4] = (uint8_t)(stamp >> 24);
		labelled[5] = (uint8_t)(stamp >> 16);
		labelled[6] = (uint8_t)(stamp >> 8);
		labelled[7] = (uint8_t)stamp;
		memcpy(labelled + 8, packet + STREAM_ID_BYTES + 8, RTP_SOURCE_BYTES);
		telephone_to_whole(&speech_telephone, packet + STREAM_ID_BYTES + RTP_HEADER_BYTES,
			labelled + RTP_HEADER_BYTES);
		sendto(speech_out, whole, sizeof(whole), MSG_NOSIGNAL,
			(struct sockaddr *)&speech_port, sizeof(speech_port));
	}
}

/* VoIP: my.sbitx.net's repeater; accounts/voip.py has the packet */

static long channel_of(long hz) {
	return (hz + HZ_A_CHANNEL / 2) / HZ_A_CHANNEL;
}

static void put_be32(uint8_t *at, uint32_t value) {
	at[0] = (uint8_t)(value >> 24);
	at[1] = (uint8_t)(value >> 16);
	at[2] = (uint8_t)(value >> 8);
	at[3] = (uint8_t)value;
}

static uint32_t be32(const uint8_t *at) {
	return (uint32_t)at[0] << 24 | (uint32_t)at[1] << 16 | (uint32_t)at[2] << 8 | at[3];
}

static void voip_off(void) {
	close_fd(&voip_fd);
	close_fd(&voip_radio_fd);
	voip_room[0] = 0;
	voip_busy[0] = 0;
}

// the service's stream: its id, its two keys, a socket to the service and
// one on the radio, where the radio program sends the owner's speech
static void voip_on(const char *id_hex, const char *nonce) {
	struct addrinfo hints;
	struct addrinfo *found;
	struct sockaddr_in here;
	char text[TEXT_ROOM];
	unsigned int byte;
	int i;

	voip_off();
	for (i = 0; i < STREAM_ID_BYTES; i++) {
		if (sscanf(id_hex + 2 * i, "%2x", &byte) != 1)
			return;
		voip_id[i] = (uint8_t)byte;
	}
	snprintf(text, sizeof(text), "zbitx-voip-up-1 %.64s", nonce);
	hmac_sha256_hex(stretched, text, voip_up_key);
	snprintf(text, sizeof(text), "zbitx-voip-down-1 %.64s", nonce);
	hmac_sha256_hex(stretched, text, voip_down_key);
	is_voip_taken_begun = 0;
	voip_tuned_ms = 0;
	voip_channel_told = -1;
	telephone_begin(&voip_spoken_telephone);
	telephone_begin(&voip_heard_telephone);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(server, port, &hints, &found) != 0)
		return;
	voip_fd = socket(found->ai_family, found->ai_socktype | SOCK_NONBLOCK, found->ai_protocol);
	if (voip_fd < 0 || connect(voip_fd, found->ai_addr, found->ai_addrlen) < 0) {
		freeaddrinfo(found);
		voip_off();
		return;
	}
	freeaddrinfo(found);
	voip_radio_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	memset(&here, 0, sizeof(here));
	here.sin_family = AF_INET;
	here.sin_port = htons((uint16_t)atoi(voip_port));
	inet_pton(AF_INET, LOOPBACK, &here.sin_addr);
	if (voip_radio_fd >= 0 && bind(voip_radio_fd, (struct sockaddr *)&here, sizeof(here)) < 0)
		close_fd(&voip_radio_fd);
	printf("remote: on VoIP\n");
	fflush(stdout);
}

// a packet to the service: where the radio is tuned, or the owner's speech.
// The callsign is left empty: the service writes it, from the account
static void voip_send(int kind, const uint8_t *sound) {
	uint8_t packet[VOIP_PACKET_BYTES];
	uint8_t *head = packet + STREAM_ID_BYTES;
	uint8_t hash[SHA256_BYTES];

	memset(packet, 0, sizeof(packet));
	memcpy(packet, voip_id, STREAM_ID_BYTES);
	head[0] = (uint8_t)kind;
	head[1] = VOIP_CODEC_MULAW;
	put_be32(head + 2 + VOIP_CALLSIGN_BYTES, (uint32_t)atol(tuned_hz));
	voip_sent++;
	put_be32(head + 2 + VOIP_CALLSIGN_BYTES + 4, voip_sent);
	if (sound != NULL)
		memcpy(head + VOIP_HEAD_BYTES, sound, TELEPHONE_FRAME_SAMPLES);
	hmac_sha256((const uint8_t *)voip_up_key, strlen(voip_up_key), packet,
		VOIP_PACKET_BYTES - TAG_BYTES, hash);
	memcpy(packet + VOIP_PACKET_BYTES - TAG_BYTES, hash, TAG_BYTES);
	send(voip_fd, packet, sizeof(packet), MSG_NOSIGNAL);
}

// another's speech from the service is handed to the radio program, to be
// heard, if its tag, its number and its frequency are right
static void voip_hear(const uint8_t *packet, ssize_t got) {
	uint8_t whole[RTP_MICROPHONE_LABEL_BYTES + RTP_PACKET_BYTES];
	uint8_t *labelled = whole + RTP_MICROPHONE_LABEL_BYTES;
	const uint8_t *head = packet + STREAM_ID_BYTES;
	struct sockaddr_in microphone;
	uint8_t hash[SHA256_BYTES];
	uint8_t differing = 0;
	uint32_t number;
	uint32_t stamp;
	int i;

	if (got != VOIP_PACKET_BYTES || memcmp(packet, voip_id, STREAM_ID_BYTES) != 0
			|| voip_radio_fd == NOT_OPEN)
		return;
	hmac_sha256((const uint8_t *)voip_down_key, strlen(voip_down_key), packet,
		VOIP_PACKET_BYTES - TAG_BYTES, hash);
	// compared without stopping at the first byte that differs
	for (i = 0; i < TAG_BYTES; i++)
		differing |= (uint8_t)(hash[i] ^ packet[VOIP_PACKET_BYTES - TAG_BYTES + i]);
	if (differing || head[0] != VOIP_KIND_SPEECH || head[1] != VOIP_CODEC_MULAW)
		return;
	number = be32(head + 2 + VOIP_CALLSIGN_BYTES + 4);
	if (is_voip_taken_begun && (uint32_t)(number - voip_taken - 1) >= VOIP_AHEAD_MOST)
		return;
	is_voip_taken_begun = 1;
	voip_taken = number;
	if (channel_of((long)be32(head + 2 + VOIP_CALLSIGN_BYTES)) != channel_of(atol(tuned_hz)))
		return;
	memset(whole, 0, RTP_MICROPHONE_LABEL_BYTES);
	whole[0] = RTP_MICROPHONE_VOIP;
	memcpy(whole + RTP_MICROPHONE_KIND_BYTES, head + 2, VOIP_CALLSIGN_BYTES);
	voip_heard_sequence++;
	stamp = (uint32_t)voip_heard_sequence * WHOLE_FRAME_SAMPLES;
	labelled[0] = RTP_VERSION_2;
	labelled[1] = RTP_AUDIO_PAYLOAD_TYPE;
	labelled[2] = (uint8_t)(voip_heard_sequence >> 8);
	labelled[3] = (uint8_t)voip_heard_sequence;
	put_be32(labelled + 4, stamp);
	put_be32(labelled + 8, number);
	telephone_to_whole(&voip_heard_telephone, head + VOIP_HEAD_BYTES,
		labelled + RTP_HEADER_BYTES);
	memset(&microphone, 0, sizeof(microphone));
	microphone.sin_family = AF_INET;
	microphone.sin_port = htons((uint16_t)atoi(microphone_port));
	inet_pton(AF_INET, LOOPBACK, &microphone.sin_addr);
	sendto(voip_radio_fd, whole, sizeof(whole), MSG_NOSIGNAL, (struct sockaddr *)&microphone,
		sizeof(microphone));
}

static void voip_poll(long now) {
	uint8_t whole[RTP_PACKET_BYTES + 1];
	uint8_t packet[VOIP_PACKET_BYTES + 1];
	uint8_t sound[TELEPHONE_FRAME_SAMPLES];
	long channel = channel_of(atol(tuned_hz));
	ssize_t got;

	if (voip_fd == NOT_OPEN)
		return;
	// where the radio is tuned: now and then, and soon after it moves
	if (tuned_hz[0] != 0 && (now - voip_tuned_ms >= VOIP_TUNED_EVERY_MS
			|| (channel != voip_channel_told && now - voip_tuned_ms >= VOIP_TUNED_SOON_MS))) {
		voip_send(VOIP_KIND_TUNED, NULL);
		voip_tuned_ms = now;
		voip_channel_told = channel;
	}
	while (voip_radio_fd != NOT_OPEN && (got = recv(voip_radio_fd, whole, sizeof(whole), 0)) > 0) {
		if (got != RTP_PACKET_BYTES || tuned_hz[0] == 0)
			continue;
		telephone_from_whole(&voip_spoken_telephone, whole + RTP_HEADER_BYTES, sound);
		voip_send(VOIP_KIND_SPEECH, sound);
	}
	while ((got = recv(voip_fd, packet, sizeof(packet), 0)) > 0)
		voip_hear(packet, got);
}

// VOIP_STATUS: what there is to say of VoIP
static void voip_say(long now) {
	char said[STATUS_ROOM];

	if (!is_voip_wanted)
		snprintf(said, sizeof(said), "OFFLINE");
	else if (state != ONLINE || voip_fd == NOT_OPEN)
		snprintf(said, sizeof(said), "CONNECTING");
	else if (voip_radio_fd == NOT_OPEN)
		snprintf(said, sizeof(said), "PORT %.5s IS IN USE", voip_port);
	else if (voip_busy[0] != 0 && now - voip_busy_ms < VOIP_BUSY_SHOWN_MS)
		snprintf(said, sizeof(said), "BUSY %.26s", voip_busy);
	else if (voip_room[0] != 0)
		snprintf(said, sizeof(said), "%s", voip_room);
	else
		snprintf(said, sizeof(said), "ONLINE");
	status_say(VOIP_STATUS_CONTROL, voip_status, said);
}

// what the service says of VoIP on the connection that is kept
static void voip_heard_of(const char *line, long now) {
	char token[LINE_ROOM];
	char nonce[LINE_ROOM];
	long channel;
	int count;
	int i;

	if (sscanf(line, "VOIP STREAM %16s %64s", token, nonce) == 2 && is_voip_wanted)
		voip_on(token, nonce);
	if (sscanf(line, "VOIP ROOM %ld %d", &channel, &count) == 2)
		snprintf(voip_room, sizeof(voip_room), "%ld KHZ, %d HERE", channel, count);
	if (!strncmp(line, "VOIP BUSY ", strlen("VOIP BUSY "))) {
		snprintf(voip_busy, sizeof(voip_busy), "%.26s", line + strlen("VOIP BUSY "));
		for (i = 0; voip_busy[i] != 0; i++) {
			if (voip_busy[i] >= 'a' && voip_busy[i] <= 'z')
				voip_busy[i] = (char)(voip_busy[i] - 'a' + 'A');
		}
		voip_busy_ms = now;
	}
}

/* a new cookie, asked for at the radio */

static void cookie_renew(void) {
	char said[STATUS_ROOM];

	if (make_a_cookie() < 0) {
		say_status("COOKIE NOT SAVED");
		return;
	}
	stretched_for[0] = 0;
	// the service knows the old one: it is let go, and the radio shows the
	// new one until the website has it
	if (state != OFF) {
		close_fd(&line_fd);
		sound_off();
		voip_off();
		sessions_close();
		state = OFF;
	}
	show_the_cookie(said, sizeof(said));
	say_status(said);
	if (radio_fd != NOT_OPEN)
		send_text(radio_fd, "NEW_COOKIE -\n");
	printf("remote: a new cookie is made\n");
	fflush(stdout);
}

/* the connection kept to my.sbitx.net */

static void line_close(long now) {
	close_fd(&line_fd);
	sound_off();
	voip_off();
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
	if (result == AUTH_OK && (send_text(line_fd, "radio\n") < 0
			|| read_line_waiting(line_fd, line, sizeof(line)) < 0 || strcmp(line, "OK"))) {
		no_answer("no OK to radio");
		result = AUTH_NO_ANSWER;
	}
	if (result != AUTH_OK) {
		// the status has little room; the reason is in the program's output
		printf("remote: no answer from %s port %s: %s\n", server, port, failed_because);
		fflush(stdout);
		line_close(now);
		say_status("NO ANSWER FROM SERVER");
		return;
	}
	line_lines.used = 0;
	next_ping_ms = now + PING_EVERY_MS;
	last_heard_ms = now;
	retry_after_ms = RETRY_FIRST_MS;
	state = ONLINE;
	// a new connection is a new radio to the service, which knows nothing
	// of the grid or the switches until it is told them again
	is_grid_told = 0;
	allowed_told[0] = 0;
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
		if (sscanf(line, "SESSION %64s", token) == 1 && is_remote_allowed)
			session_open(token);
		if (sscanf(line, "AUDIO ON %16s %64s", token, nonce) == 2)
			sound_on(token, nonce);
		if (!strcmp(line, "AUDIO OFF"))
			sound_off();
		voip_heard_of(line, now);
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
	// what the owner allows, once the connection is made and again whenever
	// either switch is changed
	snprintf(token, sizeof(token), "ALLOWS%s%s%s", is_remote_allowed ? " remote" : "",
		is_streaming_allowed ? " streaming" : "", is_voip_wanted ? " voip" : "");
	if (!is_voip_wanted)
		voip_off();
	if (got == 0 && strcmp(token, allowed_told)) {
		snprintf(line, sizeof(line), "%.60s\n", token);
		if (send_text(line_fd, line) < 0)
			got = -1;
		snprintf(allowed_told, sizeof(allowed_told), "%.60s", token);
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
	voip_off();
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
		voip_off();
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
	struct pollfd polls[7 + CAT_CLIENTS_MAX + 2 * SESSIONS_MAX];
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
	if (voip_fd != NOT_OPEN) {
		polls[count].fd = voip_fd;
		polls[count].events = POLLIN;
		polls[count++].revents = 0;
	}
	if (voip_radio_fd != NOT_OPEN) {
		polls[count].fd = voip_radio_fd;
		polls[count].events = POLLIN;
		polls[count++].revents = 0;
	}
	// what a program asks through CAT is not kept waiting for the loop
	if (cat_listen_fd != NOT_OPEN) {
		polls[count].fd = cat_listen_fd;
		polls[count].events = POLLIN;
		polls[count++].revents = 0;
	}
	for (i = 0; i < CAT_CLIENTS_MAX; i++) {
		if (cat_clients[i].fd == NOT_OPEN)
			continue;
		polls[count].fd = cat_clients[i].fd;
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
	voip_poll(now_ms());
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
		else if (!strcmp(argv[i], "--bluetooth-script"))
			bluetooth.script = argv[i + 1];
		else if (!strcmp(argv[i], "--sound-card-script"))
			sound_card.script = argv[i + 1];
		else if (!strcmp(argv[i], "--cat-port"))
			cat_port = argv[i + 1];
		else if (!strcmp(argv[i], "--voip-port"))
			voip_port = argv[i + 1];
		else
			i = argc;
	}
	if (i != argc) {
		printf("zbitx_remote [--server host] [--port n] [--radio-port n] [--key file]"
			" [--microphone-port n] [--bluetooth-script file] [--sound-card-script file]"
			" [--cat-port n] [--voip-port n]\n");
		return EXIT_FAILURE;
	}
	signal(SIGPIPE, SIG_IGN);
	telephone_open();
	for (i = 0; i < SESSIONS_MAX; i++) {
		sessions[i].service_fd = NOT_OPEN;
		sessions[i].radio_fd = NOT_OPEN;
	}
	for (i = 0; i < CAT_CLIENTS_MAX; i++)
		cat_clients[i].fd = NOT_OPEN;
	snprintf(status_said, sizeof(status_said), "OFF");
	printf("remote: for %s port %s, the radio program on port %s\n", server, port, radio_port);
	fflush(stdout);
	while (1) {
		radio_poll(now_ms());
		helper_poll(&bluetooth, now_ms());
		helper_poll(&sound_card, now_ms());
		cat_poll(now_ms());
		follow_the_owner(now_ms());
		voip_say(now_ms());
		wait_and_pass_on();
	}
	return EXIT_SUCCESS;
}
