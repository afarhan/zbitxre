// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_wifi.c — the WiFi access points the radio can hear, listed for the
// front panel.
//
// The author's plan for the panel's WiFi setting: the panel shows the
// access points, the operator chooses one and types its key, and the radio
// joins it.
//
// WHO SCANS
//
// The radio, the Pi, and not the panel's Pico. The two are an inch apart
// and hear the same access points, so either could; the Pi does because it
// costs the Pico no memory, of which it has little to spare, and leaves the
// Pico's own WiFi chip, a transmitter on 2.4 GHz against the receiver,
// switched off.
//
// HOW
//
// WIFI is a control, an action, with one thing it does: SCAN. The panel
// sets it when its WiFi screen is opened; "WIFI SCAN" from the terminal
// does the same. The scan is asked of wpa_supplicant through wpa_cli, which
// the user the radio runs as may use, being in the group netdev. A scan
// takes a few seconds, and the radio does not wait for it: the results are
// read SCAN_TAKES_MS later, from panel_wifi_poll(), which the radio's loop
// calls each time round.
//
// Of the results, an access point that is heard more than once, on two
// bands or from two boxes, is listed once, at its strongest; one with no
// name is left out; and the LINK_WIFI_POINTS_MAX strongest are kept, the
// strongest first. The author asked for 16.
//
// The list goes to the panel in parts, each when the one before has been
// taken; front_panel/link.c says what is in a part. It is printed on the
// terminal too.
//
// JOINING
//
// The panel sends back the name of the access point chosen and its key.
// The radio makes it the access point in use, through wpa_cli again, which
// needs no more rights than the scan did:
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
// wpa_cli is run directly, each word of the command given as it is and
// not through a shell, since a name or a key may have any character in it.
// The key is never printed.
//
// The panel's status bar is told what is being done, and ten seconds later
// whether the radio is on that access point.
//
// WHICH IT IS ON
//
// WIFI_SSID is the name of the access point the radio is on, or nothing
// when it is on none, looked for every five seconds with wpa_cli's status,
// which the check after a join shares. The panel's WiFi row in SET shows
// it, the author's asking of 18 October 2026. It is a report: never kept,
// and set at the radio only, never by a client.
//
// Changing the access point changes the radio's address on the network,
// and whatever was connected to it over WiFi loses it.
//
// For the thread that owns the controls.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "control.h"
#include "link.h"
#include "panel.h"
#include "panel_wifi.h"

#define SCAN_COMMAND "/sbin/wpa_cli -i wlan0 scan > /dev/null 2>&1"
#define RESULTS_COMMAND "/sbin/wpa_cli -i wlan0 scan_results 2> /dev/null"
#define SCAN_TAKES_MS 4000
#define RESULTS_MAX 16384
#define RESULT_FIELDS 5
#define FIELDS_BETWEEN '\t'
#define NOT_WAITING (-1L)
#define NOTHING_TO_SEND (-1)

#define WPA_CLI "/sbin/wpa_cli"
#define WPA_INTERFACE "wlan0"
#define WORDS_MAX 10
#define ANSWER_MAX 4096
#define ID_MAX 12
#define HEX_PER_BYTE 2
#define QUOTES 2
#define JOINED_CHECK_AFTER_MS 10000
#define SSID_EVERY_MS 5000
#define SSID_LINE "\nssid="
#define JOINED_STATE "wpa_state=COMPLETED"
#define NO_NETWORK (-1)
#define NETWORK_FIELDS 2

static struct link_wifi_point points[LINK_WIFI_POINTS_MAX];
static int point_count;
static uint8_t part[LINK_COUNTED_MAX];
static int next_to_send = NOTHING_TO_SEND;
static int is_scan_wanted;
static long results_at_ms = NOT_WAITING;

