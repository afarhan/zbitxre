// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// linking.c — getting the radio its key from my.sbitx.net, and keeping it.
//
// A radio proves who it is to my.sbitx.net with a key the service gave it;
// PROTOCOL.md says how. This file gets the key, once, when the owner first
// turns remote access on, and keeps it.
//
// A client, the window on another computer, has a key of its own and gets
// it in the same way, so the window is built with this file too, and says
// "client" where the radio's daemon says "radio". What is said of the radio
// below is so of both.
//
// LINKING, AS THE OPERATOR SEES IT
//
// The radio shows eight letters, "CODE BCDF-GHJK". The operator signs in at
// my.sbitx.net on a phone or a computer, chooses to link a radio and types
// the letters. Within a few seconds the radio has its key. Nothing is typed
// on the radio and the account's password never comes near it.
//
// HOW
//
// By three requests to the website, which accounts/README.md lists: one for
// a code, and then one every few seconds, as often as the service said, to
// ask whether the owner has typed it, until they have or the code's ten
// minutes are up. The answers are JSON of one level, and json_get() picks a
// value out by its name; it is not a JSON reader and is not offered as one.
//
// The website is asked over HTTPS, though nothing else the radio says to
// my.sbitx.net is encrypted: the key itself comes back in the last answer,
// and a key read on the way would be the radio's identity in another's
// hands. The asking is done by curl, the program, run with each word of its
// command given as it is and not through a shell, as panel_wifi.c runs
// wpa_cli. curl is on every Raspberry Pi system, checks the website's
// certificate, and saves this program a TLS library to be built with. What
// is sent goes to curl on its standard input, not on its command line.
//
// Nothing here waits. linking_begin() starts it; linking_poll(), called
// each time round the program's loop, looks whether curl has finished and
// carries on from there. It says what there is to show the operator, and
// at the end whether the radio is linked. One request is out at a time, and
// curl gives up by itself after fifteen seconds.
//
// THE KEY
//
// It is kept in a file only the radio's user can read, data/remote.key
// unless the program is told otherwise: one line, the key's id, a space,
// and the key. It is never printed and is in no control.

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "linking.h"

#define CURL "curl"
#define CURL_GIVES_UP_S "15"
#define URL_MAX 128
#define BODY_MAX 256
#define ANSWER_MAX 1024
#define PATH_MAX_LENGTH 512
#define NAME_MAX_LENGTH 40
#define KEY_LINE_MAX (LINKING_KEY_ID_ROOM + LINKING_KEY_ROOM + 4)
#define KEY_FILE_MODE 0600
#define MS_PER_SECOND 1000
#define POLL_EVERY_S 5
#define CODE_LASTS_S 600
#define HTTP_OK 200
#define HTTP_TOO_MANY 429
#define STILL_ASKING (-2)
#define NO_ANSWER (-1)
#define NO_CHILD (-1)

enum state {
	IDLE,
	TO_ASK_FOR_A_CODE,
	ASKING_FOR_A_CODE,
	SHOWING_A_CODE,
	ASKING_WHETHER_LINKED,
};

static char web[URL_MAX];
static char key_path[PATH_MAX_LENGTH];
static char radio_name[NAME_MAX_LENGTH];
static char kind[NAME_MAX_LENGTH];
static enum state state = IDLE;
static char device_code[LINKING_KEY_ROOM];
static long code_ends_ms;
static long next_ask_ms;
static long ask_every_ms;
static pid_t curl_pid = NO_CHILD;
static int curl_output = -1;

// the value of a name in JSON of one level: a text without its quotes, or
// a number as it is written; says whether the name was there
static int json_get(const char *json, const char *name, char *value, int room) {
	char quoted[NAME_MAX_LENGTH + 4];
	const char *at;
	int used = 0;

	value[0] = 0;
	snprintf(quoted, sizeof(quoted), "\"%s\"", name);
	at = strstr(json, quoted);
	if (at == NULL)
		return 0;
	at += strlen(quoted);
	while (*at == ' ' || *at == ':')
		at++;
	if (*at == '"') {
		at++;
		while (*at != 0 && *at != '"' && used < room - 1)
			value[used++] = *at++;
	} else {
		while (*at != 0 && *at != ',' && *at != '}' && *at != ' ' && used < room - 1)
			value[used++] = *at++;
	}
	value[used] = 0;
	return 1;
}

