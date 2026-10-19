// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_wifi.c — the WiFi access points the radio can hear, listed for the
// front panel, and the one the operator chooses there joined.
//
// The author's plan for the panel's WiFi setting: the panel shows the
// access points, the operator chooses one and types its key, and the radio
// joins it. wifi.c does the scanning, the reading of the results and the
// joining, which the local GTK client shares; this is the panel's part.
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
// does the same. A scan takes a few seconds, and the radio does not wait
// for it: the results are read SCAN_TAKES_MS later, from panel_wifi_poll(),
// which the radio's loop calls each time round. The list, the strongest
// first, goes to the panel in parts, each when the one before has been
// taken; front_panel/link.c says what is in a part. It is printed on the
// terminal too.
//
// The panel sends back the name of the access point chosen and its key,
// and the radio joins it, wifi.c says how. The panel's status bar is told
// what is being done, and ten seconds later whether the radio is on that
// access point.
//
// WHICH IT IS ON
//
// WIFI_SSID is the name of the access point the radio is on, or nothing
// when it is on none, looked for every five seconds, which the check after
// a join shares. The panel's WiFi row in SET shows it, the author's asking
// of 18 October 2026. It is a report: never kept, and set at the radio
// only, never by a client.
//
// For the thread that owns the controls.

#include <stdio.h>
#include <string.h>

#include "control.h"
#include "link.h"
#include "panel.h"
#include "panel_wifi.h"
#include "wifi.h"

#define SCAN_TAKES_MS 4000
#define NOT_WAITING (-1L)
#define NOTHING_TO_SEND (-1)
#define JOINED_CHECK_AFTER_MS 10000
#define SSID_EVERY_MS 5000

static struct link_wifi_point points[LINK_WIFI_POINTS_MAX];
static int point_count;
static uint8_t part[LINK_COUNTED_MAX];
static int next_to_send = NOTHING_TO_SEND;
static int is_scan_wanted;
static long results_at_ms = NOT_WAITING;
static char joining[LINK_WIFI_NAME_MAX + 1];
static long joined_check_at_ms = NOT_WAITING;
static long ssid_check_at_ms = NOT_WAITING;

static struct control wifi_ssid = {
	.group = "SYSTEM", .name = "WIFI_SSID", .kind = CONTROL_TEXT,
	.is_set_at_the_radio = 1,
};

// makes the list from wpa_cli's results, and sets it to be sent to the
// panel; says how many access points it has
int panel_wifi_take_results(const char *results) {
	point_count = wifi_take_results(results, points, LINK_WIFI_POINTS_MAX);
	next_to_send = 0;
	return point_count;
}

static void read_the_results(void) {
	int i;

	point_count = wifi_read_results(points, LINK_WIFI_POINTS_MAX);
	next_to_send = 0;
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

static void take_the_ssid(const char *status) {
	char why[CONTROL_WHY_MAX];
	char ssid[LINK_WIFI_NAME_MAX + 1];

	wifi_ssid_in(status, ssid, sizeof(ssid));
	if (strcmp(wifi_ssid.value, ssid))
		control_set(wifi_ssid.name, ssid, ORIGIN_NONE, why, sizeof(why));
}

// whether the radio is on the access point it was asked to join
static void say_whether_joined(const char *status) {
	char said[LINK_SAY_MAX + 1];

	if (wifi_is_on(status, joining))
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
	trouble = wifi_join(gathered, key);
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

// the status, every SSID_EVERY_MS for WIFI_SSID, and once after a join, to
// say whether it was joined: one asking does for both when they fall
// together
static void poll_the_status(long now_ms) {
	char status[WIFI_ANSWER_MAX];
	int is_join_due = joined_check_at_ms != NOT_WAITING && now_ms >= joined_check_at_ms;

	if (ssid_check_at_ms == NOT_WAITING)
		ssid_check_at_ms = now_ms + SSID_EVERY_MS;
	if (now_ms < ssid_check_at_ms && !is_join_due)
		return;
	if (now_ms >= ssid_check_at_ms)
		ssid_check_at_ms = now_ms + SSID_EVERY_MS;
	if (wifi_status(status, sizeof(status)) < 0)
		status[0] = 0;
	take_the_ssid(status);
	if (is_join_due) {
		joined_check_at_ms = NOT_WAITING;
		say_whether_joined(status);
	}
}

void panel_wifi_poll(long now_ms) {
	if (is_scan_wanted) {
		is_scan_wanted = 0;
		if (wifi_scan() < 0)
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
