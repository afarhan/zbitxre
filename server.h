// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// server.h — the radio's control protocol over TCP. See server.c.

#pragma once

#include <poll.h>
#include <stdint.h>

#include "control.h"

#define SERVER_PORT 8081
#define SERVER_CLIENTS_MAX 8
#define SERVER_POLLS (1 + SERVER_CLIENTS_MAX)
#define SERVER_SPECTRUM_POINTS_MAX 1024

int server_open(int port);
void server_close(void);
int server_get_pollfds(struct pollfd *polls);
void server_handle_events(const struct pollfd *polls, int count);
void server_broadcast(const struct control *c, int origin);
int server_wants_spectrum(void);
void server_send_spectrum(int span_hz, const uint8_t *points, int count);
void server_send_messages(void);
void server_on_answer(void (*answer)(const char *line));
void server_say(const char *text);
void server_warn(const char *text);
void server_show_qso(const char *text);
void server_send_cw(const char *text);
