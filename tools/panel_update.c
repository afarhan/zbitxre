// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_update.c — gives the zbitx's front panel new firmware over I2C.
//
//     panel_update --version        what firmware the panel has
//     panel_update image.bin.gz     gives the panel that image
//     panel_update --bootsel        restarts the panel as a USB drive
//
// The author's plan of 16 October 2026: the release of that day goes on the
// panel with the USB cable, and every later one over the I2C bus the radio
// and the panel share, with this. front_panel/updater.cpp is the panel's
// side and says how the image is kept and put in place; link.h has the
// messages.
//
// The radio program must not be running: it polls the panel on the same
// bus. setup/update.sh stops it first, as it does for any update.
//
// Each part of the image is sent, then the panel's answer is read until it
// says it has written the part, READY with the next offset it wants; while
// it writes flash it may not answer at all, and the reading is tried again.
// A part whose answer was lost is sent again, and the panel takes it once.
// When the image is whole the panel checks its MD5, says DONE, and
// restarts; its loader copies the image into place. Anything that goes
// wrong leaves the panel with the firmware it had.

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "i2c.h"
#include "link.h"
#include "md5.h"

#define ANSWER_TRIES_MS 30000
#define BETWEEN_TRIES_MS 5
#define RESTART_WAIT_MS 60000
#define SIZE_MOST (1024 * 1024)

struct answer {
	int state;
	uint32_t wanted;
	char version[LINK_VERSION_ROOM + 1];
};

static long now_ms(void) {
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void sleep_ms(int ms) {
	usleep((useconds_t)ms * 1000);
}

// whether a program called sdr is running: it polls the panel
static int is_radio_running(void) {
	DIR *proc = opendir("/proc");
	struct dirent *entry;
	char path[300];
	char name[64];
	FILE *file;
	int found = 0;

	if (proc == NULL)
		return 0;
	while (!found && (entry = readdir(proc)) != NULL) {
		if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
			continue;
		snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
		file = fopen(path, "r");
		if (file == NULL)
			continue;
		if (fgets(name, sizeof(name), file) != NULL && !strcmp(name, "sdr\n"))
			found = 1;
		fclose(file);
	}
	closedir(proc);
	return found;
}

// a message: the mark, its kind, what it carries, and the CRC-16
static int send_message(char kind, const uint8_t *carried, int count) {
	uint8_t message[LINK_UPDATE_MARK_SIZE + 1 + 4 + 1 + LINK_UPDATE_DATA_MAX + 2];
	uint16_t crc;
	int used = 0;

	memcpy(message, LINK_UPDATE_MARK, LINK_UPDATE_MARK_SIZE);
	used += LINK_UPDATE_MARK_SIZE;
	message[used++] = (uint8_t)kind;
	if (count > 0)
		memcpy(message + used, carried, count);
	used += count;
	crc = link_crc16(message, used);
	message[used++] = (uint8_t)crc;
	message[used++] = (uint8_t)(crc >> 8);
	return i2c_send(LINK_ADDRESS, message, used);
}

static int read_answer(struct answer *a) {
	uint8_t bytes[I2C_COUNTED_ROOM];
	int count = i2c_receive_counted(LINK_ADDRESS, bytes, sizeof(bytes));

	if (count != LINK_UPDATE_ANSWER || bytes[1] != (uint8_t)LINK_UPDATE_MARK[0])
		return -1;
	a->state = bytes[2];
	a->wanted = (uint32_t)bytes[3] | (uint32_t)bytes[4] << 8
		| (uint32_t)bytes[5] << 16 | (uint32_t)bytes[6] << 24;
	memcpy(a->version, bytes + 7, LINK_VERSION_ROOM);
	a->version[LINK_VERSION_ROOM] = 0;
	return 0;
}

// the panel's answer once it is no longer busy, or -1 when it does not come
static int wait_for_answer(struct answer *a, long within_ms) {
	long until = now_ms() + within_ms;

	while (now_ms() < until) {
		if (read_answer(a) == 0 && a->state != LINK_UPDATE_BUSY)
			return 0;
		sleep_ms(BETWEEN_TRIES_MS);
	}
	return -1;
}

static int ask(struct answer *a) {
	if (send_message(LINK_UPDATE_ASK, NULL, 0) < 0)
		return -1;
	return wait_for_answer(a, 2000);
}

static uint8_t *read_image(const char *path, size_t *size) {
	FILE *file = fopen(path, "rb");
	uint8_t *bytes;
	long length;

	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	rewind(file);
	if (length <= 0 || length > SIZE_MOST) {
		fclose(file);
		return NULL;
	}
	bytes = malloc((size_t)length);
	if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
		free(bytes);
		fclose(file);
		return NULL;
	}
	fclose(file);
	*size = (size_t)length;
	return bytes;
}