// curl is started on a request, the body posted as JSON from its standard
// input, and is not waited for
static int ask(const char *path, const char *body) {
	const char *argv[12];
	char url[URL_MAX + 32];
	int to_child[2];
	int from_child[2];
	int count = 0;

	snprintf(url, sizeof(url), "%s%s", web, path);
	argv[count++] = CURL;
	argv[count++] = "--silent";
	argv[count++] = "--max-time";
	argv[count++] = CURL_GIVES_UP_S;
	argv[count++] = "--write-out";
	argv[count++] = "\n%{http_code}";
	argv[count++] = "--header";
	argv[count++] = "Content-Type: application/json";
	argv[count++] = "--data-binary";
	argv[count++] = "@-";
	argv[count++] = url;
	argv[count] = NULL;

	if (pipe(to_child) < 0)
		return -1;
	if (pipe(from_child) < 0) {
		close(to_child[0]);
		close(to_child[1]);
		return -1;
	}
	curl_pid = fork();
	if (curl_pid < 0) {
		close(to_child[0]);
		close(to_child[1]);
		close(from_child[0]);
		close(from_child[1]);
		curl_pid = NO_CHILD;
		return -1;
	}
	if (curl_pid == 0) {
		dup2(to_child[0], STDIN_FILENO);
		dup2(from_child[1], STDOUT_FILENO);
		close(to_child[0]);
		close(to_child[1]);
		close(from_child[0]);
		close(from_child[1]);
		execvp(CURL, (char *const *)argv);
		_exit(EXIT_FAILURE);
	}
	close(to_child[0]);
	close(from_child[1]);
	if (write(to_child[1], body, strlen(body)) < 0)
		kill(curl_pid, SIGTERM);
	close(to_child[1]);
	curl_output = from_child[0];
	return 0;
}

// whether curl has finished, and if it has, what the website said: the
// HTTP status, with the answer's text left in answer
static int answer_of(char *answer, int room) {
	char *last_line;
	ssize_t got;
	int used = 0;
	int status;

	if (curl_pid == NO_CHILD)
		return NO_ANSWER;
	if (waitpid(curl_pid, &status, WNOHANG) == 0)
		return STILL_ASKING;
	curl_pid = NO_CHILD;
	got = read(curl_output, answer, room - 1);
	while (got > 0 && used + got < room - 1) {
		used += (int)got;
		got = read(curl_output, answer + used, room - 1 - used);
	}
	if (got > 0)
		used += (int)got;
	answer[used] = 0;
	close(curl_output);
	curl_output = -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return NO_ANSWER;
	last_line = strrchr(answer, '\n');
	if (last_line == NULL)
		return NO_ANSWER;
	*last_line = 0;
	return atoi(last_line + 1);
}

static int keep_the_key(const char *key_id, const char *key) {
	char line[KEY_LINE_MAX];
	int length;
	int file;
	int is_written;

	length = snprintf(line, sizeof(line), "%s %s\n", key_id, key);
	file = open(key_path, O_WRONLY | O_CREAT | O_TRUNC, KEY_FILE_MODE);
	if (file < 0)
		return -1;
	is_written = write(file, line, length) == length;
	close(file);
	if (!is_written)
		return -1;
	return 0;
}

// the key and its id, from the file; says whether the radio has one
int linking_read_key(char *key_id, char *key) {
	char line[KEY_LINE_MAX];
	char *space;
	FILE *file = fopen(key_path, "r");

	key_id[0] = 0;
	key[0] = 0;
	if (file == NULL)
		return 0;
	if (fgets(line, sizeof(line), file) == NULL) {
		fclose(file);
		return 0;
	}
	fclose(file);
	line[strcspn(line, "\r\n")] = 0;
	space = strchr(line, ' ');
	if (space == NULL)
		return 0;
	*space = 0;
	snprintf(key_id, LINKING_KEY_ID_ROOM, "%.*s", LINKING_KEY_ID_ROOM - 1, line);
	snprintf(key, LINKING_KEY_ROOM, "%.*s", LINKING_KEY_ROOM - 1, space + 1);
	return key_id[0] != 0 && key[0] != 0;
}

void linking_forget_key(void) {
	unlink(key_path);
}

void linking_begin(void) {
	if (state == IDLE || state == SHOWING_A_CODE)
		state = TO_ASK_FOR_A_CODE;
}

// a code that is showing is dropped; a request that is out is let finish
// and its answer thrown away
void linking_stop(void) {
	char answer[ANSWER_MAX];

	if (curl_pid != NO_CHILD) {
		kill(curl_pid, SIGTERM);
		waitpid(curl_pid, NULL, 0);
		answer_of(answer, sizeof(answer));
	}
	state = IDLE;
	device_code[0] = 0;
}

