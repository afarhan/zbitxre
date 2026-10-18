// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// halted.cpp — what the panel does when LVGL finds it cannot go on: most
// often, that it has run out of its memory for what is on the screens.
//
// LVGL's way was to stop dead, and the panel froze, as the author found on
// 18 October 2026, opening SET. Once LVGL has failed to get memory what it
// holds cannot be trusted, and drawing a word on the screen would want
// memory too, so the panel says why on the serial port and starts again,
// which takes a second, and comes back on the main screen with all its
// memory free. lv_conf.h names this as LVGL's assert handler.

#include <Arduino.h>

#include "halted.h"

#define SAID_FOR_MS 50

void panel_halted(void) {
	Serial.println("panel: LVGL could not go on, out of memory most likely; restarting");
	Serial.flush();
	delay(SAID_FOR_MS);
	rp2040.reboot();
}
