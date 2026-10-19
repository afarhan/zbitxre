// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// wifi.c — the radio's WiFi: the access points it hears, the one it is on,
// and joining one.
//
// Two set the WiFi: the front panel, through the radio program,
// panel_wifi.c, and the local GTK client on an sbitx's own screen, which
// the author asked for on 18 October 2026 so that a touch screen sbitx can
// be put on a network without a keyboard. Both run on the radio's Pi, and
// both use this. The remote GTK client and the web page have no WiFi at
// all: a key typed wrong from afar would leave the radio out of reach.
//
// WHAT IS HEARD
//
// The Pi is asked through wpa_cli: "scan", and a few seconds later
// "scan_results". Of the results, an access point that is heard more than
// once, on two bands or from two boxes, is listed once, at its strongest;
// one with no name is left out; and the strongest are kept, the strongest
// first, as many as there is room for. The author asked for 16.
//
// WHICH IT IS ON
//
// wpa_cli's "status": the ssid= line, when wpa_state= is COMPLETED.
//
// JOINING
//
// A Pi with NetworkManager, as Raspberry Pi OS has had since Bookworm, is
// joined through nmcli, "device wifi connect", which keeps the network
// among its own; wpa_cli reads the status and the results there as well,
// but a network added through it would be NetworkManager's to undo. The
// sbitx at .82 found it so on 18 October 2026. Without NetworkManager, as
// on the zbitx's Buster, it is joined through wpa_cli:
//
//   If wpa_supplicant already knows a network of that name, its key is
//   changed; if not, a network is added. The author's rule: it is set as
//   the new access point, or its key is brought up to date.
//   The name is given in hex, so that no character in it can be taken for
//   anything else. A key is given in quotes, as a passphrase; an open
//   access point is given no key.
//   The network is enabled and the configuration saved, and then it is
//   selected. In that order: selecting a network disables the others, and
//   saved after it they would stay disabled for good.
//
// Either program is run directly, each word of the command given as it is
// and not through a shell, since a name or a key may have any character in
// it. The key is never printed. Changing the access point changes the
// radio's address on the network, and whatever was connected to it over
// WiFi loses it.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wifi.h"

#define WPA_CLI "/sbin/wpa_cli"
#define NMCLI "/usr/bin/nmcli"
#define WPA_INTERFACE "wlan0"
#define WORDS_MAX 12
#define RESULTS_MAX 16384
#define RESULT_FIELDS 5
#define FIELDS_BETWEEN '\t'
#define ID_MAX 12
#define HEX_PER_BYTE 2
#define QUOTES 2
#define NO_NETWORK (-1)
#define SSID_LINE "\nssid="
#define JOINED_STATE "wpa_state=COMPLETED"
#define NM_RUNNING "running"

// runs the program with the words given, each as it is, and keeps what it
// prints; wpa_cli is told the interface first
static int ask_a_program(const char *program, const char *const *words, char *answer,
		int room) {
	const char *argv[WORDS_MAX + 4];
	int from_child[2];
	ssize_t got;
	int used = 0;
	int count = 0;
	int status;
	pid_t child;
	int i;

	argv[count++] = program;
	if (!strcmp(program, WPA_CLI)) {
		argv[count++] = "-i";
		argv[count++] = WPA_INTERFACE;
	}
	for (i = 0; words[i] != NULL && i < WORDS_MAX; i++)
		argv[count++] = words[i];
	argv[count] = NULL;

	answer[0] = 0;
	if (pipe(from_child) < 0)
		return -1;
	child = fork();
	if (child < 0) {
		close(from_child[0]);
		close(from_child[1]);
		return -1;
	}
	if (child == 0) {
		dup2(from_child[1], STDOUT_FILENO);
		close(from_child[0]);
		close(from_child[1]);
		execv(program, (char *const *)argv);
		_exit(EXIT_FAILURE);
	}
	close(from_child[1]);
	got = read(from_child[0], answer, room - 1);
	while (got > 0 && used + got < room - 1) {
		used += (int)got;
		got = read(from_child[0], answer + used, room - 1 - used);
	}
	if (got > 0)
		used += (int)got;
	answer[used] = 0;
	close(from_child[0]);
	waitpid(child, &status, 0);
	return 0;
}

static wifi_ask_fn ask = ask_a_program;

void wifi_set_asking(wifi_ask_fn asking) {
	ask = asking;
}

/* what is heard */

