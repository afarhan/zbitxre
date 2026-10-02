// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_panel_wifi.c — the list of WiFi access points for the panel.
//
// It needs no radio, no panel and no WiFi. panel_wifi.c is given results
// in the form wpa_cli prints them, and in place of hw/panel.c the radio's
// end of a link whose other end is here, as in test_panel_controls.c. What
// the panel's end takes is the list a panel would show.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "link.h"
#include "panel.h"
#include "panel_wifi.h"

#define PANEL_ROOM (LINK_ANSWER_MAX - 1)
#define TURNS 12
#define RESULTS_MAX 4096
#define MANY 20
#define LONG_NAME "AN ACCESS POINT WITH A LONG NAME"

static struct link_end radio;
static struct link_end panel;

static struct link_wifi_point taken[LINK_WIFI_POINTS_MAX + 4];
static int taken_count;
static int parts_taken;
static int lists_ended;
static int out_of_place;
static char asked[2048];
static int asked_used;
static char last_said[LINK_SAY_MAX + 1];
static const char *on_now = "Airtel_71hh";
static int is_key_refused;

static int lose_the_word;
static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

// in place of hw/panel.c
void panel_set_kept_bytes(int control, const uint8_t *bytes, int count) {
	link_offer_kept_bytes(&radio, control, bytes, count);
}

int panel_is_waiting(int control) {
	return link_is_waiting(&radio, control);
}

void panel_say(const char *text) {
	snprintf(last_said, sizeof(last_said), "%s", text);
}

// in place of wpa_cli: what was asked is noted, the words with a space
// between them and a line to each command, and the answers are made up
static int ask_here(const char *const *words, char *answer, int room) {
	int i;

	for (i = 0; words[i] != NULL; i++) {
		if (i > 0)
			asked_used += snprintf(asked + asked_used, sizeof(asked) - asked_used, " ");
		asked_used += snprintf(asked + asked_used, sizeof(asked) - asked_used,
			"%s", words[i]);
	}
	asked_used += snprintf(asked + asked_used, sizeof(asked) - asked_used, "\n");
	if (!strcmp(words[0], "list_networks"))
		snprintf(answer, room, "network id / ssid / bssid / flags\n"
			"0\tFarhan\tany\t\n13\tAirtel_71hh\tany\t[CURRENT]\n");
	else if (!strcmp(words[0], "add_network"))
		snprintf(answer, room, "14\n");
	else if (!strcmp(words[0], "status"))
		snprintf(answer, room, "bssid=6e:22\nssid=%s\nwpa_state=COMPLETED\n", on_now);
	else if (!strcmp(words[0], "set_network") && is_key_refused && !strcmp(words[2], "psk"))
		snprintf(answer, room, "FAIL\n");
	else
		snprintf(answer, room, "OK\n");
	return 0;
}

// the panel's choice, sent as the panel sends a text: in parts
static void choose(const char *name, const char *key, long now_ms) {
	uint8_t part[LINK_TEXT_PART_HEADER + LINK_TEXT_PART_DATA];
	char text[LINK_TEXT_MAX + 1];
	int offset = 0;
	int count;

	asked_used = 0;
	asked[0] = 0;
	last_said[0] = 0;
	snprintf(text, sizeof(text), "%s%c%s", name, LINK_WIFI_BETWEEN, key);
	do {
		count = link_put_text_part(part, LINK_WIFI_CHOSEN, text, offset);
		panel_wifi_took_bytes(LINK_TEXT_PART, part, count, now_ms);
		offset += count - LINK_TEXT_PART_HEADER;
	} while (!part[2]);
}

static void on_panel_took_bytes(int control, const uint8_t *bytes, int count) {
	struct link_wifi_point point;
	int at = LINK_WIFI_PART_HEADER;
	int used;

	if (control != LINK_WIFI_LIST || count < LINK_WIFI_PART_HEADER)
		return;
	parts_taken++;
	if (bytes[0] == 0)
		taken_count = 0;
	if (bytes[0] != taken_count)
		out_of_place++;
	while (at < count) {
		used = link_get_wifi_point(bytes + at, count - at, &point);
		if (used < 0)
			return;
		at += used;
		if (taken_count < (int)(sizeof(taken) / sizeof(taken[0])))
			taken[taken_count++] = point;
	}
	if (bytes[1])
		lists_ended++;
}

static void one_turn(void) {
	uint8_t answer[LINK_ANSWER_MAX + LINK_FRAME_MAX];
	uint8_t word[LINK_FRAME_MAX];
	int count;

	link_make_answer(&panel, answer);
	link_take_frame(&radio, answer + 1, answer[0], NULL, NULL);
	panel_wifi_poll(0);
	if (!link_has_something_to_say(&radio))
		return;
	count = link_make_frame(&radio, word);
	if (!lose_the_word)
		link_take_frame(&panel, word, count, NULL, on_panel_took_bytes);
}

static void turns(int count) {
	int i;

	for (i = 0; i < count; i++)
		one_turn();
}

static void forget_what_was_taken(void) {
	taken_count = 0;
	parts_taken = 0;
	lists_ended = 0;
	out_of_place = 0;
}