static enum linking_result took_a_code(int http_status, const char *answer, long now_ms,
		char *status) {
	char user_code[NAME_MAX_LENGTH];
	char number[NAME_MAX_LENGTH];

	state = IDLE;
	if (http_status == HTTP_TOO_MANY) {
		snprintf(status, LINKING_STATUS_ROOM, "TOO MANY TRIES");
		return LINKING_FAILED;
	}
	if (http_status != HTTP_OK || !json_get(answer, "device_code", device_code,
			sizeof(device_code)) || !json_get(answer, "user_code", user_code,
			sizeof(user_code))) {
		snprintf(status, LINKING_STATUS_ROOM, "NO ANSWER FROM SERVER");
		return LINKING_FAILED;
	}
	ask_every_ms = POLL_EVERY_S * MS_PER_SECOND;
	if (json_get(answer, "interval", number, sizeof(number)) && atoi(number) > 0)
		ask_every_ms = atol(number) * MS_PER_SECOND;
	code_ends_ms = now_ms + CODE_LASTS_S * MS_PER_SECOND;
	if (json_get(answer, "expires_in", number, sizeof(number)) && atoi(number) > 0)
		code_ends_ms = now_ms + atol(number) * MS_PER_SECOND;
	next_ask_ms = now_ms + ask_every_ms;
	snprintf(status, LINKING_STATUS_ROOM, "CODE %.16s", user_code);
	state = SHOWING_A_CODE;
	return LINKING_UNDER_WAY;
}

static enum linking_result took_whether_linked(int http_status, const char *answer,
		long now_ms, char *status) {
	char said[NAME_MAX_LENGTH];
	char key_id[LINKING_KEY_ID_ROOM];
	char key[LINKING_KEY_ROOM];

	state = SHOWING_A_CODE;
	next_ask_ms = now_ms + ask_every_ms;
	if (http_status != HTTP_OK || !json_get(answer, "status", said, sizeof(said)))
		return LINKING_UNDER_WAY;
	if (!strcmp(said, "pending"))
		return LINKING_UNDER_WAY;
	state = IDLE;
	device_code[0] = 0;
	if (strcmp(said, "linked") || !json_get(answer, "key", key, sizeof(key))
			|| !json_get(answer, "key_id", key_id, sizeof(key_id))) {
		snprintf(status, LINKING_STATUS_ROOM, "CODE RAN OUT");
		return LINKING_FAILED;
	}
	if (keep_the_key(key_id, key) < 0) {
		snprintf(status, LINKING_STATUS_ROOM, "KEY NOT SAVED");
		return LINKING_FAILED;
	}
	snprintf(status, LINKING_STATUS_ROOM, "LINKED");
	return LINKING_LINKED;
}

// carries the linking on. status is written only when there is something
// new to show; LINKING_LINKED and LINKING_FAILED are each said once
enum linking_result linking_poll(long now_ms, char *status) {
	char answer[ANSWER_MAX];
	char body[BODY_MAX];
	int http_status;

	status[0] = 0;
	if (state == IDLE)
		return LINKING_IDLE;
	if (state == TO_ASK_FOR_A_CODE) {
		snprintf(body, sizeof(body), "{\"name\": \"%s\", \"kind\": \"%s\"}", radio_name,
			kind);
		if (ask("/api/radio/link", body) < 0) {
			state = IDLE;
			snprintf(status, LINKING_STATUS_ROOM, "NO ANSWER FROM SERVER");
			return LINKING_FAILED;
		}
		state = ASKING_FOR_A_CODE;
		snprintf(status, LINKING_STATUS_ROOM, "ASKING");
		return LINKING_UNDER_WAY;
	}
	if (state == ASKING_FOR_A_CODE || state == ASKING_WHETHER_LINKED) {
		http_status = answer_of(answer, sizeof(answer));
		if (http_status == STILL_ASKING)
			return LINKING_UNDER_WAY;
		if (state == ASKING_FOR_A_CODE)
			return took_a_code(http_status, answer, now_ms, status);
		return took_whether_linked(http_status, answer, now_ms, status);
	}
	if (now_ms >= code_ends_ms) {
		state = IDLE;
		device_code[0] = 0;
		snprintf(status, LINKING_STATUS_ROOM, "CODE RAN OUT");
		return LINKING_FAILED;
	}
	if (now_ms < next_ask_ms)
		return LINKING_UNDER_WAY;
	snprintf(body, sizeof(body), "{\"device_code\": \"%s\"}", device_code);
	if (ask("/api/radio/link/poll", body) == 0)
		state = ASKING_WHETHER_LINKED;
	else
		next_ask_ms = now_ms + ask_every_ms;
	return LINKING_UNDER_WAY;
}

void linking_open(const char *web_url, const char *key_file, const char *name,
		const char *radio_or_client) {
	snprintf(web, sizeof(web), "%s", web_url);
	snprintf(key_path, sizeof(key_path), "%s", key_file);
	snprintf(radio_name, sizeof(radio_name), "%s", name);
	snprintf(kind, sizeof(kind), "%s", radio_or_client);
	state = IDLE;
	device_code[0] = 0;
}
