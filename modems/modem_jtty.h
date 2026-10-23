// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_jtty.h — what of JTTY's modem the radio program uses: the text
// heard in each part of the passband. See modem_jtty.c.

#pragma once

#define JTTY_BINS 24
#define JTTY_BIN_TEXT_ROOM 81

void jtty_poll_bins(void (*tell)(int bin, int hz, const char *text));
void jtty_resend_bins(void);