static void put_number(uint8_t *at, uint32_t n) {
	at[0] = (uint8_t)n;
	at[1] = (uint8_t)(n >> 8);
	at[2] = (uint8_t)(n >> 16);
	at[3] = (uint8_t)(n >> 24);
}

static int give_image(const uint8_t *image, size_t size) {
	uint8_t carried[4 + 1 + LINK_UPDATE_DATA_MAX];
	char md5[MD5_HEX_ROOM];
	struct answer a;
	uint32_t offset = 0;
	int length;
	int percent = -1;
	long began = now_ms();

	md5_hex(image, size, md5);
	put_number(carried, (uint32_t)size);
	memcpy(carried + 4, md5, LINK_UPDATE_MD5_HEX);
	if (send_message(LINK_UPDATE_BEGIN, carried, 4 + LINK_UPDATE_MD5_HEX) < 0
			|| wait_for_answer(&a, ANSWER_TRIES_MS) < 0 || a.state != LINK_UPDATE_READY) {
		fprintf(stderr, "panel_update: the panel did not begin the update\n");
		return -1;
	}
	while (offset < size) {
		length = size - offset > LINK_UPDATE_DATA_MAX ? LINK_UPDATE_DATA_MAX
			: (int)(size - offset);
		put_number(carried, offset);
		carried[4] = (uint8_t)length;
		memcpy(carried + 5, image + offset, length);
		if (send_message(LINK_UPDATE_DATA, carried, 5 + length) < 0) {
			sleep_ms(BETWEEN_TRIES_MS);
			continue;
		}
		if (wait_for_answer(&a, ANSWER_TRIES_MS) < 0) {
			fprintf(stderr, "panel_update: the panel stopped answering at %u\n", offset);
			return -1;
		}
		if (a.state == LINK_UPDATE_FAILED) {
			fprintf(stderr, "panel_update: the panel refused the part at %u\n", offset);
			return -1;
		}
		// the panel says where it is; a part it did not get is sent again
		offset = a.wanted;
		if ((int)(offset * 100 / size) != percent) {
			percent = (int)(offset * 100 / size);
			printf("\rpanel_update: %3d%%", percent);
			fflush(stdout);
		}
	}
	printf("\n");
	if (send_message(LINK_UPDATE_END, NULL, 0) < 0
			|| wait_for_answer(&a, ANSWER_TRIES_MS) < 0 || a.state != LINK_UPDATE_DONE) {
		fprintf(stderr, "panel_update: the panel found the image wrong and keeps its own\n");
		return -1;
	}
	printf("panel_update: sent in %ld s; the panel is restarting with it\n",
		(now_ms() - began) / 1000);
	return 0;
}

// after its restart, and the loader's copy, the panel answers again
static int wait_for_restart(struct answer *a) {
	long until = now_ms() + RESTART_WAIT_MS;

	sleep_ms(3000);
	while (now_ms() < until) {
		if (ask(a) == 0)
			return 0;
		sleep_ms(500);
	}
	return -1;
}

int main(int argc, char **argv) {
	struct answer a;
	uint8_t *image;
	size_t size;

	if (argc != 2) {
		fprintf(stderr, "usage: panel_update --version | --bootsel | image.bin[.gz]\n");
		return 2;
	}
	if (is_radio_running()) {
		fprintf(stderr, "panel_update: the radio program is running; stop it first\n");
		return 1;
	}
	if (i2c_open_bus_of(LINK_ADDRESS) < 0) {
		fprintf(stderr, "panel_update: no panel answers on any I2C bus\n");
		return 1;
	}
	if (!strcmp(argv[1], "--bootsel"))
		return send_message(LINK_UPDATE_BOOTSEL, NULL, 0) < 0 ? 1 : 0;
	if (ask(&a) < 0) {
		fprintf(stderr, "panel_update: the panel does not take updates over I2C; "
			"its firmware is older, and goes on with the USB cable\n");
		return 1;
	}
	if (!strcmp(argv[1], "--version")) {
		printf("%s\n", a.version);
		return 0;
	}
	image = read_image(argv[1], &size);
	if (image == NULL) {
		fprintf(stderr, "panel_update: %s cannot be read, or is not an image\n", argv[1]);
		return 1;
	}
	printf("panel_update: the panel has %s; giving it %s, %zu bytes\n",
		a.version, argv[1], size);
	if (give_image(image, size) < 0) {
		send_message(LINK_UPDATE_ABORT, NULL, 0);
		free(image);
		return 1;
	}
	free(image);
	if (wait_for_restart(&a) < 0) {
		fprintf(stderr, "panel_update: the panel has not answered since its restart\n");
		return 1;
	}
	printf("panel_update: the panel has %s now\n", a.version);
	return 0;
}
