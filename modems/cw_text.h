// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// cw_text.h — text sent as CW: characters in, the key down or up out, a
// sample at a time. See cw_text.c.

#pragma once

void cw_text_reset(void);
void cw_text_set_wpm(int wpm);
int cw_text_put(const char *text);
int cw_text_put_code(const char *code, int gap_ms);
void cw_text_abort(void);
int cw_text_is_key_down(void);
int cw_text_is_busy(void);
