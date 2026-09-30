// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// server.h — the radio's control protocol over TCP. See server.c.

#pragma once

#include <poll.h>

#include "control.h"

#define SERVER_PORT 8081
#define SERVER_CLIENTS_MAX 8
#define SERVER_POLLS (1 + SERVER_CLIENTS_MAX)

int server_open(int port);
void server_close(void);
int server_get_pollfds(struct pollfd *polls);
void server_handle_events(const struct pollfd *polls, int count);
void server_broadcast(const struct control *c, int origin);