// an access point takes its place in the list, which is kept strongest
// first: one already there under the same name keeps the stronger of the
// two, and with the list full the weakest gives way
static void note_a_point(const char *name, int signal_dbm, int is_secured) {
	struct link_wifi_point point;
	int at;
	int i;

	for (i = 0; i < point_count; i++) {
		if (strcmp(points[i].name, name))
			continue;
		if (signal_dbm <= points[i].signal_dbm)
			return;
		memmove(points + i, points + i + 1, (point_count - i - 1) * sizeof(points[0]));
		point_count--;
		break;
	}
	at = point_count;
	while (at > 0 && points[at - 1].signal_dbm < signal_dbm)
		at--;
	if (at == LINK_WIFI_POINTS_MAX)
		return;
	if (point_count == LINK_WIFI_POINTS_MAX)
		point_count--;
	memset(&point, 0, sizeof(point));
	point.signal_dbm = (int8_t)signal_dbm;
	point.is_secured = (uint8_t)is_secured;
	snprintf(point.name, sizeof(point.name), "%s", name);
	memmove(points + at + 1, points + at, (point_count - at) * sizeof(points[0]));
	points[at] = point;
	point_count++;
}

// one line of wpa_cli's results: the address, the frequency, the signal,
// the flags and the name, with a tab between them
static void take_a_result(char *line) {
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
	note_a_point(field[4], atoi(field[2]),
		strstr(field[3], "WPA") != NULL || strstr(field[3], "WEP") != NULL);
}

// makes the list from wpa_cli's results, and sets it to be sent to the
// panel; says how many access points it has
int panel_wifi_take_results(const char *results) {
	static char line[LINK_WIFI_NAME_MAX + 200];
	const char *end;
	size_t length;

	point_count = 0;
	while (*results != 0) {
		end = strchr(results, '\n');
		if (end == NULL)
			end = results + strlen(results);
		length = (size_t)(end - results);
		if (length < sizeof(line)) {
			memcpy(line, results, length);
			line[length] = 0;
			take_a_result(line);
		}
		results = end;
		if (*results == '\n')
			results++;
	}
	next_to_send = 0;
	return point_count;
}

static void read_the_results(void) {
	static char results[RESULTS_MAX];
	FILE *from = popen(RESULTS_COMMAND, "r");
	size_t count = 0;
	int i;

	if (from != NULL) {
		count = fread(results, 1, sizeof(results) - 1, from);
		pclose(from);
	}
	results[count] = 0;
	panel_wifi_take_results(results);
	printf("wifi: %d access points\n", point_count);
	for (i = 0; i < point_count; i++) {
		printf("wifi: %4d dBm  %s", points[i].signal_dbm, points[i].name);
		if (!points[i].is_secured)
			printf("  (open)");
		printf("\n");
	}
	fflush(stdout);
}

// the next part of the list goes when the panel has taken the one before
static void send_a_part(void) {
	int used = LINK_WIFI_PART_HEADER;
	int put;

	if (next_to_send == NOTHING_TO_SEND || panel_is_waiting(LINK_WIFI_LIST))
		return;
	part[0] = (uint8_t)next_to_send;
	while (next_to_send < point_count) {
		put = link_put_wifi_point(part + used, (int)sizeof(part) - used,
			&points[next_to_send]);
		if (put == 0)
			break;
		used += put;
		next_to_send++;
	}
	part[1] = next_to_send == point_count;
	if (part[1])
		next_to_send = NOTHING_TO_SEND;
	panel_set_kept_bytes(LINK_WIFI_LIST, part, used);
}

/* joining */