int wifi_scan(void) {
	const char *words[] = { "scan", NULL };
	char answer[WIFI_ANSWER_MAX];

	return ask(WPA_CLI, words, answer, sizeof(answer));
}

// an access point takes its place in the list, which is kept strongest
// first: one already there under the same name keeps the stronger of the
// two, and with the list full the weakest gives way
static void note_a_point(struct link_wifi_point *points, int *count, int most,
		const char *name, int signal_dbm, int is_secured) {
	struct link_wifi_point point;
	int at;
	int i;

	for (i = 0; i < *count; i++) {
		if (strcmp(points[i].name, name))
			continue;
		if (signal_dbm <= points[i].signal_dbm)
			return;
		memmove(points + i, points + i + 1, (*count - i - 1) * sizeof(points[0]));
		(*count)--;
		break;
	}
	at = *count;
	while (at > 0 && points[at - 1].signal_dbm < signal_dbm)
		at--;
	if (at == most)
		return;
	if (*count == most)
		(*count)--;
	memset(&point, 0, sizeof(point));
	point.signal_dbm = (int8_t)signal_dbm;
	point.is_secured = (uint8_t)is_secured;
	snprintf(point.name, sizeof(point.name), "%s", name);
	memmove(points + at + 1, points + at, (*count - at) * sizeof(points[0]));
	points[at] = point;
	(*count)++;
}

// one line of wpa_cli's results: the address, the frequency, the signal,
// the flags and the name, with a tab between them
static void take_a_result(char *line, struct link_wifi_point *points, int *count, int most) {
	char *field[RESULT_FIELDS];
	int i;

	field[0] = line;
	for (i = 1; i < RESULT_FIELDS; i++) {
		field[i] = strchr(field[i - 1], FIELDS_BETWEEN);
		if (field[i] == NULL)
			return;
		*field[i]++ = 0;
	}
	if (field[4][0] == 0 || atoi(field[2]) >= 0)
		return;
	note_a_point(points, count, most, field[4], atoi(field[2]),
		strstr(field[3], "WPA") != NULL || strstr(field[3], "WEP") != NULL);
}

// the list made of wpa_cli's results; says how many access points it has
int wifi_take_results(const char *results, struct link_wifi_point *points, int most) {
	static char line[LINK_WIFI_NAME_MAX + 200];
	const char *end;
	size_t length;
	int count = 0;

	while (*results != 0) {
		end = strchr(results, '\n');
		if (end == NULL)
			end = results + strlen(results);
		length = (size_t)(end - results);
		if (length < sizeof(line)) {
			memcpy(line, results, length);
			line[length] = 0;
			take_a_result(line, points, &count, most);
		}
		results = end;
		if (*results == '\n')
			results++;
	}
	return count;
}

int wifi_read_results(struct link_wifi_point *points, int most) {
	const char *words[] = { "scan_results", NULL };
	static char results[RESULTS_MAX];

	if (ask(WPA_CLI, words, results, sizeof(results)) < 0)
		results[0] = 0;
	return wifi_take_results(results, points, most);
}

/* which it is on */

int wifi_status(char *answer, int room) {
	const char *words[] = { "status", NULL };

	return ask(WPA_CLI, words, answer, room);
}

// the access point named in a status, when the radio is on it, or nothing
void wifi_ssid_in(const char *status, char *ssid, int room) {
	const char *at = strstr(status, SSID_LINE);
	size_t length;

	ssid[0] = 0;
	if (at == NULL || strstr(status, JOINED_STATE) == NULL)
		return;
	at += strlen(SSID_LINE);
	length = strcspn(at, "\n");
	if (length > (size_t)room - 1)
		length = (size_t)room - 1;
	memcpy(ssid, at, length);
	ssid[length] = 0;
}

int wifi_is_on(const char *status, const char *name) {
	char ssid[LINK_WIFI_NAME_MAX + 1];

	wifi_ssid_in(status, ssid, sizeof(ssid));
	return ssid[0] != 0 && !strcmp(ssid, name);
}

/* joining */

// asks wpa_cli, with up to four words, and says whether the answer was OK
static int is_ok(const char *a, const char *b, const char *c, const char *d) {
	const char *words[] = { a, b, c, d, NULL };
	char answer[WIFI_ANSWER_MAX];

	if (ask(WPA_CLI, words, answer, sizeof(answer)) < 0)
		return 0;
	return strncmp(answer, "OK", 2) == 0;
}

