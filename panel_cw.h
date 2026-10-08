// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// panel_cw.h — the CW read off the air, to the front panel and the
// terminal. See panel_cw.c.

#pragma once

void panel_cw_poll(long now_ms);
void panel_cw_also_tell(void (*read)(const char *text));