// runs wpa_cli with the words given, each as it is, and keeps what it
// prints
static int ask_wpa_cli(const char *const *words, char *answer, int room) {
	const char *argv[WORDS_MAX + 4];
	int from_child[2];
	ssize_t got;
	int used = 0;
	int count = 0;
	int status;
	pid_t child;
	int i;

	argv[count++] = WPA_CLI;
	argv[count++] = "-i";
	argv[count++] = WPA_INTERFACE;
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
		execv(WPA_CLI, (char *const *)argv);
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

static panel_wifi_ask_fn ask = ask_wpa_cli;
static char joining[LINK_WIFI_NAME_MAX + 1];
static long joined_check_at_ms = NOT_WAITING;

void panel_wifi_set_asking(panel_wifi_ask_fn asking) {
	ask = asking;
}

// asks, with up to four words, and says whether the answer was OK
static int is_ok(const char *a, const char *b, const char *c, const char *d) {
	const char *words[] = { a, b, c, d, NULL };
	char answer[ANSWER_MAX];

	if (ask(words, answer, sizeof(answer)) < 0)
		return 0;
	return strncmp(answer, "OK", 2) == 0;
}

// the number wpa_supplicant knows a network of this name by, or NO_NETWORK
static int network_of(const char *name) {
	const char *words[] = { "list_networks", NULL };
	static char answer[ANSWER_MAX];
	char *line;
	char *next;
	char *known;
	char *end;

	if (ask(words, answer, sizeof(answer)) < 0)
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
	char answer[ANSWER_MAX];
	char id[ID_MAX];
	size_t i;

	if (ask(words, answer, sizeof(answer)) < 0 || answer[0] < '0' || answer[0] > '9')
		return NO_NETWORK;
	snprintf(id, sizeof(id), "%d", atoi(answer));
	for (i = 0; i < strlen(name); i++)
		snprintf(hex + i * HEX_PER_BYTE, HEX_PER_BYTE + 1, "%02x", (unsigned char)name[i]);
	if (!is_ok("set_network", id, "ssid", hex))
		return NO_NETWORK;
	return atoi(id);
}

// makes the access point named the one in use, with its key; says what to
// tell the operator if it could not
static const char *join(const char *name, const char *key) {
	char quoted[LINK_WIFI_KEY_MAX + QUOTES + 1];
	char id[ID_MAX];
	size_t length = strlen(key);
	int network;

	if (name[0] == 0 || strlen(name) > LINK_WIFI_NAME_MAX)
		return "wifi: no access point was named";
	if (length != 0 && (length < LINK_WIFI_KEY_MIN || length > LINK_WIFI_KEY_MAX))
		return "wifi: a key is 8 to 63 characters";
	network = network_of(name);
	if (network == NO_NETWORK)
		network = add_a_network(name);
	if (network == NO_NETWORK)
		return "wifi: the access point could not be added";
	snprintf(id, sizeof(id), "%d", network);
	snprintf(quoted, sizeof(quoted), "\"%s\"", key);
	if (length == 0 && !is_ok("set_network", id, "key_mgmt", "NONE"))
		return "wifi: the access point could not be set open";
	if (length != 0 && !is_ok("set_network", id, "psk", quoted))
		return "wifi: the key was not taken";
	if (!is_ok("enable_network", id, NULL, NULL) || !is_ok("save_config", NULL, NULL, NULL)
			|| !is_ok("select_network", id, NULL, NULL))
		return "wifi: the access point could not be selected";
	return NULL;
}

static struct control wifi_ssid = {
	.group = "SYSTEM", .name = "WIFI_SSID", .kind = CONTROL_TEXT,
	.is_set_at_the_radio = 1,
};
static long ssid_check_at_ms = NOT_WAITING;

// WIFI_SSID from wpa_cli's status: the ssid= line's name, when the radio is
// on the access point, or nothing
static void take_the_ssid(const char *answer) {
	char why[CONTROL_WHY_MAX];
	char ssid[LINK_WIFI_NAME_MAX + 1] = "";
	const char *at = strstr(answer, SSID_LINE);
	size_t length;

	if (at != NULL && strstr(answer, JOINED_STATE) != NULL) {
		at += strlen(SSID_LINE);
		length = strcspn(at, "\n");
		if (length > LINK_WIFI_NAME_MAX)
			length = LINK_WIFI_NAME_MAX;
		memcpy(ssid, at, length);
		ssid[length] = 0;
	}
	if (strcmp(wifi_ssid.value, ssid))
		control_set(wifi_ssid.name, ssid, ORIGIN_NONE, why, sizeof(why));
}

// whether the radio is on the access point it was asked to join, from an
// answer of wpa_cli's status
static void say_whether_joined(const char *answer) {
	char wanted[LINK_WIFI_NAME_MAX + 8];
	char said[LINK_SAY_MAX + 1];

	snprintf(wanted, sizeof(wanted), "\nssid=%s\n", joining);
	if (strstr(answer, wanted) != NULL && strstr(answer, JOINED_STATE) != NULL)
		snprintf(said, sizeof(said), "wifi: joined %s", joining);
	else
		snprintf(said, sizeof(said), "wifi: not on %s", joining);
	printf("%s\n", said);
	fflush(stdout);
	panel_say(said);
}

// the access point the operator chose on the panel, and its key, come as a
// text in parts; when it is whole the radio joins it
void panel_wifi_took_bytes(int control, const uint8_t *bytes, int count, long now_ms) {
	static char gathered[LINK_TEXT_MAX + 1];
	char said[LINK_SAY_MAX + 1];
	const char *trouble;
	int number = 0;
	char *key;

	if (control != LINK_TEXT_PART)
		return;
	if (link_get_text_part(bytes, count, &number, gathered, sizeof(gathered)) != 1
			|| number != LINK_WIFI_CHOSEN)
		return;
	key = strchr(gathered, LINK_WIFI_BETWEEN);
	if (key == NULL)
		return;
	*key++ = 0;
	printf("wifi: joining %s\n", gathered);
	trouble = join(gathered, key);
	memset(key, 0, strlen(key));
	if (trouble != NULL) {
		printf("%s\n", trouble);
		panel_say(trouble);
	}
	else {
		snprintf(joining, sizeof(joining), "%.*s", LINK_WIFI_NAME_MAX, gathered);
		snprintf(said, sizeof(said), "wifi: joining %s", joining);
		panel_say(said);
		joined_check_at_ms = now_ms + JOINED_CHECK_AFTER_MS;
	}
	fflush(stdout);
}

// wpa_cli's status, every SSID_EVERY_MS for WIFI_SSID, and once after a
// join, to say whether it was joined: one asking does for both when they
// fall together
static void poll_the_status(long now_ms) {
	const char *words[] = { "status", NULL };
	char answer[ANSWER_MAX];
	int is_join_due = joined_check_at_ms != NOT_WAITING && now_ms >= joined_check_at_ms;

	if (ssid_check_at_ms == NOT_WAITING)
		ssid_check_at_ms = now_ms + SSID_EVERY_MS;
	if (now_ms < ssid_check_at_ms && !is_join_due)
		return;
	if (now_ms >= ssid_check_at_ms)
		ssid_check_at_ms = now_ms + SSID_EVERY_MS;
	if (ask(words, answer, sizeof(answer)) < 0)
		answer[0] = 0;
	take_the_ssid(answer);
	if (is_join_due) {
		joined_check_at_ms = NOT_WAITING;
		say_whether_joined(answer);
	}
}

void panel_wifi_poll(long now_ms) {
	if (is_scan_wanted) {
		is_scan_wanted = 0;
		if (system(SCAN_COMMAND) != 0)
			printf("wifi: the scan could not be asked for\n");
		results_at_ms = now_ms + SCAN_TAKES_MS;
	}
	if (results_at_ms != NOT_WAITING && now_ms >= results_at_ms) {
		results_at_ms = NOT_WAITING;
		read_the_results();
	}
	poll_the_status(now_ms);
	send_a_part();
}

static int apply_wifi(struct control *c, const char *value, char *why, int room) {
	(void)c;
	(void)value;
	(void)why;
	(void)room;
	is_scan_wanted = 1;
	return 0;
}

static struct control wifi = {
	.group = "SYSTEM", .name = "WIFI", .kind = CONTROL_ACTION,
	.choices = "SCAN", .apply = apply_wifi,
};

int panel_wifi_open(void) {
	if (control_register(&wifi_ssid) < 0)
		return -1;
	return control_register(&wifi);
}
