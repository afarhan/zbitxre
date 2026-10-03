// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// ft8_qso.h — an FT8 contact, from the call to the logbook. See ft8_qso.c.

#pragma once

void ft8_qso_open(void);
void ft8_qso_poll(void);
int ft8_qso_answer(const char *line);
int ft8_qso_answer_call(const char *callsign);
