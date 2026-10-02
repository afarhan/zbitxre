// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_settings.c — the settings kept from one start to the next. It needs
// no radio; its file is in build/.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "settings.h"

#define SETTINGS_PATH "build/test_settings.ini"
#define WHY_MAX 80
#define LINE_MAX_LENGTH 200

static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static struct control freq = {
	.group = "RADIO", .name = "FREQ", .kind = CONTROL_NUMBER,
	.min = 500000, .max = 30000000, .step = 1, .value = "7088000",
};
static struct control band = {
	.group = "RADIO", .name = "BAND", .kind = CONTROL_CHOICE,
	.choices = "40M/20M", .value = "40M",
};
static struct control wpm = {
	.group = "CW", .name = "WPM", .kind = CONTROL_NUMBER,
	.min = 5, .max = 50, .step = 1, .value = "20",
};
static struct control callsign = {
	.group = "STATION", .name = "MYCALLSIGN", .kind = CONTROL_TEXT,
};
static struct control macro = {
	.group = "CW", .name = "MACRO", .kind = CONTROL_ACTION, .choices = "CQ/RST",
};

static int file_has(const char *wanted) {
	char line[LINE_MAX_LENGTH];
	FILE *file = fopen(SETTINGS_PATH, "r");
	int found = 0;

	if (file == NULL)
		return 0;
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strncmp(line, wanted, strlen(wanted)) == 0)
			found = 1;
	}
	fclose(file);
	return found;
}

int main(void) {
	char why[WHY_MAX];

	control_register(&freq);
	control_register(&band);
	control_register(&wpm);
	control_register(&callsign);
	control_register(&macro);
	remove(SETTINGS_PATH);

	settings_load(SETTINGS_PATH);
	expect(strcmp(control_get("WPM"), "20") == 0, "with no file the controls keep what they started with");

	control_set("WPM", "27", ORIGIN_NONE, why, sizeof(why));
	settings_changed(&wpm, 1000);
	control_set("MYCALLSIGN", "VU2ESE", ORIGIN_NONE, why, sizeof(why));
	settings_changed(&callsign, 1500);
	control_set("FREQ", "14074000", ORIGIN_NONE, why, sizeof(why));
	settings_changed(&freq, 1600);
	settings_poll(3500);
	expect(!file_has("WPM="), "a change is not written while changes are still coming");
	settings_poll(3700);
	expect(file_has("WPM=27") && file_has("MYCALLSIGN=VU2ESE"),
		"two seconds after the last change the settings are written");
	expect(file_has("FREQ=14074000"), "the frequency is kept too");
	expect(!file_has("BAND=") && !file_has("MACRO="),
		"the band, which follows from the frequency, is not, and an action has nothing to keep");

	control_set("WPM", "12", ORIGIN_NONE, why, sizeof(why));
	control_set("MYCALLSIGN", "", ORIGIN_NONE, why, sizeof(why));
	control_set("FREQ", "7000000", ORIGIN_NONE, why, sizeof(why));
	settings_load(SETTINGS_PATH);
	expect(strcmp(control_get("WPM"), "27") == 0
		&& strcmp(control_get("MYCALLSIGN"), "VU2ESE") == 0
		&& strcmp(control_get("FREQ"), "14074000") == 0,
		"loaded again, the controls have what was written");

	if (checks_failed > 0) {
		printf("%d checks failed\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("settings: all checks passed\n");
	return EXIT_SUCCESS;
}
