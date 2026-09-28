// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// front_panel.ino — the zbitx's front panel: a Raspberry Pi Pico W with a
// 480 by 320 touch screen and a knob.
//
// This is the beginning of the panel for zbitxre. It comes from the sketch
// zbitx_front_panel_lvgl, and draws its controls with LVGL. It has two
// controls, the frequency and the volume, and tells the radio of them over
// I2C. Below them it shows the spectrum the radio sends, and its waterfall.
//
// The files:
//
//   front_panel.ino   setup() and loop(), and nothing else
//   ui.cpp, ui.h      the display, the touch screen and the knob, given to LVGL
//   main.cpp          the main screen
//   block.cpp         what the panel remembers: access points and the touch
//                     screen's calibration, in the last 4K of the flash
//   ili9488.cpp       the display's driver, from ltp1_text_ui, where it was
//                     fully debugged
//   touch.cpp         the touch screen, which shares the display's SPI port
//   testcard.cpp      colour bars, shown for a moment as the panel starts
//   waterfall.cpp     the spectrum and the waterfall, below the controls
//   link.c, link.h    what the panel and the radio say to each other; the
//                     radio is built with these same two files
//   i2c_link.cpp      the panel's end of the link, an I2C slave
//   lv_conf.h         LVGL's settings
//
// TO PUT NEW FIRMWARE ON A PANEL
//
// Hold the knob down while the panel powers up. The Pico goes to its loader
// and shows up on USB as a drive called RPI-RP2. "make panel-flash" in the
// folder above copies the firmware to it. This works with the shipping
// firmware too, which is how a panel is moved to this one and back.
//
// Use a USB cable that is known to carry data. A cable with a bad data line
// lets the computer see that something is there and no more.
//
// THE SERIAL PORT
//
// The panel prints what it is doing on its USB serial port. It also takes
// some letters there. 's' sends back the whole screen, which
// tools/panel_screen.py turns into a picture. 'r' restarts the panel. 'i'
// says how the link to the radio and the waterfall have been doing. 'n'
// stops the waterfall being painted and 'w' starts it again, so that the
// hash of the painting can be told by ear from the rest.
//
// WHICH PICO CORE
//
// The sketch this comes from notes that it worked only with version 4.7 of
// the arduino-pico core, with noise and hang-ups under 5.0. That has not
// been looked into yet, and it bears on the noise the panel makes in the
// receiver.

#include <Arduino.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <lvgl.h>
#include "waterfall.h"
#include "ui.h"

//the panel waits this long for someone to open its serial port, so that
//its first lines are not lost, and then starts anyway
#define SERIAL_WAITED_FOR_MS 6000
#define SERIAL_LOOKED_AT_EVERY_MS 10

#define ASK_FOR_SCREEN 's'
#define ASK_TO_RESTART 'r'
#define ASK_ABOUT_LINK 'i'
#define ASK_TO_PAINT 'w'
#define ASK_NOT_TO_PAINT 'n'

void setup() {
  Serial.begin();
  while (!Serial && millis() < SERIAL_WAITED_FOR_MS)
    delay(SERIAL_LOOKED_AT_EVERY_MS);
  Serial.println("zbitxre front panel");
  ui_init();
  waterfall_init();
  Serial.println("the screen is up");
  i2c_link_init();
}

void on_serial(int c) {
  if (c == ASK_ABOUT_LINK) {
    i2c_link_report();
    waterfall_report();
  }
  if (c == ASK_TO_PAINT)
    waterfall_set_painting(true);
  if (c == ASK_NOT_TO_PAINT)
    waterfall_set_painting(false);
  if (c == ASK_FOR_SCREEN)
    ui_screen_to_serial();
  if (c == ASK_TO_RESTART)
    rp2040.reboot();
}

void loop() {
  ui_slice();
  i2c_link_slice();
  waterfall_slice();
  if (Serial.available())
    on_serial(Serial.read());
}