// the number wpa_supplicant knows a network of this name by, or NO_NETWORK
static int network_of(const char *name) {
	const char *words[] = { "list_networks", NULL };
	static char answer[WIFI_ANSWER_MAX];
	char *line;
	char *next;
	char *known;
	char *end;

	if (ask(WPA_CLI, words, answer, sizeof(answer)) < 0)
		return NO_NETWORK;
	for (line = answer; line != NULL && *line != 0; line = next) {
		next = strchr(line, '\n');
		if (next != NULL)
			*next++ = 0;
		known = strchr(line, FIELDS_BETWEEN);
		if (known == NULL || line[0] < '0' || line[0] > '9')
			continue;
		*known++ = 0;
		end = strchr(known, FIELDS_BETWEEN);
		if (end != NULL)
			*end = 0;
		if (!strcmp(known, name))
			return atoi(line);
	}
	return NO_NETWORK;
}

static int add_a_network(const char *name) {
	const char *words[] = { "add_network", NULL };
	char hex[LINK_WIFI_NAME_MAX * HEX_PER_BYTE + 1];
	char answer[WIFI_ANSWER_MAX];
	char id[ID_MAX];
	size_t i;

	if (ask(WPA_CLI, words, answer, sizeof(answer)) < 0 || answer[0] < '0' || answer[0] > '9')
		return NO_NETWORK;
	snprintf(id, sizeof(id), "%d", atoi(answer));
	for (i = 0; i < strlen(name); i++)
		snprintf(hex + i * HEX_PER_BYTE, HEX_PER_BYTE + 1, "%02x", (unsigned char)name[i]);
	if (!is_ok("set_network", id, "ssid", hex))
		return NO_NETWORK;
	return atoi(id);
}

static const char *join_with_wpa_cli(const char *name, const char *key) {
	char quoted[LINK_WIFI_KEY_MAX + QUOTES + 1];
	char id[ID_MAX];
	int network;

	network = network_of(name);
	if (network == NO_NETWORK)
		network = add_a_network(name);
	if (network == NO_NETWORK)
		return "wifi: the access point could not be added";
	snprintf(id, sizeof(id), "%d", network);
	snprintf(quoted, sizeof(quoted), "\"%s\"", key);
	if (key[0] == 0 && !is_ok("set_network", id, "key_mgmt", "NONE"))
		return "wifi: the access point could not be set open";
	if (key[0] != 0 && !is_ok("set_network", id, "psk", quoted))
		return "wifi: the key was not taken";
	if (!is_ok("enable_network", id, NULL, NULL) || !is_ok("save_config", NULL, NULL, NULL)
			|| !is_ok("select_network", id, NULL, NULL))
		return "wifi: the access point could not be selected";
	return NULL;
}

static int is_network_manager_running(void) {
	const char *words[] = { "-t", "-f", "RUNNING", "general", NULL };
	char answer[WIFI_ANSWER_MAX];

	if (ask(NMCLI, words, answer, sizeof(answer)) < 0)
		return 0;
	return strncmp(answer, NM_RUNNING, strlen(NM_RUNNING)) == 0;
}

// nmcli says "successfully" when it has joined; a joining it waits for, a
// few seconds at most, since the key is tried there
static const char *join_with_nmcli(const char *name, const char *key) {
	const char *with_key[] = { "device", "wifi", "connect", name, "password", key, NULL };
	const char *open[] = { "device", "wifi", "connect", name, NULL };
	char answer[WIFI_ANSWER_MAX];
	const char *const *words = with_key;

	if (key[0] == 0)
		words = open;
	if (ask(NMCLI, words, answer, sizeof(answer)) < 0)
		return "wifi: nmcli could not be run";
	if (strstr(answer, "successfully") == NULL)
		return "wifi: the access point was not joined";
	return NULL;
}

// makes the access point named the one in use, with its key; says what to
// tell the operator if it could not
const char *wifi_join(const char *name, const char *key) {
	size_t length = strlen(key);

	if (name[0] == 0 || strlen(name) > LINK_WIFI_NAME_MAX)
		return "wifi: no access point was named";
	if (length != 0 && (length < LINK_WIFI_KEY_MIN || length > LINK_WIFI_KEY_MAX))
		return "wifi: a key is 8 to 63 characters";
	if (is_network_manager_running())
		return join_with_nmcli(name, key);
	return join_with_wpa_cli(name, key);
}