static void check_a_few(void) {
	static const char results[] =
		"bssid / frequency / signal level / flags / ssid\n"
		"20:0c:86:b6:cd:61\t2437\t-78\t[WPA-PSK-CCMP+TKIP][WPA2-PSK-CCMP+TKIP][ESS]\tAirtel_ship_4039\n"
		"54:af:97:ac:1f:d1\t2437\t-81\t[ESS]\tA cafe\n"
		"6e:22:f7:f5:17:dc\t2462\t-62\t[WPA2-PSK-CCMP][ESS][UTF-8]\tAirtel_71hh\n"
		"6e:22:f7:f5:17:dd\t5180\t-55\t[WPA2-PSK-CCMP][ESS]\tAirtel_71hh\n"
		"aa:bb:cc:dd:ee:ff\t2412\t-40\t[WPA2-PSK-CCMP][ESS]\t\n";

	forget_what_was_taken();
	expect(panel_wifi_take_results(results) == 3,
		"of five results, the one heard twice is listed once and the one with no name left out");
	turns(TURNS);
	expect(taken_count == 3 && lists_ended == 1 && parts_taken == 1,
		"the panel is sent the three, in one part, marked as the last");
	expect(strcmp(taken[0].name, "Airtel_71hh") == 0 && taken[0].signal_dbm == -55
		&& taken[0].is_secured,
		"the strongest is first, at the stronger of its two signals, and wants a key");
	expect(strcmp(taken[1].name, "Airtel_ship_4039") == 0
		&& strcmp(taken[2].name, "A cafe") == 0 && !taken[2].is_secured,
		"then the rest by strength, a name with a space whole, an open one marked open");
}

static void check_many(void) {
	static char results[RESULTS_MAX];
	int used = 0;
	int is_sorted = 1;
	int i;

	used += snprintf(results + used, sizeof(results) - used,
		"bssid / frequency / signal level / flags / ssid\n");
	for (i = 0; i < MANY; i++)
		used += snprintf(results + used, sizeof(results) - used,
			"00:00:00:00:00:%02x\t2412\t%d\t[WPA2-PSK-CCMP][ESS]\t%s %02d\n",
			i, -90 + 2 * ((i * 7) % MANY), LONG_NAME, i);

	forget_what_was_taken();
	expect(panel_wifi_take_results(results) == LINK_WIFI_POINTS_MAX,
		"of twenty access points sixteen are kept");
	lose_the_word = 1;
	turns(2);
	lose_the_word = 0;
	turns(TURNS);
	for (i = 1; i < taken_count; i++) {
		if (taken[i].signal_dbm > taken[i - 1].signal_dbm)
			is_sorted = 0;
	}
	expect(taken_count == LINK_WIFI_POINTS_MAX && lists_ended == 1 && parts_taken > 1
		&& out_of_place == 0,
		"sixteen long names go in several parts, in their places, though a frame was lost");
	expect(is_sorted && taken[0].signal_dbm == -52 && taken[LINK_WIFI_POINTS_MAX - 1].signal_dbm == -82,
		"they are the sixteen strongest, the strongest first");
}

static void check_none(void) {
	forget_what_was_taken();
	taken_count = 5;
	expect(panel_wifi_take_results("bssid / frequency / signal level / flags / ssid\n") == 0,
		"results with no access points make an empty list");
	turns(TURNS);
	expect(taken_count == 0 && lists_ended == 1 && parts_taken == 1,
		"which the panel is sent as one part with nothing in it");
}

static void check_joining(void) {
	panel_wifi_set_asking(ask_here);

	choose("Airtel_71hh", "a new key for it", 1000);
	expect(strcmp(asked,
		"list_networks\n"
		"set_network 13 psk \"a new key for it\"\n"
		"enable_network 13\n"
		"save_config\n"
		"select_network 13\n") == 0,
		"an access point the radio knows is given its new key, saved, and then selected");
	expect(strcmp(last_said, "wifi: joining Airtel_71hh") == 0, "the panel is told it is joining");
	panel_wifi_poll(5000);
	expect(strcmp(last_said, "wifi: joining Airtel_71hh") == 0, "nothing more is said for ten seconds");
	asked_used = 0;
	panel_wifi_poll(11000);
	expect(strcmp(asked, "status\n") == 0 && strcmp(last_said, "wifi: joined Airtel_71hh") == 0,
		"then the radio looks, and says it has joined");

	choose("A new one", "its key 123", 20000);
	expect(strstr(asked, "add_network\nset_network 14 ssid 41206e6577206f6e65\n"
		"set_network 14 psk \"its key 123\"\nenable_network 14\n") != NULL,
		"one it does not know is added, its name given in hex");
	panel_wifi_poll(31000);
	expect(strcmp(last_said, "wifi: not on A new one") == 0,
		"and if the radio is not on it ten seconds later, that is said");

	choose("A cafe", "", 40000);
	expect(strstr(asked, "set_network 14 key_mgmt NONE\n") != NULL && strstr(asked, "psk") == NULL,
		"an open access point is given no key");

	choose("Airtel_71hh", "short", 50000);
	expect(asked[0] == 0 && strcmp(last_said, "wifi: a key is 8 to 63 characters") == 0,
		"a key of five characters is refused before anything is asked");
	is_key_refused = 1;
	choose("Airtel_71hh", "refused by wpa", 60000);
	is_key_refused = 0;
	expect(strstr(asked, "select_network") == NULL
		&& strcmp(last_said, "wifi: the key was not taken") == 0,
		"a key that is not taken stops it there, and nothing is selected");
}

int main(void) {
	link_start(&radio, LINK_FRAME_MAX);
	link_start(&panel, PANEL_ROOM);
	turns(TURNS);
	expect(panel_wifi_open() == 0, "WIFI is registered as a control");

	check_a_few();
	check_many();
	check_none();
	check_joining();

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("panel wifi: all checks passed\n");
	return EXIT_SUCCESS;
}
