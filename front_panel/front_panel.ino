// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// front_panel.ino — the zbitx's front panel: a Raspberry Pi Pico W with a
// 480 by 320 touch screen and a knob.
//
// This is the panel for zbitxre. It comes from the sketch
// zbitx_front_panel_lvgl, and draws its controls with LVGL. The radio
// describes its controls to it over I2C, and the panel shows them, changes
// them and tunes the radio. It shows the spectrum the radio sends, and its
// waterfall.
//
// The files:
//
//   front_panel.ino   setup() and loop(), and nothing else
//   ui.cpp, ui.h      the display, the touch screen and the knob, given to LVGL
//   main.cpp          the main screen: where every control is, the knob's
//                     three states, and the tuning
//   controls.cpp      the controls the radio has described, and the panel's
//                     own
//   screens.cpp       the settings, and the screen a line of text is typed on
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
// The knob can be worked from the serial port, for trying the screen with
// no hand on the panel: '+' and '-' are a step of the knob each way, 'k' is
// a click and 'h' a long press. '@' and a name and a new line select what a
// touch would: @FREQ, @LIST, @SPECTRUM, a band, @40M, a macro's button,
// @F1, a control, @WPM, or the settings, @SET; and with the settings up, a
// row of them, @CALLSIGN, or @BACK. '=' and a text control's
// name, a space, the text and a new line do what typing the text on the
// panel and saving it would: =MYCALLSIGN VU2ESE.
//
// Painting the display is what puts the hash in the receiver. To find what
// quietens it, '2' and '4' drive the display's pins at 2 and 4 mA with slow
// edges, the Pico's own choice being 4, and '9' drives them at 12 mA with
// sharp edges. 'F', 'H' and 'Q' run the display's port at its full rate, at
// 12 MHz and at 6 MHz. 'a' paints the graph and all of the waterfall for
// each new line, 'b' the graph and 48 rows of it, 'c' the graph alone. What is
// chosen is shown in the corner of the frequency's box.
//
// What was heard: 2 mA with slow edges is some 3 dB quieter than 12 mA with
// sharp ones, so the panel starts with 2 mA. The port at 24 MHz is less
// irritating than at 6, because each burst of the hash is shorter, so the
// panel starts with the port at its full rate. waterfall.cpp says why the
// waterfall is as shallow as it is.
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
#include "ili9488.h"
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
#define DRIVE_2MA '2'
#define DRIVE_4MA '4'
#define DRIVE_12MA_SHARP '9'
#define PAINT_ALL_ROWS 'a'
#define PAINT_SOME_ROWS 'b'
#define PAINT_GRAPH_ONLY 'c'
#define SOME_ROWS 48
#define RATE_FULL 'F'
#define RATE_HALF 'H'
#define RATE_QUARTER 'Q'
#define RATE_FULL_HZ 37500000u
#define RATE_HALF_HZ 12000000u
#define RATE_QUARTER_HZ 6000000u
#define KNOB_TURNED_UP '+'
#define KNOB_TURNED_DOWN '-'
#define KNOB_CLICKED 'k'
#define KNOB_HELD 'h'
#define A_NOTE_FOLLOWS '#'
#define A_TOUCH_FOLLOWS '@'
#define A_TEXT_FOLLOWS '='
#define NOTE_MAX 24
#define NOTE_WAITED_FOR_MS 200

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

//a line that begins with '#' is a note: the rest of it, up to the end of
//the line, is shown in place of the frequency, as large. An empty note
//brings the frequency back
void take_a_note() {
  char text[NOTE_MAX];
  size_t count;

  Serial.setTimeout(NOTE_WAITED_FOR_MS);
  count = Serial.readBytesUntil('\n', text, sizeof(text) - 1);
  text[count] = 0;
  main_note_large(text);
}

void take_a_touch() {
  char text[NOTE_MAX];
  size_t count;

  Serial.setTimeout(NOTE_WAITED_FOR_MS);
  count = Serial.readBytesUntil('\n', text, sizeof(text) - 1);
  text[count] = 0;
  main_select(text);
}

void take_a_text() {
  char text[2 * NOTE_MAX];
  size_t count;

  Serial.setTimeout(NOTE_WAITED_FOR_MS);
  count = Serial.readBytesUntil('\n', text, sizeof(text) - 1);
  text[count] = 0;
  screens_set_text(text);
}

void on_serial(int c) {
  if (c == A_NOTE_FOLLOWS)
    take_a_note();
  if (c == A_TEXT_FOLLOWS)
    take_a_text();
  if (c == A_TOUCH_FOLLOWS)
    take_a_touch();
  if (c == ASK_ABOUT_LINK) {
    i2c_link_report();
    waterfall_report();
  }
  if (c == ASK_TO_PAINT) {
    waterfall_set_painting(true);
    main_note("");
  }
  if (c == ASK_NOT_TO_PAINT) {
    waterfall_set_painting(false);
    main_note("not painting");
  }
  if (c == DRIVE_2MA) {
    ili_set_drive(2, false);
    main_note("2 mA slow");
  }
  if (c == DRIVE_4MA) {
    ili_set_drive(4, false);
    main_note("4 mA slow");
  }
  if (c == DRIVE_12MA_SHARP) {
    ili_set_drive(12, true);
    main_note("12 mA sharp");
  }
  if (c == PAINT_ALL_ROWS) {
    waterfall_set_rows(WATERFALL_ALL_ROWS);
    main_note("all rows");
  }
  if (c == PAINT_SOME_ROWS) {
    waterfall_set_rows(SOME_ROWS);
    main_note("48 rows");
  }
  if (c == PAINT_GRAPH_ONLY) {
    waterfall_set_rows(0);
    main_note("graph only");
  }
  if (c == RATE_FULL) {
    ili_set_rate(RATE_FULL_HZ);
    main_note("24 MHz");
  }
  if (c == RATE_HALF) {
    ili_set_rate(RATE_HALF_HZ);
    main_note("12 MHz");
  }
  if (c == RATE_QUARTER) {
    ili_set_rate(RATE_QUARTER_HZ);
    main_note("6 MHz");
  }
  if (c == KNOB_TURNED_UP)
    main_knob_turned(1);
  if (c == KNOB_TURNED_DOWN)
    main_knob_turned(-1);
  if (c == KNOB_CLICKED)
    main_knob_clicked();
  if (c == KNOB_HELD)
    main_knob_held();
  if (c == ASK_FOR_SCREEN)
    ui_screen_to_serial();
  if (c == ASK_TO_RESTART)
    rp2040.reboot();
}

void loop() {
  ui_slice();
  i2c_link_slice();
  screens_slice();
  waterfall_slice();
  if (Serial.available())
    on_serial(Serial.read());
}
