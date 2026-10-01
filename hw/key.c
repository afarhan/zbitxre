// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// key.c — the key jack's two contacts, each closure and opening with its
// time.
//
// A CW keyer must never lose a tap of a paddle, and must know when each
// contact closed and opened far more closely than the radio's loop comes
// round. So the pins are not looked at from time to time: the kernel is
// asked to report every edge on them. It notes the time of each edge as
// the edge happens, to the microsecond, and keeps the reports until they
// are read. A tap that begins and ends between two reads is two reports,
// each with its own time.
//
// The contacts are the tip and the ring of the KEY jack, on the pins the
// board gives as its key and its PTT. board.c has already made them inputs
// with pull-ups, so a closed contact reads low and a falling edge is a
// closure.
//
// key_take_event() gives the reports one at a time, the earliest first, and
// never waits: it is for the audio thread. The times are on the clock that
// key_now_us() reads, so that whoever takes them can tell how long ago an
// edge was.
//
// The reports come through the kernel's GPIO character device,
// /dev/gpiochip0, which since Linux 5.7 stamps them with its monotonic
// clock. hw/gpio.c goes on reading and writing the pins' registers as
// before; the two do not interfere.
//
// A pin can be reported to one asker only. zbitxv2 asks through wiringPi,
// which claims the pin in /sys/class/gpio, and the claim outlives the
// program: once zbitxv2 has run, the kernel refuses this file its reports
// until the radio is restarted. With no reports the keyer has only the
// pins as they are at the start of each block, loses the short taps and the
// paddle memory, and is no longer iambic to the hand; that is how it was
// found. So a pin that is refused is released from /sys/class/gpio and
// asked for once more. A radio on which both programs are run in turn,
// as they will be for some time, then keys properly in this one.
//
// key_open() returns 0, or -1 if the kernel will not report the edges.

#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "key.h"

#define CHIP "/dev/gpiochip0"
#define SYSFS_RELEASE "/sys/class/gpio/unexport"
#define PIN_TEXT_MAX 8
#define CONTACTS 2
#define NOT_OPEN (-1)
#define NS_PER_US 1000
#define US_PER_SECOND 1000000LL

static int event_fd[CONTACTS] = { NOT_OPEN, NOT_OPEN };
static struct key_event waiting[CONTACTS];
static int is_waiting[CONTACTS];

// releases a pin that an earlier program claimed in /sys/class/gpio
static void release_from_sysfs(int pin) {
	char text[PIN_TEXT_MAX];
	int file = open(SYSFS_RELEASE, O_WRONLY);
	int length;

	if (file < 0)
		return;
	length = snprintf(text, sizeof(text), "%d", pin);
	if (write(file, text, length) != length)
		length = 0;
	close(file);
}

static int request_events(int chip, int pin, const char *label,
		struct gpioevent_request *request) {
	memset(request, 0, sizeof(*request));
	request->lineoffset = pin;
	request->handleflags = GPIOHANDLE_REQUEST_INPUT;
	request->eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
	strncpy(request->consumer_label, label, sizeof(request->consumer_label) - 1);
	return ioctl(chip, GPIO_GET_LINEEVENT_IOCTL, request);
}

static int open_contact(int chip, int pin, const char *label) {
	struct gpioevent_request request;
	int flags;

	if (request_events(chip, pin, label, &request) < 0) {
		release_from_sysfs(pin);
		if (request_events(chip, pin, label, &request) < 0)
			return NOT_OPEN;
	}
	flags = fcntl(request.fd, F_GETFL, 0);
	if (flags < 0 || fcntl(request.fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		close(request.fd);
		return NOT_OPEN;
	}
	return request.fd;
}

int key_open(const struct board *board) {
	int chip;

	if (board == NULL)
		return -1;
	chip = open(CHIP, O_RDONLY);
	if (chip < 0)
		return -1;
	event_fd[KEY_TIP] = open_contact(chip, board->key_pin, "zbitxre key tip");
	event_fd[KEY_RING] = open_contact(chip, board->ptt_pin, "zbitxre key ring");
	close(chip);
	is_waiting[KEY_TIP] = 0;
	is_waiting[KEY_RING] = 0;
	if (event_fd[KEY_TIP] == NOT_OPEN || event_fd[KEY_RING] == NOT_OPEN) {
		key_close();
		return -1;
	}
	return 0;
}

void key_close(void) {
	int contact;

	for (contact = 0; contact < CONTACTS; contact++) {
		if (event_fd[contact] != NOT_OPEN)
			close(event_fd[contact]);
		event_fd[contact] = NOT_OPEN;
	}
}

// the next report from a contact, if there is one and none is held yet
static void read_contact(int contact) {
	struct gpioevent_data edge;

	if (is_waiting[contact] || event_fd[contact] == NOT_OPEN)
		return;
	if (read(event_fd[contact], &edge, sizeof(edge)) != (ssize_t)sizeof(edge))
		return;
	waiting[contact].contact = contact;
	waiting[contact].is_closed = edge.id == GPIOEVENT_EVENT_FALLING_EDGE;
	waiting[contact].us = (int64_t)(edge.timestamp / NS_PER_US);
	is_waiting[contact] = 1;
}

int key_take_event(struct key_event *event) {
	int earliest = KEY_TIP;

	read_contact(KEY_TIP);
	read_contact(KEY_RING);
	if (!is_waiting[KEY_TIP] && !is_waiting[KEY_RING])
		return 0;
	if (!is_waiting[KEY_TIP])
		earliest = KEY_RING;
	if (is_waiting[KEY_TIP] && is_waiting[KEY_RING]
			&& waiting[KEY_RING].us < waiting[KEY_TIP].us)
		earliest = KEY_RING;
	*event = waiting[earliest];
	is_waiting[earliest] = 0;
	return 1;
}

int64_t key_now_us(void) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * US_PER_SECOND + now.tv_nsec / NS_PER_US;
}
