// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// zbitx_gui.c — a window on the desktop that works the radio over the
// network.
//
//     zbitx_gui [radio] [port]
//     zbitx_gui remote <radio> [server] [port]
//
// The radio is sbitx.local unless another name or address is given, and the
// port 8081.
//
// With the word remote, the window reaches the owner's radio from anywhere,
// through my.sbitx.net; PROTOCOL.md, at the top of the repository, says how
// that works. The radio is named as the website names it, the owner's
// callsign and the radio's own name, vu2ese-home. The server is
// my.sbitx.net and the port 8081 unless others are given.
//
// REACHING A RADIO THROUGH MY.SBITX.NET
//
// The window has to show that it is the radio's owner asking. Each radio
// has a client password for that, twelve letters that the website made and
// shows beside the radio. The first time, the window asks for it, on the
// screen that any text is typed on, and keeps it in
// ~/.config/zbitx/<radio>.password, which only this user can read. The
// account's own password is typed into the website and never into this
// window.
//
// Then the window connects to the service and, before anything of the
// radio's, there are a few lines between the two:
//
//     CHALLENGE ...                the service
//     AUTH client vu2ese-home ...  the window, with a proof made with the
//                                  client password
//     WELCOME vu2ese-home ...      the service, with a proof of its own,
//                                  which the window checks: it will not go
//                                  on with something that does not have the
//                                  password too
//     CONNECTED vu2ese-home        the service, when the radio has answered
//
// and from there on it is the radio's own protocol, as if the window had
// connected to the radio itself, and everything below is the same. The
// password is never sent: it is the secret in the proof, stretched first,
// as PROTOCOL.md says, so that twelve letters cannot be found by trying
// them all. None of this is inside TLS, by the author's ruling; the proof
// is there to say who is on the line.
//
// A password the service refuses is forgotten and asked for again. If the
// radio is not on line, the window says so and tries again every three
// seconds, as it does for a radio that cannot be reached.
//
// THE WINDOW
//
// It is laid out as the zbitx's front panel is, place for place, so that
// whoever knows one knows the other; front_panel/main.cpp has the panel's
// plan, and the places here are its places, in its pixels: the panel is
// 480 by 320, and every widget is 48 high. The window's plan is 336 high,
// seven rows of 48 and no part of an eighth: at 320 the list, which is 48
// to a row, had four rows and two thirds of a fifth, and the column of
// buttons stopped 32 short of the foot. The 16 more are the waterfall's,
// and the bottom row is that much lower. The window opens maximised, to
// fill the radio's screen, and the plan is stretched to whatever size the
// window is, across and down each by its own measure, the letters sized to
// go with it; restored from maximised it is 800 by 480, the size zbitxv2's
// window opened at, and at any size everything keeps its place in the plan.
//
//   y 0     STEP | the meter | the frequency, 96 wide | RIT, SPLIT, DRIVE,
//           AGC, IF, VOL. On the panel the frequency is 144 wide and
//           there is no meter
//   y 48    a button for each band, 10M to 80M, and MODE
//   y 96    a column of buttons on the left, 48 wide: LOG, SET, TERM
//           beside it, 288 wide: the status line, 16 high, for what the
//           window and the radio have to say: the radio's remarks, such
//           as why a message could not be answered, and while an FT8
//           contact is under way how it stands, white on red, as on the
//           panel. In CW it has what the radio reads off the air, run
//           through from right to left as on the panel: each letter
//           comes in at the right end and what came before moves left
//           and off; a remark takes the line until the next letter
//           comes. It is the whole width of the graph; the power and
//           the SWR are laid over its right end only while there is
//           power to show; under it the graph, 64
//           high, and the waterfall, 112. The span is in the graph's top
//           left corner
//           on the right, 144 wide: the list, down to the foot of the
//           window, five rows
//   y 288   six places under the waterfall for the mode's own
//
// The top row is the frequency and the radio's core controls. Whatever
// else the radio has, that belongs to the radio as a whole or to the mode
// in use, is in the list, a row to each, which scrolls. The list is made
// from the radio's schema, so a control that a modem adds comes into it
// without this program knowing of it beforehand. BW is BW_VOICE in the
// voice modes and BW_CW in CW: the radio keeps one for each, as zbitxv2
// did. A modem's own controls are shown only in its modes: a control in
// the group CW in CW and CWR, one in VOICE in USB, LSB and AM.
//
// The frequency's box has two lines, as zbitxv2's window had, by its
// draw_dial(): a small one over a large one. The large line is the
// frequency in use and the small line the one in waiting, each with what
// it is before it:
//
//   as a rule            small  the other VFO, "B:7.050.000"
//                        large  the VFO in use, "A:7.100.000"; while the
//                               radio transmits, "TX:7.100.000"
//   RIT or SPLIT on      small  where the transmitter is to be, "TX:..."
//                        large  where the receiver listens, "RX:..."
//                        and the two change places while the radio
//                        transmits, so that the large line is always
//                        where the radio is working
//
// Each VFO has a dial of its own, and the one in waiting stays where it
// was left while the other is tuned. The radio keeps the two dials for
// its clients, as its controls VFO_A and VFO_B, which the front panel
// uses too: the window takes the waiting VFO's from there, and tells the
// radio both when the VFOs are changed over. A VFO that has never been
// used is put, once, where the radio was when the window first heard
// from it. The time is not in the box, where it was: the author moved it
// to the window's title bar, with the station's callsign, the radio's
// MYCALLSIGN, before it: "zbitx - VU2ESE - 06:49:25Z", and with no
// callsign set, "zbitx - 06:49:25Z". It is the time in UTC, to the
// second, with a Z after it, as on the panel, and "--:--:--Z"
// while the radio does not know the time. It is the radio's time and not
// this computer's: the window asks the radio the time once, on connecting,
// and counts the seconds on from the answer by its own clock; the radio's
// CLOCK changes as each minute begins, and the count is set right by that.
// RIT and SPLIT are red while they are on, as on the
// panel, where the author's reason is given: they are switched on and
// then forgotten.
//
// A band's button goes to that band. The button of the band in use goes
// to the next place in that band's stack, and from the last to the first.
// A tap on the graph or the waterfall goes to the next span, and from the
// narrowest back to the widest: 25K, 10K, 6K, 2.5K, the radio's control
// SPAN, which the panel shares. The radio sends a client the whole of what
// its receiver's bins cover, some 25 kHz, at their full resolution, and
// the window shows the middle of it that the span calls for, so a
// narrower span is the same spectrum drawn larger.
//
// On the bottom row, in USB, LSB and AM there are two buttons, TX and RX:
// TX starts transmitting and RX stops it, two buttons apart as the author
// wanted on the panel. MIC was on this row to their left; the author moved
// it to the list, where it is a row in these modes as any modem's control
// is in its own. TX is red while the radio transmits and RX
// has the bright edge while it receives. 2TONE has the same two. In CW the
// places are the first six of the radio's macros, each sent at a click,
// and the column's fourth button is ESC, which stops a macro, or anything
// else the radio is sending as text, where it is; the Escape key does the
// same there. In FT8 the row is empty, and the column's third and fourth
// buttons are CQ and HALT.
//
// Between STEP and the frequency is the meter, a place of its own on the
// top row: the signal's strength as the radio receives, the radio's
// SMETER, on a dial with a needle, as a radio's meter has. The author
// asked for a dial in place of the six boxes zbitxv2's window had, and
// then for it to be here and not over the graph's corner, the frequency's
// box giving up a third of its width for it. The scale is an arc, marked
// at S1, S3, S5, S7 and S9, which is seven tenths of the way across, and
// from there to 20 dB over S9 it is red. The dial is drawn to the size its
// place has, and the needle's pivot is at the foot of it or below. While
// the radio transmits
// the graph is not the spectrum, of which there is none, but the envelope
// of what is being sent, as an oscilloscope shows it and as zbitxv2 drew
// it: the radio sends it in the spectrum's place, a fifth of a second
// across, and the waterfall stays as it was. The dial is then the power,
// nought to a hundred watts, marked at 0, 50 and 100; under it are the
// power and the SWR in figures, "25W 1.3", and a bar that is the SWR, from
// nought to 4.5: green to 1.5, yellow to 3 and red above. The author asked for the two there; they were written at
// the right end of the status line, where they flickered through what the
// line was saying.
//
// In FT8 the graph and the waterfall are not shown. In their place, and
// down to the foot of the window, the left half is the list of the
// messages the radio has copied, coloured as the panel's is: the second
// each was sent at, how strong it was and its text, the newest at the
// bottom and kept in view. The right half is the graph and the waterfall,
// of FT8's passband alone, the dial at the left edge and 4 kHz above it at
// the right, with a red line where TX_PITCH puts our own signal, as the old
// sbitx drew it, so that a clear place can be seen; a tap there moves the
// pitch to it. The pitch of each message is left out of the list, which is
// half as wide as it was: where the stations are is seen beside it. The rows of one slot are told from those of the next by their
// ground, a little lighter for every other slot; a station calling CQ is
// in green; a message that has the station's own callsign in it, the
// radio's MYCALLSIGN, is in yellow, so that an answer stands out, which
// the author asked for; and what the radio itself sent is in red, with TX
// where the strength would be. A tap on a message marks it, and the list then stays
// where it is as more come, so that the marked one does not run off under
// the finger, and the status line says how it is answered: by a tap on it
// again, or by a press of the knob that works the selected control, as a
// click of the panel's knob does. Its line then goes back to the radio,
// which begins the contact. A mark not touched for thirty seconds is taken
// off, as on the panel. BW is not shown in FT8: its
// passband is the modem's own, 50 to 4000 Hz, and is not the voice modes'
// bandwidth.
//
// The two lists scroll, by the wheel, their bars, or a finger drawn up or
// down them. A row's tap is a press and a release in one place: a finger
// that moves is scrolling, and selects nothing.
//
// TX, the radio's own switch between receive and transmit, is on the
// bottom row as the two buttons in the modes that have them, and in the
// other modes it is a row of the list, so that there is always a way to
// put the radio on the air and take it off. FT8 has neither: the radio
// keys itself in the slot a message is due and the switch, worked by hand,
// sends nothing; a message is tapped twice, or CQ pressed, and HALT stops
// it. The frequency in red says the radio is sending.
//
// SET opens the settings, a screen of their own in the window, as on the
// panel: a row to each of the radio's controls in its group STATION, the
// callsign and the grid, and how FT8 is worked. A tap on a choice moves it
// to its next. A tap on a text opens the screen it is typed on. The arrow
// at the top left of each screen goes back. Without a callsign the radio
// cannot call CQ in FT8 or answer anyone.
//
// The last row of the settings is CONNECTIONS, which opens a screen like
// it: what the radio has to do with the world outside itself. The author
// put them there: to the owner it is one radio, and its link to
// my.sbitx.net is one of its settings, whichever program sees to it. The
// rows are the radio's controls in its group CONNECTIONS, each OFF or ON
// at a tap: REMOTE, whether the radio may be worked from afar through
// my.sbitx.net, and STREAMING, whether anyone may listen to it there;
// PROTOCOL.md says how both are done. Under them is a row that cannot be
// tapped, what the radio has to say of the link, its REMOTE_STATUS: the
// first time either is turned on it is the radio's cookie, twelve letters
// to be typed at my.sbitx.net when the radio is added to its owner's
// account there, and after that ONLINE and the name the radio was given.
// The screen is not offered when the window is itself reaching its radio
// through my.sbitx.net: these are set at the radio and nowhere else, the
// author's rule, a plain on and off. Both screens
// are shown afresh when any of them changes while they are on the screen.
//
// TYPING
//
// Wherever a text is to be typed, the window's own keyboard comes up, on a
// screen with the text in a box in its upper part and the keyboard in its
// lower part, as the author's rule for the panel has it, so that the
// keyboard never covers what is typed. The radio has no other keyboard to
// count on. It has only the keys a callsign, a grid or a short message
// needs: the figures, the capital letters, the stroke, the stop, the
// question mark and the dash, a space, and one to rub out. SAVE gives the
// text to the radio and CANCEL drops it. A keyboard plugged into the
// radio works too: what it types goes into the box, in capitals, Enter
// saves and Escape cancels.
//
// LOG opens the logbook, in a window of its own, logbook_window.c, which
// does what the panel's LOG screen does. While it is open the knob that
// works the selected control works its list instead. What the panel has
// and the window has not yet is the terminal behind TERM, which is shown
// dim and does nothing. The window has no MENU button.
//
// A control has its name over its value, in cyan, in the middle of its
// tile with a little room above it: the author found the names too small
// and too faint in grey at the tiles' corners. The frequency's two lines
// are both in the fixed-width letters, bold, in which the stops between
// the MHz, the kHz and the Hz are plain to see, the small line in cyan;
// the two are one column in the middle of the box and begin at the same
// place, the small line right over the large one and not in the box's
// corner. The time is in white. Nothing in the window is in small grey
// letters but the span in the graph's corner.
//
// A control is selected by a click on it, or a touch, and has a bright edge
// while it is. The mouse wheel over a control moves its value a step; over
// the frequency, the graph or the waterfall it tunes. A click on a choice
// that is already selected moves it to its next choice, and round from the
// last to the first, so RIT is switched with two clicks. On the keyboard, Tab
// and Shift-Tab move the selection, the up and down keys move what is
// selected, and Escape goes back to the frequency.
//
// THE KEYBOARD FOR CW
//
// In CW the third button of the column, TERM elsewhere, is KBD. It brings
// up the same screen and the same keys that texts are typed on, to send
// with: each key touched is sent to the radio as it is touched, CW_TEXT,
// and is added to the box at the top, which is only a record of what was
// sent. SPACE sends a space. Rubbing out takes the last character out of
// the box and does not call it back from the radio. Where SAVE is for a
// text there is ESC, which stops what the radio is still sending and
// empties the box, and where CANCEL is, DONE, which goes back to the
// radio's own screen. A key touched while a macro is being sent stops the
// macro, which is the radio's doing. The author asked for it: the radio's
// own screen is touched, and a keyboard is not always plugged in.
//
// In CW the keyboard also sends: every letter, figure and sign struck is
// sent to the radio as it is struck, CW_TEXT, as zbitxv2 sent what was
// typed, and the space bar and Enter send a space. A key struck while a
// macro is being sent stops the macro, which is the radio's doing, and
// Escape stops whatever is being sent.
//
// THE TUNING WHEEL
//
// A tap on the frequency brings up a box with a wheel in it, for tuning by
// touch, as the author asked. The box lies over the list of controls on the
// right, which is as wide as it is: it was first dropped under the
// frequency, where it hid a large part of the spectrum, and the spectrum is
// what is watched while tuning. A finger put on the wheel and taken
// round it turns it, clockwise up and the other way down, a step of STEP
// for every ten degrees, thirty-six to the turn, so that a turn with the
// step at 100 Hz is 3.6 kHz. The wheel has a dimple that goes round with
// it, so that it is seen to turn. It is the angle the finger goes through
// that counts, not where it is put down. The hub in the wheel's middle does
// not turn and has a cross on it: a tap there, where the author wanted it
// and not in the box's corner, or another tap on the frequency, puts the
// box away. The tap used to go to the other VFO; the author set that aside
// for the time being, and VFO in the list still does it.
//
// TUNING
//
// The radio knows only FREQ, the frequency it is tuned to. STEP, VFO and
// OFFSET are the window's own, as STEP, VFO, RIT and SPLIT were zbitxv2's
// window's, and every client keeps its own: the front panel's knob is on
// the panel, and tunes it as the wheel tunes this.
//
// VFO and OFFSET are beside the frequency on the top row, since they are
// what the frequency's box shows. RIT and SPLIT were two switches there,
// and VFO a row far down the list. The author made one control of the
// two: they are never wanted together, a split being a RIT with a very
// wide range, and that left a place on the top row for VFO.
//
//   STEP     how far a turn of the wheel tunes: 10H, 100H, 500H, 1K or
//            10K. The dial moves by STEP, onto a multiple of it.
//   VFO      A or B, each with a dial of its own. Choosing the other keeps
//            the dial of the one left, and goes to the other's; one not
//            yet used opens where the other is.
//   OFFSET   how far the receiver is from the transmitter: NORM, RIT or
//            SPLIT.
//            NORM   both are on the dial.
//            RIT    the wheel moves the receiver's offset, RIT_DELTA, by
//                   STEP, up to 25 kHz either way, and the dial stays; the
//                   receiver listens at the dial + RIT_DELTA, and the
//                   transmitter is to be on the dial. Leaving RIT puts
//                   the offset back to nothing.
//            SPLIT  the transmitter is to be on the other VFO's dial.
//                   Choosing it goes to VFO A, so that it receives on A
//                   and transmits on B.
//
// The window sends the radio, as FREQ, where the receiver listens. The
// large figures are that frequency, and where the transmitter is to be is
// beside them when it is elsewhere, as it is with RIT or SPLIT on. With
// RIT or SPLIT on, FREQ is to change at each switch between receive and
// transmit, to where the transmitter is to be and back: the radio does not
// say yet when it switches, since it has no transmitter yet.
//
// When the radio tells a new FREQ, from a band chosen or the front panel,
// that is where the receiver listens, and the dial is worked back from it.
//
// THE KNOBS
//
// The sbitx has two knobs on its front, wired to the Pi it runs this window
// on, and the window reads them, as zbitxv2's window did. One tunes and
// does nothing else: a notch is a turn of the wheel on the frequency,
// whatever is selected. The other changes the value of the control that is
// selected, the row that has the focus, a notch for a step; with no row
// selected it works VOL. A press of its button answers the FT8 message
// that is marked, if one is; with none marked it selects the next control
// down the list, so the radio can be worked with no touch of the screen.
// The tuning knob's button does nothing yet.
//
// The knobs are the window's alone. The radio program, which runs with no
// screen, knows nothing of them and has none of this in it. All the window
// takes from the radio's side is the radio's name, to know whether it is on
// one that has knobs: it reads data/hw_settings.ini in the folder it is
// started in, the file the radio program reads. On a desktop there is no
// such file and no knobs are looked for. A zbitx has none on the Pi: its
// one knob is on the front panel.
//
// The sbitx's, from sbitx_gtk.c, whose numbers for the pins are wiringPi's;
// here they are the Pi's own. A knob's two contacts are in the order
// sbitx_gtk.c gave them to its enc_init(), which is what makes clockwise
// count upward.
//
//     pin  header  name in sbitx_gtk.c
//      17    11    ENC2_A            the tuning knob
//      27    13    ENC2_B
//      22    15    ENC2_SW           its button
//      10    19    ENC1_B            the knob for the selected control
//       9    21    ENC1_A
//      11    23    ENC1_SW           its button
//
// A knob has to be looked at more often than a window is woken, or notches
// are lost when it is spun, so a thread of its own looks at both, twice a
// millisecond, and counts; the window takes the counts up to act on them
// every few milliseconds. knob.c says how a knob is read.
//
// THE RADIO
//
// It speaks the radio's control protocol, which server.c on the radio
// describes: lines of a name and a value. On connecting it asks for the
// schema, to learn each control's range and step, and for the spectrum. It
// sends a line when a control is moved, and shows the change at once, since
// the radio does not tell a client of its own changes. It shows what the
// radio tells it of everyone else's, so the window follows the radio when
// the front panel is turned.
//
// Showing what the radio says never sends anything back: that is how a
// change goes round in a loop.
//
// If the radio cannot be reached, or goes, the window says so and tries
// again every three seconds.
//
// It is built on the desktop with "make gui", which needs GTK 3.

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <pthread.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/random.h>

#include "board.h"
#include "gpio.h"
#include "knob.h"
#include "logbook_window.h"
#include "sha256.h"

#define DEFAULT_RADIO "sbitx.local"
#define DEFAULT_PORT 8081
#define REMOTE_WORD "remote"
#define DEFAULT_SERVER "my.sbitx.net"
#define NONCE_BYTES 16
#define CHALLENGE_ROOM 72
#define KEY_FOLDER_MODE 0700
#define KEY_FILE_MODE 0600
#define PATH_ROOM 512
#define PASSWORD_ROOM 32
#define STRETCH_ROUNDS 50000
#define SALT_BEGINS "zbitx-1 "
#define RETRY_EVERY_MS 3000
#define HZ_PER_MHZ 1000000
#define HZ_PER_KHZ 1000
#define LINE_MAX_LENGTH 128
#define TEXT_MAX 64
#define GAP 6

// the panel's plan, in the panel's pixels, and seven whole rows high
#define PANEL_WIDTH 480
#define PANEL_HEIGHT 336
#define ROW_HEIGHT 48
#define TILE_WIDTH 48
#define DIAL_LEFT 48
#define FREQ_LEFT 96
#define FREQ_WIDTH 96
#define WHEEL_LEFT LIST_LEFT
#define WHEEL_TOP MIDDLE_TOP
#define WHEEL_SIZE 144
#define WHEEL_MARGIN 8
#define WHEEL_HUB_SHARE 0.32
#define WHEEL_DIMPLE_AT 0.68
#define WHEEL_DIMPLE_SHARE 0.16
#define WHEEL_TICKS 36
#define WHEEL_TICK_SHARE 0.08
#define WHEEL_NOTCH (2.0 * G_PI / WHEEL_TICKS)
#define WHEEL_CLOSE_ARM 7
#define BANDS_TOP 48
#define MIDDLE_TOP 96
#define COLUMN_BUTTONS 4
#define CW_KBD_ROW 2
#define FT8_CQ_ROW 2
#define FT8_HALT_ROW 3
#define GRAPH_LEFT 48
#define GRAPH_WIDTH 288
#define STATUS_HEIGHT 16
#define GRAPH_HEIGHT 64
#define FALL_HEIGHT 112
#define METER_WIDTH 112
#define LIST_LEFT 336
#define LIST_WIDTH 144
#define LIST_HEIGHT (PANEL_HEIGHT - MIDDLE_TOP)
#define FOOTER_TOP 288
#define FOOTER_TILES 6
#define BANDS 9
#define ROW_MARGIN_SIDE 6
#define TITLE_SIZE 11
#define CORNER_SIZE 9
#define WAITING_SIZE 12
#define TITLE_ROOM_ABOVE 4
#define VALUE_SIZE 13
#define FREQUENCY_SIZE 17
// the longest the large line is, "TX:21.074.000", and how wide a letter of
// the frequency's face is beside its height, with a little over
#define FREQUENCY_LETTERS 13
#define LETTER_WIDTH_SHARE 0.63
#define STATUS_SIZE 10
#define SPAN_LEFT 3
#define SPAN_BASELINE 11

// the size zbitxv2's window opened at
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 480
#define PLACED_MAX 128
#define CSS_MAX 2048
#define MESSAGES_SHOWN_MAX 200
#define MESSAGE_FIELDS 4
#define MESSAGE_SIZE 8
#define MESSAGES_WIDTH (GRAPH_WIDTH / 2)
#define MARK_KEPT_FOR_S 30
#define SLOT_SECONDS 15
#define SECONDS_A_MINUTE 60
#define TIME_DIGITS 6
#define TIME_MINUTE_AT 2
#define TIME_SECOND_AT 4
#define DIGITS_OF_TWO 100
#define CLOCK_SHOWN_EVERY_MS 250
#define CW_READ_MAX 80
#define SECONDS_AN_HOUR 3600
#define SECONDS_A_DAY 86400
#define HALF_A_MINUTE 30
#define MICROS_A_SECOND 1000000
#define TENTHS 10

#define KNOWN_MAX 128
#define CONTROL_NAME_MAX 24
#define CONTROL_VALUE_MAX 32
#define CHOICES_MAX 128
#define NOT_FOUND (-1)
#define ON_FREQUENCY (-1)

#define POINTS_MAX 1024
#define FALL_ROWS 256
#define FULL_SCALE_DB 80
#define MODULATION_FULL_SCALE 255.0
// the S meter's dial, in the panel's pixels, and what the radio's SMETER
// is made of: the S units times a hundred, and the dB over
#define SMETER_ARC_SHARE 0.8
#define SMETER_ARC_TOP 0.36
#define POWER_DIAL_WATTS 100.0
#define POWER_MARK_EDGE 2
#define POWER_WHOLE_FROM 100
#define METER_FOOT_TOP 0.67
#define METER_FIGURES_AT 0.78
#define METER_FIGURES_SIZE 0.2
#define SWR_BAR_HEIGHT 0.07
#define SWR_BAR_MARGIN 0.1
#define SWR_BAR_TOP 0.89
#define SWR_BAR_FULL 4.5
#define SWR_ZONES 3
#define SWR_DIM 0.28
#define SMETER_SWEEP (48.0 * G_PI / 180.0)
#define SMETER_TICK 0.09
#define SMETER_MARK_ROOM 0.06
#define SMETER_MARK_SIZE 0.17
#define SMETER_NEEDLE_FROM 0.3
#define SMETER_S9_AT 0.7
#define SMETER_OVER_MOST_DB 20
#define SMETER_PER_UNIT 100
#define S_UNITS_MOST 9
#define DB_PER_S_UNIT 6.0
#define SHADES_PER_DB 2
#define SHADES_A_BAND 32
#define SHADE_STEP 8
#define COLOUR_MAX 255
#define GRAPH_SHARE 3
#define VOICE_LOW_HZ 300
#define FT8_LOW_HZ 50
#define FT8_HIGH_HZ 4000
#define PITCH_DASH 3.0

#define HW_SETTINGS_FILE "data/hw_settings.ini"
#define KNOBS_LOOKED_AT_EVERY_US 500
#define KNOBS_TAKEN_UP_EVERY_MS 10
#define BUTTON_DOWN_LOOKS 40
#define VOLUME_CONTROL "AUDIO"

#define LOWEST_HZ 500000
#define HIGHEST_HZ 30000000
#define RIT_MAX_HZ 25000
#define HZ_PER_K 1000
#define VFO_A 0
#define VFO_B 1
#define VFOS 2

// a control: one the radio has told of, its value, and from the schema its
// kind and range; or one of the window's own, which it never sends
struct known {
	char name[CONTROL_NAME_MAX];
	char group[CONTROL_NAME_MAX];
	char value[CONTROL_VALUE_MAX];
	int is_own;
	int is_choice;
	int is_text;
	int is_listed;
	int min;
	int max;
	int step;
	char choices[CHOICES_MAX];
};

// a row of the list: the control it works, or BW, which works BW_VOICE or
// BW_CW with the mode
// where a row is put: at a place of its own in the top two rows, or in the
// list
enum place {
	ON_TOP,
	IN_LIST,
};

// a control that has a place of its own, as the panel has it
struct top_place {
	const char *control;
	int x;
	int y;
};

// what a mode has on the bottom row besides its controls: a button that
// puts a control at a choice
struct footer_button {
	const char *group;
	int slot;
	const char *label;
	const char *control;
	const char *choice;
};

struct row {
	char control[CONTROL_NAME_MAX];
	enum place place;
	GtkWidget *box;
	GtkWidget *value_label;
};

static const struct top_place top_places[] = {
	{"STEP", 0, 0},
	{"VFO", 192, 0},
	{"OFFSET", 240, 0},
	{"DRIVE", 288, 0},
	{"AGC", 336, 0},
	{"IF", 384, 0},
	{"AUDIO", 432, 0},
	{"MODE", 432, BANDS_TOP},
	{NULL, 0, 0},
};

static const struct footer_button footer_buttons[] = {
	{"VOICE", 1, "TX", "TX", "ON"},
	{"VOICE", 2, "RX", "TX", "OFF"},
	{"2TONE", 0, "TX", "TX", "ON"},
	{"2TONE", 1, "RX", "TX", "OFF"},
	{"RADE2", 0, "TX", "TX", "ON"},
	{"RADE2", 1, "RX", "TX", "OFF"},
	{NULL, 0, NULL, NULL, NULL},
};

static const char *const band_names[BANDS] = {
	"10M", "12M", "15M", "17M", "20M", "30M", "40M", "60M", "80M",
};

static const char *const column_names[COLUMN_BUTTONS] = { "LOG", "SET", "TERM", "" };
#define MACRO_GROUP "CW"
#define MACRO_CONTROL "MACRO"
#define MESSAGES_GROUP "FT8"
#define FT8_DO_CONTROL "FT8_DO"
#define CW_DO_CONTROL "CW_DO"
#define CW_TEXT_CONTROL "CW_TEXT"
// a space on its own would be lost on the way; the radio takes this for one
#define CW_TEXT_SPACE "_"
// how much of what was sent the keyboard's box keeps in view
#define CW_SENT_SHOWN 40
#define CW_ESC_ROW 3
#define TX_PITCH_CONTROL "TX_PITCH"
#define LOG_ROW 0
#define SET_ROW 1
#define STATION_GROUP "STATION"
#define REMOTE_GROUP "REMOTE"
#define CONNECTIONS_GROUP "CONNECTIONS"
#define PAGE_CONNECTIONS "connections"
#define COOKIE_WORD "COOKIE "
#define ACCOUNT_SITE "my.sbitx.net"
#define CALLSIGN_CONTROL "MYCALLSIGN"
#define WINDOW_NAME "zbitx"
#define TITLE_MAX 160
#define PAGE_MAIN "main"
#define PAGE_SETTINGS "settings"
#define PAGE_TEXT "text"
#define KEY_ROWS 4
#define KEYS_A_ROW 10
#define CANCEL_KEYS 3
#define SPACE_KEYS 3
#define RUB_OUT_KEYS 2
#define SAVE_KEYS 2
#define TYPED_SIZE 16

static const char *radio_name = DEFAULT_RADIO;
static int radio_port = DEFAULT_PORT;

static GSocketClient *connector;
static GSocketConnection *connection;
static GDataInputStream *reader;
static GOutputStream *writer;

// what has been put on the screen, and where in the panel's plan, so that
// it can be put again when the window's size changes
struct placed {
	GtkWidget *widget;
	int x;
	int y;
	int width;
	int height;
};

static struct placed placed[PLACED_MAX];
static double scale_across = (double)WINDOW_WIDTH / PANEL_WIDTH;
static double scale_down = (double)WINDOW_HEIGHT / PANEL_HEIGHT;
static double scale = (double)WINDOW_HEIGHT / PANEL_HEIGHT;
static int laid_out_width;
static int laid_out_height;
static int is_lay_out_due;
static GtkCssProvider *css;
static GtkWidget *screen;
static GtkWidget *frequency_box;
static GtkWidget *wheel_area;
// how far round the wheel has been turned, where the finger last was on it,
// and what has been turned since the last step was taken
static double wheel_angle;
static double wheel_finger_angle;
static double wheel_owed;
static double wheel_began_x;
static double wheel_began_y;
static int is_wheel_held;
static GtkWidget *frequency_label;
static GtkWidget *vfo_label;
static GtkWidget *spectrum_area;
static GtkWidget *status_label;
static GtkWidget *meter_label;
static GtkWidget *list;
static GtkWidget *list_scroller;
static GtkWidget *messages;
static GtkWidget *messages_scroller;
static GtkWidget *marked_message;
static GtkWidget *pages;
static GtkWidget *settings_list;
static GtkWidget *connections_list;
static GtkWidget *text_title;
static GtkWidget *typed_label;
// the newest of what the radio has read of CW, the latest at the end
static char cw_read[CW_READ_MAX + 1];
static struct known *typed_for;
// how far the radio's time is ahead of this computer's steady clock, in
// seconds, once the radio has said the time
static gint64 radio_ahead_s;
static int is_time_known;
static char typed[CONTROL_VALUE_MAX];
static guint mark_timer;
static int messages_shown;
static GtkWidget *band_box[BANDS];
static GtkWidget *column_box[COLUMN_BUTTONS];
static GtkWidget *column_label[COLUMN_BUTTONS];
static GtkWidget *footer_box[FOOTER_TILES];
static GtkWidget *footer_label[FOOTER_TILES];

static struct known known[KNOWN_MAX];
static int known_count;
static struct row rows[KNOWN_MAX];
static int row_count;
static int focus = ON_FREQUENCY;

static int dial_hz;
static int vfo_dial_hz[VFOS];
static int vfo_in_use = VFO_A;

static int span_hz;
static int point_count;
static uint8_t points[POINTS_MAX];
static GtkWidget *dial_area;
static int is_modulation_shown;
static int modulation_count;
static uint8_t modulation[POINTS_MAX];
static cairo_surface_t *fall;

// a radio's two knobs: each one's two contacts and its button
struct knob_pins {
	int a_pin;
	int b_pin;
	int button_pin;
};

struct radio_knobs {
	const char *radio;
	struct knob_pins tuning;
	struct knob_pins control;
};

static const struct radio_knobs radios_with_knobs[] = {
	{
		.radio = "sbitxv3",
		.tuning = { 17, 27, 22 },
		.control = { 10, 9, 11 },
	},
	{ .radio = NULL },
};

static GtkWidget *main_window;
static guint retry_timer;
static int is_connecting;

// reaching a radio through my.sbitx.net: how far the few lines before the
// radio's own have got
enum through {
	NOT_THROUGH,
	TO_BE_CHALLENGED,
	TO_BE_WELCOMED,
	TO_BE_CONNECTED,
	THROUGH,
};

static int is_remote;
static enum through through = NOT_THROUGH;
static const char *through_wanted = "";
static char through_challenge[CHALLENGE_ROOM];
static char through_nonce[2 * NONCE_BYTES + 1];
static char through_key[SHA256_HEX_ROOM];
static int has_through_key;
static struct known password_asked;
// in the place of a control to type a text for: the keyboard is sending CW
static struct known cw_sending;
static GtkWidget *cancel_key;
static GtkWidget *save_key;
static struct knob tuning_knob;
static struct knob control_knob;
static int control_button_pin;
static atomic_int tuning_notches;
static atomic_int control_notches;
static atomic_int control_presses;

static void try_to_connect(void);
static void make_list(void);
static void add_label_class(GtkWidget *label, const char *name);
static void set_class(GtkWidget *widget, const char *name, int on);
static void tell_taps(GtkWidget *widget, GCallback tapped, gpointer data);
static GtkWidget *list_row_new(const char *name, GtkWidget **value);

// a remark on the status line, from its left end
static void show_status(const char *text) {
	set_class(status_label, "warning", 0);
	gtk_label_set_xalign(GTK_LABEL(status_label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_END);
	gtk_label_set_text(GTK_LABEL(status_label), text);
}

// something the radio will not do until the operator puts it right: in bold,
// in a colour of its own, until the next remark
static void show_warning(const char *text) {
	show_status(text);
	set_class(status_label, "qso", 0);
	set_class(status_label, "warning", 1);
}

// CW read off the air: the characters join the end of what was read
// before, the oldest go to make room, and the line shows the end of it,
// against its right end
static void take_cw_line(const char *text) {
	size_t had = strlen(cw_read);
	size_t more = strlen(text);

	if (more > CW_READ_MAX) {
		text += more - CW_READ_MAX;
		more = CW_READ_MAX;
	}
	if (had + more > CW_READ_MAX) {
		memmove(cw_read, cw_read + had + more - CW_READ_MAX, CW_READ_MAX - more);
		had = CW_READ_MAX - more;
	}
	memcpy(cw_read + had, text, more + 1);
	gtk_label_set_xalign(GTK_LABEL(status_label), 1);
	gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_START);
	gtk_label_set_text(GTK_LABEL(status_label), cw_read);
}

static struct known *find_known(const char *name) {
	int i;

	for (i = 0; i < known_count; i++) {
		if (!strcmp(known[i].name, name))
			return &known[i];
	}
	if (known_count == KNOWN_MAX)
		return NULL;
	memset(&known[known_count], 0, sizeof(known[0]));
	snprintf(known[known_count].name, CONTROL_NAME_MAX, "%s", name);
	return &known[known_count++];
}

static const char *value_of(const char *name) {
	struct known *k = find_known(name);

	if (k == NULL)
		return "";
	return k->value;
}

static int is_cw(void) {
	const char *mode = value_of("MODE");

	return !strcmp(mode, "CW") || !strcmp(mode, "CWR");
}

static const char *row_control(const struct row *r) {
	if (strcmp(r->control, "BW"))
		return r->control;
	if (is_cw())
		return "BW_CW";
	return "BW_VOICE";
}

static const char *label_of(const char *control) {
	if (!strcmp(control, "AUDIO"))
		return "VOL";
	return control;
}

// the group of the modem that has the mode in use: CW for CW and CWR,
// VOICE for the voice modes, and the mode's own name for any other
static const char *mode_group(void) {
	const char *mode = value_of("MODE");

	if (is_cw())
		return "CW";
	if (!strcmp(mode, "USB") || !strcmp(mode, "LSB") || !strcmp(mode, "AM"))
		return "VOICE";
	return mode;
}

// whether the mode in use has a button on the bottom row that works the
// control
static int has_footer_button(const char *control) {
	const struct footer_button *b;

	for (b = footer_buttons; b->group != NULL; b++) {
		if (!strcmp(b->control, control) && !strcmp(b->group, mode_group()))
			return 1;
	}
	return 0;
}

static int is_row_shown(const struct row *r) {
	struct known *k = find_known(row_control(r));

	if (k == NULL)
		return 0;
	if (r->place == IN_LIST && has_footer_button(r->control))
		return 0;
	if (!strcmp(r->control, "BW") || !strcmp(r->control, "TX"))
		return strcmp(mode_group(), MESSAGES_GROUP) != 0;
	if (!strcmp(k->group, "RADIO") || r->place == ON_TOP)
		return 1;
	// RADE2 is spoken into, and has the voice modes' microphone gain
	if (!strcmp(mode_group(), "RADE2") && !strcmp(r->control, "MIC"))
		return 1;
	return !strcmp(k->group, mode_group());
}

static int is_on(const char *name) {
	return !strcmp(value_of(name), "ON");
}

// whether OFFSET is at this one of NORM, RIT and SPLIT
static int is_offset(const char *kind) {
	return !strcmp(value_of("OFFSET"), kind);
}

// where the receiver listens: the dial, moved by RIT_DELTA while RIT is on
static int receiver_hz(void) {
	if (is_offset("RIT"))
		return dial_hz + atoi(value_of("RIT_DELTA"));
	return dial_hz;
}

// where the transmitter is to be: the dial, or while SPLIT is on the other
// VFO's dial, if that VFO has been used
static int transmitter_hz(void) {
	int other = VFO_B;

	if (!is_offset("SPLIT"))
		return dial_hz;
	if (vfo_in_use == VFO_B)
		other = VFO_A;
	if (vfo_dial_hz[other] == 0)
		return dial_hz;
	return vfo_dial_hz[other];
}

static void format_frequency(char *text, int room, int hz) {
	snprintf(text, room, "%d.%03d.%03d", hz / HZ_PER_MHZ,
		(hz % HZ_PER_MHZ) / HZ_PER_KHZ, hz % HZ_PER_KHZ);
}

// the panel's pixels in the window's: across, down, and for what has no
// direction, the letters and the margins, by the smaller of the two
static int px(double panel_pixels) {
	return (int)(panel_pixels * scale + 0.5);
}

static int px_across(double panel_pixels) {
	return (int)(panel_pixels * scale_across + 0.5);
}

static int px_down(double panel_pixels) {
	return (int)(panel_pixels * scale_down + 0.5);
}

static void set_class(GtkWidget *widget, const char *name, int is_wanted) {
	GtkStyleContext *style = gtk_widget_get_style_context(widget);

	if (is_wanted)
		gtk_style_context_add_class(style, name);
	else
		gtk_style_context_remove_class(style, name);
}

// the nth of a control's choices, from 0, "USB/LSB/CW"; 0 if there are not
// so many
static int choice_at(const char *choices, int n, char *choice, int room) {
	char list[CHOICES_MAX];
	char *name;

	snprintf(list, sizeof(list), "%s", choices);
	for (name = strtok(list, "/"); name != NULL; name = strtok(NULL, "/")) {
		if (n == 0) {
			snprintf(choice, room, "%s", name);
			return 1;
		}
		n--;
	}
	return 0;
}

static const char *const vfo_dial_names[VFOS] = { "VFO_A", "VFO_B" };

static int vfo_in_waiting(void) {
	if (vfo_in_use == VFO_B)
		return VFO_A;
	return VFO_B;
}

// one line of the frequency's box: what the frequency is, and the frequency
static void format_dial_line(char *text, int room, const char *what, int hz) {
	char figures[TEXT_MAX / 2];

	format_frequency(figures, sizeof(figures), hz);
	snprintf(text, room, "%.2s:%s", what, figures);
}

// the frequency's two lines, as zbitxv2's draw_dial() had them: the large
// one is where the radio is working and the small one what is in waiting
static void show_frequency(void) {
	int receive_hz = receiver_hz();
	int transmit_hz = transmitter_hz();
	int is_transmitting = is_on("TX");
	int other = vfo_in_waiting();
	const char *other_name = "B";
	char small_line[TEXT_MAX];
	char large_line[TEXT_MAX];

	if (other == VFO_A)
		other_name = "A";
	if (receive_hz != transmit_hz && is_transmitting) {
		format_dial_line(small_line, sizeof(small_line), "RX", receive_hz);
		format_dial_line(large_line, sizeof(large_line), "TX", transmit_hz);
	}
	else if (receive_hz != transmit_hz) {
		format_dial_line(small_line, sizeof(small_line), "TX", transmit_hz);
		format_dial_line(large_line, sizeof(large_line), "RX", receive_hz);
	}
	else {
		format_dial_line(small_line, sizeof(small_line), other_name, dial_hz);
		if (vfo_dial_hz[other] != 0)
			format_dial_line(small_line, sizeof(small_line), other_name,
				vfo_dial_hz[other]);
		format_dial_line(large_line, sizeof(large_line), value_of("VFO"), dial_hz);
		if (is_transmitting)
			format_dial_line(large_line, sizeof(large_line), "TX", dial_hz);
	}
	gtk_label_set_text(GTK_LABEL(vfo_label), small_line);
	gtk_label_set_text(GTK_LABEL(frequency_label), large_line);
	set_class(frequency_box, "focused", focus == ON_FREQUENCY);
}

static gint64 steady_s(void) {
	return g_get_monotonic_time() / MICROS_A_SECOND;
}

// the window's title: its name, the station's callsign if one is set, and
// the time in UTC, to the second
static gboolean show_clock(gpointer unused) {
	gint64 now = steady_s() + radio_ahead_s;
	int second_of_day = (int)(now % SECONDS_A_DAY);
	const char *callsign = value_of(CALLSIGN_CONTROL);
	char time[TEXT_MAX];
	char title[TITLE_MAX];

	(void)unused;
	snprintf(time, sizeof(time), "--:--:--Z");
	if (is_time_known)
		snprintf(time, sizeof(time), "%02d:%02d:%02dZ", second_of_day / SECONDS_AN_HOUR,
			(second_of_day % SECONDS_AN_HOUR) / SECONDS_A_MINUTE,
			second_of_day % SECONDS_A_MINUTE);
	if (callsign[0] != 0)
		snprintf(title, sizeof(title), "%s - %s - %s", WINDOW_NAME, callsign, time);
	else
		snprintf(title, sizeof(title), "%s - %s", WINDOW_NAME, time);
	gtk_window_set_title(GTK_WINDOW(main_window), title);
	return G_SOURCE_CONTINUE;
}

// TIME 1791397105: the radio's time, or 0 if it does not know it
static void take_time_line(const char *value) {
	gint64 radio_s = g_ascii_strtoll(value, NULL, 10);

	is_time_known = radio_s > 0;
	radio_ahead_s = radio_s - steady_s();
}

// the radio's CLOCK has changed, which it does as a minute begins: the
// count of seconds is moved to the nearest minute
static void take_start_of_minute(void) {
	gint64 now = steady_s() + radio_ahead_s;
	gint64 minute = ((now + HALF_A_MINUTE) / SECONDS_A_MINUTE) * SECONDS_A_MINUTE;

	if (is_time_known)
		radio_ahead_s += minute - now;
}

// what is at a place on the bottom row in the mode in use: the button's
// label and what a click on it sends, or nothing
static int footer_button_at(int slot, char *label, char *control, char *choice, int room) {
	const struct footer_button *b;
	struct known *macros = find_known(MACRO_CONTROL);
	const char *group = mode_group();

	for (b = footer_buttons; b->group != NULL; b++) {
		if (b->slot == slot && !strcmp(b->group, group)) {
			snprintf(label, room, "%s", b->label);
			snprintf(control, room, "%s", b->control);
			snprintf(choice, room, "%s", b->choice);
			return 1;
		}
	}
	if (strcmp(group, MACRO_GROUP) || macros == NULL)
		return 0;
	if (!choice_at(macros->choices, slot, choice, room))
		return 0;
	snprintf(label, room, "%s", choice);
	snprintf(control, room, "%s", MACRO_CONTROL);
	return 1;
}

static void show_buttons(void) {
	char label[CONTROL_VALUE_MAX];
	char control[CONTROL_VALUE_MAX];
	char choice[CONTROL_VALUE_MAX];
	int is_ft8 = !strcmp(mode_group(), MESSAGES_GROUP);
	int is_a_button;
	int is_in_force;
	int i;

	for (i = 0; i < BANDS; i++)
		set_class(band_box[i], "in-use", !strcmp(value_of("BAND"), band_names[i]));
	for (i = 0; i < FOOTER_TILES; i++) {
		is_a_button = footer_button_at(i, label, control, choice, sizeof(label));
		gtk_widget_set_visible(footer_box[i], is_a_button);
		if (!is_a_button)
			continue;
		gtk_label_set_text(GTK_LABEL(footer_label[i]), label);
		is_in_force = !strcmp(control, "TX") && !strcmp(value_of("TX"), choice);
		set_class(footer_box[i], "on", is_in_force && !strcmp(choice, "ON"));
		set_class(footer_box[i], "focused", is_in_force && !strcmp(choice, "OFF"));
	}
	for (i = 0; i < COLUMN_BUTTONS; i++) {
		gtk_label_set_text(GTK_LABEL(column_label[i]), column_names[i]);
		set_class(column_box[i], "dim", i != SET_ROW && i != LOG_ROW);
		gtk_widget_set_visible(column_box[i], column_names[i][0] != 0);
	}
	if (!strcmp(mode_group(), MACRO_GROUP)) {
		gtk_label_set_text(GTK_LABEL(column_label[CW_KBD_ROW]), "KBD");
		set_class(column_box[CW_KBD_ROW], "dim", 0);
		gtk_label_set_text(GTK_LABEL(column_label[CW_ESC_ROW]), "ESC");
		set_class(column_box[CW_ESC_ROW], "dim", 0);
		gtk_widget_set_visible(column_box[CW_ESC_ROW], TRUE);
	}
	if (!is_ft8)
		return;
	gtk_label_set_text(GTK_LABEL(column_label[FT8_CQ_ROW]), "CQ");
	gtk_label_set_text(GTK_LABEL(column_label[FT8_HALT_ROW]), "HALT");
	set_class(column_box[FT8_CQ_ROW], "dim", 0);
	set_class(column_box[FT8_HALT_ROW], "dim", 0);
	gtk_widget_set_visible(column_box[FT8_HALT_ROW], TRUE);
}

// the power and the SWR are on the dial, which is drawn again; the status
// line's own place for them is no longer used
static void show_meter(void) {
	gtk_widget_set_visible(meter_label, FALSE);
	gtk_widget_queue_draw(dial_area);
}

static void put_again(GtkWidget *widget, int x, int y, int width, int height);

// in FT8 the list of messages has the left half of where the graph and the
// waterfall are, and they have the right half, down to the foot of the
// window as the list is; in the other modes they have it all
static void place_graph(void) {
	static int was_beside_messages = -1;
	int is_beside_messages = !strcmp(mode_group(), MESSAGES_GROUP);

	if (is_beside_messages == was_beside_messages)
		return;
	was_beside_messages = is_beside_messages;
	if (is_beside_messages)
		put_again(spectrum_area, GRAPH_LEFT + MESSAGES_WIDTH, MIDDLE_TOP + STATUS_HEIGHT,
			GRAPH_WIDTH - MESSAGES_WIDTH, PANEL_HEIGHT - MIDDLE_TOP - STATUS_HEIGHT);
	else
		put_again(spectrum_area, GRAPH_LEFT, MIDDLE_TOP + STATUS_HEIGHT, GRAPH_WIDTH,
			GRAPH_HEIGHT + FALL_HEIGHT);
}

static void show_values(void) {
	int i;

	if (focus != ON_FREQUENCY && !is_row_shown(&rows[focus]))
		focus = ON_FREQUENCY;
	for (i = 0; i < row_count; i++) {
		gtk_label_set_text(GTK_LABEL(rows[i].value_label),
			value_of(row_control(&rows[i])));
		gtk_widget_set_visible(rows[i].box, is_row_shown(&rows[i]));
		set_class(rows[i].box, "focused", i == focus);
		set_class(rows[i].box, "on", !strcmp(rows[i].control, "OFFSET")
			&& !is_offset("NORM"));
	}
	show_frequency();
	show_buttons();
	show_meter();
	place_graph();
	gtk_widget_queue_draw(wheel_area);
	gtk_widget_set_visible(messages_scroller, !strcmp(mode_group(), MESSAGES_GROUP));
	gtk_widget_queue_draw(spectrum_area);
}

static void send_line(const char *name, const char *value) {
	char line[LINE_MAX_LENGTH];

	if (writer == NULL)
		return;
	snprintf(line, sizeof(line), "%.*s %.*s\n", CONTROL_NAME_MAX - 1, name,
		CONTROL_VALUE_MAX - 1, value);
	g_output_stream_write_all(writer, line, strlen(line), NULL, NULL, NULL);
}

// the radio is sent where the receiver listens
static void send_frequency(void) {
	char text[CONTROL_VALUE_MAX];
	struct known *k = find_known("FREQ");

	snprintf(text, sizeof(text), "%d", receiver_hz());
	if (k != NULL)
		snprintf(k->value, CONTROL_VALUE_MAX, "%s", text);
	send_line("FREQ", text);
}

// STEP in Hz: 10H is 10, 1K is 1000
static int step_hz(void) {
	const char *step = value_of("STEP");
	int hz = atoi(step);

	if (strchr(step, 'K') != NULL)
		hz *= HZ_PER_K;
	if (hz <= 0)
		hz = HZ_PER_K;
	return hz;
}

static int clamp(int value, int lowest, int highest) {
	if (value < lowest)
		return lowest;
	if (value > highest)
		return highest;
	return value;
}

// a turn of the wheel on the frequency, as zbitxv2 tuned: the dial by
// STEP, onto a multiple of it, or while RIT is on, RIT_DELTA by STEP
static void tune(int steps) {
	struct known *k = find_known("RIT_DELTA");
	int step = step_hz();

	if (steps == 0 || k == NULL)
		return;
	if (is_offset("RIT"))
		snprintf(k->value, CONTROL_VALUE_MAX, "%d",
			clamp(atoi(k->value) + steps * step, -RIT_MAX_HZ, RIT_MAX_HZ));
	else
		dial_hz = clamp((dial_hz / step + steps) * step, LOWEST_HZ, HIGHEST_HZ);
	send_frequency();
	show_values();
}

// a VFO chosen: the dial of the one left is kept, and the other's taken
// up; the radio is told both dials, to keep
static void choose_vfo(int vfo) {
	char text[CONTROL_VALUE_MAX];
	int i;

	if (vfo == vfo_in_use)
		return;
	vfo_dial_hz[vfo_in_use] = dial_hz;
	if (vfo_dial_hz[vfo] == 0)
		vfo_dial_hz[vfo] = dial_hz;
	vfo_in_use = vfo;
	dial_hz = vfo_dial_hz[vfo];
	for (i = 0; i < VFOS; i++) {
		snprintf(text, sizeof(text), "%d", vfo_dial_hz[i]);
		send_line(vfo_dial_names[i], text);
	}
}

// one of the window's own controls has changed
static void apply_own(struct known *k) {
	struct known *vfo = find_known("VFO");
	struct known *delta = find_known("RIT_DELTA");

	if (!strcmp(k->name, "OFFSET") && is_offset("SPLIT") && vfo != NULL)
		snprintf(vfo->value, CONTROL_VALUE_MAX, "A");
	if (!is_offset("RIT") && delta != NULL)
		snprintf(delta->value, CONTROL_VALUE_MAX, "0");
	if (vfo != NULL && !strcmp(vfo->value, "B"))
		choose_vfo(VFO_B);
	else
		choose_vfo(VFO_A);
	send_frequency();
}

// the window's own controls, which it keeps and never sends
static void add_own_control(const char *name, const char *choices, const char *value) {
	struct known *k = find_known(name);

	if (k == NULL)
		return;
	k->is_own = 1;
	snprintf(k->group, CONTROL_NAME_MAX, "WINDOW");
	snprintf(k->value, CONTROL_VALUE_MAX, "%s", value);
	if (choices == NULL)
		return;
	k->is_choice = 1;
	snprintf(k->choices, CHOICES_MAX, "%s", choices);
}

static void add_own_controls(void) {
	add_own_control("STEP", "10H/100H/500H/1K/10K", "1K");
	add_own_control("OFFSET", "NORM/RIT/SPLIT", "NORM");
	add_own_control("RIT_DELTA", NULL, "0");
	add_own_control("VFO", "A/B", "A");
}

// the choice a step away from the present one, in a list such as
// "USB/LSB/CW"; it stops at either end
static void step_choice(struct known *k, int steps) {
	char list[CHOICES_MAX];
	char *names[CHOICES_MAX];
	char *name;
	int count = 0;
	int now = 0;
	int i;

	snprintf(list, sizeof(list), "%s", k->choices);
	for (name = strtok(list, "/"); name != NULL; name = strtok(NULL, "/"))
		names[count++] = name;
	if (count == 0)
		return;
	for (i = 0; i < count; i++) {
		if (!strcmp(names[i], k->value))
			now = i;
	}
	now += steps;
	if (now < 0)
		now = 0;
	if (now >= count)
		now = count - 1;
	snprintf(k->value, CONTROL_VALUE_MAX, "%s", names[now]);
}

// the next of a control's choices, and round from the last to the first
static void next_choice_round(struct known *k) {
	char choice[CONTROL_VALUE_MAX];
	int now = 0;
	int i;

	for (i = 0; choice_at(k->choices, i, choice, sizeof(choice)); i++) {
		if (!strcmp(choice, k->value))
			now = i;
	}
	if (!choice_at(k->choices, now + 1, choice, sizeof(choice))
			&& !choice_at(k->choices, 0, choice, sizeof(choice)))
		return;
	snprintf(k->value, CONTROL_VALUE_MAX, "%s", choice);
}

static void step_number(struct known *k, int steps) {
	int value = atoi(k->value) + steps * k->step;

	if (value < k->min)
		value = k->min;
	if (value > k->max)
		value = k->max;
	snprintf(k->value, CONTROL_VALUE_MAX, "%d", value);
}

static void step_row(int row, int steps) {
	struct known *k = find_known(row_control(&rows[row]));
	char before[CONTROL_VALUE_MAX];

	if (k == NULL)
		return;
	if (!k->is_choice && k->step == 0) {
		show_status("waiting for the radio's schema");
		return;
	}
	snprintf(before, sizeof(before), "%s", k->value);
	if (k->is_choice)
		step_choice(k, steps);
	else
		step_number(k, steps);
	if (!strcmp(before, k->value))
		return;
	if (k->is_own)
		apply_own(k);
	else
		send_line(k->name, k->value);
	show_values();
}

static void step_focused(int steps) {
	if (focus == ON_FREQUENCY)
		tune(steps);
	else
		step_row(focus, steps);
}

// CONTROL <group> <name> number <min> <max> <step>, or
// CONTROL <group> <name> choice <a/b/c>
static void take_schema_line(char *rest) {
	char group[CONTROL_NAME_MAX];
	char name[CONTROL_NAME_MAX];
	char kind[CONTROL_NAME_MAX];
	char choices[CHOICES_MAX];
	struct known *k;
	int min;
	int max;
	int step;

	if (sscanf(rest, "%23s %23s %23s", group, name, kind) != 3)
		return;
	k = find_known(name);
	if (k == NULL)
		return;
	snprintf(k->group, CONTROL_NAME_MAX, "%s", group);
	if (!strcmp(kind, "number")
			&& sscanf(rest, "%*s %*s %*s %d %d %d", &min, &max, &step) == 3) {
		k->min = min;
		k->max = max;
		k->step = step;
	}
	if (!strcmp(kind, "choice") && sscanf(rest, "%*s %*s %*s %127s", choices) == 1) {
		k->is_choice = 1;
		snprintf(k->choices, CHOICES_MAX, "%s", choices);
	}
	// an action has things it can do and no value: they are kept for the
	// buttons that do them, and it is not a row
	if (!strcmp(kind, "action") && sscanf(rest, "%*s %*s %*s %127s", choices) == 1)
		snprintf(k->choices, CHOICES_MAX, "%s", choices);
	k->is_text = !strcmp(kind, "text");
	k->is_listed = !strcmp(kind, "number") || !strcmp(kind, "choice");
}

static int hex_digit(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0;
}

// blue rises, then green rises, then blue falls, then red rises, then green
// falls: the front panel's colours
static guint32 heat_map(int shade) {
	int band = shade / SHADES_A_BAND;
	int step = (shade % SHADES_A_BAND) * SHADE_STEP;

	if (band == 0)
		return step;
	if (band == 1)
		return (step << 8) | COLOUR_MAX;
	if (band == 2)
		return (COLOUR_MAX << 8) | (COLOUR_MAX - step);
	if (band == 3)
		return (step << 16) | (COLOUR_MAX << 8);
	if (band == 4)
		return (COLOUR_MAX << 16) | ((COLOUR_MAX - step) << 8);
	return COLOUR_MAX << 16;
}

// SPECTRUM <span in Hz> <two hex digits a point>; the newest line goes at
// the top of the waterfall and the rest move down a row
static void take_spectrum_line(const char *rest) {
	const char *hex = strchr(rest, ' ');
	unsigned char *pixels;
	guint32 *row;
	int stride;
	int i;

	if (hex == NULL)
		return;
	is_modulation_shown = 0;
	span_hz = atoi(rest);
	hex++;
	point_count = strlen(hex) / 2;
	if (point_count > POINTS_MAX)
		point_count = POINTS_MAX;
	for (i = 0; i < point_count; i++)
		points[i] = (uint8_t)(hex_digit(hex[2 * i]) * 16 + hex_digit(hex[2 * i + 1]));

	cairo_surface_flush(fall);
	pixels = cairo_image_surface_get_data(fall);
	stride = cairo_image_surface_get_stride(fall);
	memmove(pixels + stride, pixels, (size_t)stride * (FALL_ROWS - 1));
	row = (guint32 *)pixels;
	for (i = 0; i < point_count; i++)
		row[i] = heat_map(points[i] * SHADES_PER_DB);
	cairo_surface_mark_dirty(fall);
	gtk_widget_queue_draw(spectrum_area);
}

// MODULATION <two hex digits a point>: the envelope of what the radio is
// sending, which it sends while it transmits where the spectrum was
static void take_modulation_line(const char *hex) {
	int i;

	modulation_count = strlen(hex) / 2;
	if (modulation_count > POINTS_MAX)
		modulation_count = POINTS_MAX;
	for (i = 0; i < modulation_count; i++)
		modulation[i] = (uint8_t)(hex_digit(hex[2 * i]) * 16 + hex_digit(hex[2 * i + 1]));
	is_modulation_shown = 1;
	gtk_widget_queue_draw(spectrum_area);
}

// the radio tells where the receiver listens; the dial is that, less
// RIT_DELTA while RIT is on
static void mark_message(GtkWidget *row);

static gboolean on_mark_kept_long_enough(gpointer unused) {
	(void)unused;
	mark_timer = 0;
	mark_message(NULL);
	return G_SOURCE_REMOVE;
}

// the mark is put on a message, or with NULL taken off; with none marked
// the list goes back to the newest
static void mark_message(GtkWidget *row) {
	GtkAdjustment *scroll = gtk_scrolled_window_get_vadjustment(
		GTK_SCROLLED_WINDOW(messages_scroller));

	if (marked_message != NULL)
		set_class(marked_message, "marked", 0);
	if (mark_timer != 0)
		g_source_remove(mark_timer);
	mark_timer = 0;
	marked_message = row;
	if (row == NULL) {
		gtk_adjustment_set_value(scroll, gtk_adjustment_get_upper(scroll));
		show_status("");
		return;
	}
	set_class(row, "marked", 1);
	show_status("tap it again, or press the knob, to answer");
	mark_timer = g_timeout_add_seconds(MARK_KEPT_FOR_S, on_mark_kept_long_enough, NULL);
}

// the marked message is answered: its line goes back to the radio, and
// the mark comes off
static void answer_marked(void) {
	const char *line;
	char text[LINE_MAX_LENGTH];

	if (marked_message == NULL)
		return;
	line = g_object_get_data(G_OBJECT(marked_message), "line");
	if (line != NULL && writer != NULL) {
		snprintf(text, sizeof(text), "answer %s\n", line);
		g_output_stream_write_all(writer, text, strlen(text), NULL, NULL, NULL);
	}
	mark_message(NULL);
}

// a tap on a message marks it; on the marked one, answers it
static void on_message_tap(GtkGestureMultiPress *gesture, gint presses, gdouble x,
		gdouble y, gpointer unused) {
	GtkWidget *row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

	(void)presses;
	(void)x;
	(void)y;
	(void)unused;
	if (row != marked_message)
		mark_message(row);
	else
		answer_marked();
}

static void on_message_destroyed(GtkWidget *row, gpointer unused) {
	(void)unused;
	if (row == marked_message)
		mark_message(NULL);
}

// the list is kept at its foot, where the newest message is
static void on_messages_sized(GtkWidget *widget, GdkRectangle *size, gpointer unused) {
	GtkAdjustment *scroll = gtk_scrolled_window_get_vadjustment(
		GTK_SCROLLED_WINDOW(messages_scroller));

	(void)widget;
	(void)size;
	(void)unused;
	if (marked_message == NULL)
		gtk_adjustment_set_value(scroll, gtk_adjustment_get_upper(scroll));
}

// on connecting the radio sends what it has kept, so the list starts empty
static void drop_all_messages(void) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(messages));
	GList *child;

	mark_message(NULL);
	for (child = children; child != NULL; child = child->next)
		gtk_widget_destroy(GTK_WIDGET(child->data));
	g_list_free(children);
	messages_shown = 0;
}

static void drop_oldest_message(void) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(messages));

	if (children != NULL) {
		gtk_widget_destroy(GTK_WIDGET(children->data));
		messages_shown--;
	}
	g_list_free(children);
}

// whether a message has a callsign among its words; a callsign sent as a
// hash is shown between angle brackets, and is found too
static int has_callsign(const char *text, const char *callsign) {
	size_t length = strlen(callsign);
	const char *at = text;
	const char *end;

	if (length == 0)
		return 0;
	while (*at != 0) {
		while (*at == ' ' || *at == '<')
			at++;
		end = at;
		while (*end != 0 && *end != ' ' && *end != '>')
			end++;
		if ((size_t)(end - at) == length && !g_ascii_strncasecmp(at, callsign, length))
			return 1;
		at = end;
		if (*at == '>')
			at++;
	}
	return 0;
}

// MESSAGE 043015|-12|1797|CQ VU2ESE MK97: a row is added to the list
static void take_message_line(const char *line) {
	char copy[LINE_MAX_LENGTH];
	char shown[LINE_MAX_LENGTH];
	char *field[MESSAGE_FIELDS];
	char *saved = NULL;
	GtkWidget *box;
	GtkWidget *label;
	const char *seconds;
	int minute;
	int second;
	int i;

	snprintf(copy, sizeof(copy), "%s", line);
	field[0] = strtok_r(copy, "|", &saved);
	for (i = 1; i < MESSAGE_FIELDS - 1; i++)
		field[i] = strtok_r(NULL, "|", &saved);
	field[MESSAGE_FIELDS - 1] = strtok_r(NULL, "", &saved);
	if (field[MESSAGE_FIELDS - 1] == NULL)
		return;
	seconds = field[0];
	if (strlen(seconds) == TIME_DIGITS)
		seconds += TIME_SECOND_AT;
	snprintf(shown, sizeof(shown), "%s %3s %s", seconds, field[1], field[3]);
	label = gtk_label_new(shown);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	add_label_class(label, "message");
	if (!strcmp(field[1], "TX"))
		add_label_class(label, "sent");
	else if (has_callsign(field[3], value_of(CALLSIGN_CONTROL)))
		add_label_class(label, "to-us");
	else if (!strncmp(field[3], "CQ ", strlen("CQ ")))
		add_label_class(label, "calling");
	box = gtk_event_box_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "message-row");
	// the slots are told apart by their ground: every other one is lighter
	if (strlen(field[0]) == TIME_DIGITS) {
		minute = atoi(field[0] + TIME_MINUTE_AT) / DIGITS_OF_TWO;
		second = atoi(field[0] + TIME_SECOND_AT);
		set_class(box, "other-slot",
			((minute * SECONDS_A_MINUTE + second) / SLOT_SECONDS) % 2);
	}
	gtk_container_add(GTK_CONTAINER(box), label);
	g_object_set_data_full(G_OBJECT(box), "line", g_strdup(line), g_free);
	tell_taps(box, G_CALLBACK(on_message_tap), NULL);
	g_signal_connect(box, "destroy", G_CALLBACK(on_message_destroyed), NULL);
	gtk_box_pack_start(GTK_BOX(messages), box, FALSE, FALSE, 0);
	gtk_widget_show_all(box);
	messages_shown++;
	if (messages_shown > MESSAGES_SHOWN_MAX)
		drop_oldest_message();
}

static void take_radio_frequency(int hz) {
	dial_hz = hz;
	if (is_offset("RIT"))
		dial_hz -= atoi(value_of("RIT_DELTA"));
	// a VFO never used is put here once, and does not follow after
	if (vfo_dial_hz[vfo_in_waiting()] == 0)
		vfo_dial_hz[vfo_in_waiting()] = dial_hz;
}

// the dial the radio has kept for the VFO in waiting is taken up
static void take_kept_dial(const char *name, int hz) {
	if (hz > 0 && !strcmp(name, vfo_dial_names[vfo_in_waiting()]))
		vfo_dial_hz[vfo_in_waiting()] = hz;
}

static void fill_settings(void);
static int is_page_shown(const char *name);

static void handle_line(const char *name, char *value) {
	struct known *k;

	// the logbook's window takes what is its own of every line
	logbook_window_took(name, value);
	if (!strcmp(name, "LOG") || !strcmp(name, "LOG_LIST"))
		return;

	if (!strcmp(name, "SPECTRUM")) {
		take_spectrum_line(value);
		return;
	}
	if (!strcmp(name, "MODULATION")) {
		take_modulation_line(value);
		return;
	}
	if (!strcmp(name, "CONTROL")) {
		if (strcmp(value, "END"))
			take_schema_line(value);
		else
			make_list();
		return;
	}
	if (!strcmp(name, "MESSAGE")) {
		take_message_line(value);
		return;
	}
	if (!strcmp(name, "TIME")) {
		take_time_line(value);
		return;
	}
	if (!strcmp(name, "CW")) {
		set_class(status_label, "qso", 0);
		take_cw_line(value);
		return;
	}
	if (!strcmp(name, "SAY")) {
		set_class(status_label, "qso", 0);
		show_status(value);
		return;
	}
	if (!strcmp(name, "WARN")) {
		show_warning(value);
		return;
	}
	if (!strcmp(name, "QSO")) {
		set_class(status_label, "qso", value[0] != 0);
		show_status(value);
		return;
	}
	if (!strcmp(name, "CLOCK") && value_of("CLOCK")[0] != 0
			&& strcmp(value_of("CLOCK"), value))
		take_start_of_minute();
	if (!strcmp(name, "ERROR")) {
		show_status(value);
		return;
	}
	k = find_known(name);
	if (k == NULL || k->is_own)
		return;
	snprintf(k->value, CONTROL_VALUE_MAX, "%s", value);
	if (!strcmp(name, "FREQ"))
		take_radio_frequency(atoi(value));
	take_kept_dial(name, atoi(value));
	show_values();
	if ((is_page_shown(PAGE_SETTINGS) || is_page_shown(PAGE_CONNECTIONS))
			&& (!strcmp(k->group, STATION_GROUP) || !strcmp(k->group, REMOTE_GROUP)
			|| !strcmp(k->group, CONNECTIONS_GROUP)))
		fill_settings();
}

// the one try that is waiting to be made
static gboolean on_time_to_retry(gpointer unused) {
	(void)unused;
	retry_timer = 0;
	try_to_connect();
	return G_SOURCE_REMOVE;
}

// the connection is let go and another is tried for, once, a little later.
// There is never more than one try waiting and never more than one
// connection: until 2026-10-08 the try was made again every three seconds
// for ever after the first loss, each a new connection to the radio, which
// has eight places for clients, and the window grew until the sbitx had no
// memory left
static void disconnect(const char *why) {
	char text[LINE_MAX_LENGTH];

	g_clear_object(&reader);
	writer = NULL;
	g_clear_object(&connection);
	through = NOT_THROUGH;
	snprintf(text, sizeof(text), "%s: %s; trying again", radio_name, why);
	show_status(text);
	if (retry_timer == 0)
		retry_timer = g_timeout_add(RETRY_EVERY_MS, on_time_to_retry, NULL);
}

/* reaching a radio through my.sbitx.net */

static void open_text(struct known *k);
static void show_page(const char *name);

static void send_a_line(const char *line) {
	if (writer == NULL)
		return;
	g_output_stream_write_all(writer, line, strlen(line), NULL, NULL, NULL);
	g_output_stream_write_all(writer, "\n", 1, NULL, NULL, NULL);
}

// where the radio's client password is kept on this computer, in a folder
// that is made if it is not there
static void password_path(char *path, int room) {
	char folder[PATH_ROOM];

	snprintf(folder, sizeof(folder), "%s/zbitx", g_get_user_config_dir());
	g_mkdir_with_parents(folder, KEY_FOLDER_MODE);
	snprintf(path, room, "%.*s/%.40s.password", PATH_ROOM - 64, folder, through_wanted);
}

// the letters of a client password as the website keeps them: small, with
// the strokes and spaces it is written with left out
static void tidy_password(const char *typed_text, char *letters, int room) {
	int used = 0;
	int i;

	for (i = 0; typed_text[i] != 0 && used < room - 1; i++) {
		if (g_ascii_isalpha(typed_text[i]))
			letters[used++] = g_ascii_tolower(typed_text[i]);
	}
	letters[used] = 0;
}

// the password is stretched as the service stretches it, with the radio's
// name as salt, and what comes of it is the key of the proofs
static void take_password(const char *typed_text) {
	char letters[PASSWORD_ROOM];
	char salt[LINE_MAX_LENGTH];
	size_t i;

	tidy_password(typed_text, letters, sizeof(letters));
	snprintf(salt, sizeof(salt), "%s%.40s", SALT_BEGINS, through_wanted);
	for (i = 0; salt[i] != 0; i++)
		salt[i] = g_ascii_tolower(salt[i]);
	pbkdf2_sha256_hex(letters, salt, STRETCH_ROUNDS, through_key);
	has_through_key = letters[0] != 0;
}

static void save_password(const char *typed_text) {
	char path[PATH_ROOM];
	char letters[PASSWORD_ROOM];
	FILE *file;

	password_path(path, sizeof(path));
	tidy_password(typed_text, letters, sizeof(letters));
	file = fopen(path, "w");
	if (file != NULL) {
		g_chmod(path, KEY_FILE_MODE);
		fprintf(file, "%s\n", letters);
		fclose(file);
	}
	take_password(letters);
}

static void forget_password(void) {
	char path[PATH_ROOM];

	password_path(path, sizeof(path));
	g_remove(path);
	has_through_key = 0;
}

// says whether the window has the radio's client password; if it has not,
// from an earlier time or from its file, the owner is asked to type it
static int has_password_or_asks(void) {
	char path[PATH_ROOM];
	char line[LINE_MAX_LENGTH];
	FILE *file;

	if (has_through_key)
		return 1;
	password_path(path, sizeof(path));
	file = fopen(path, "r");
	if (file != NULL) {
		if (fgets(line, sizeof(line), file) != NULL)
			take_password(line);
		fclose(file);
	}
	if (has_through_key)
		return 1;
	snprintf(password_asked.name, sizeof(password_asked.name), "CLIENT PASSWORD");
	password_asked.value[0] = 0;
	password_asked.is_text = 1;
	open_text(&password_asked);
	return 0;
}

// what was typed on the text screen was the password: it is kept, and the
// radio is tried for
static void take_typed_password(const char *typed_text) {
	save_password(typed_text);
	show_page(PAGE_MAIN);
	try_to_connect();
}

// the service's challenge is answered with a proof made with the client
// password, and the nonce the proof was made with is kept, to check the
// service's own proof by
static void answer_the_challenge(const char *challenge) {
	uint8_t random_bytes[NONCE_BYTES];
	char text[LINE_MAX_LENGTH * 2];
	char proof[SHA256_HEX_ROOM];
	int i;

	if (getrandom(random_bytes, sizeof(random_bytes), 0) != sizeof(random_bytes)) {
		disconnect("no random numbers to be had");
		return;
	}
	for (i = 0; i < NONCE_BYTES; i++)
		snprintf(through_nonce + 2 * i, 3, "%02x", random_bytes[i]);
	snprintf(through_challenge, sizeof(through_challenge), "%.*s",
		(int)sizeof(through_challenge) - 1, challenge);
	snprintf(text, sizeof(text), "zbitx-auth-1 client %.40s %s %s", through_wanted,
		through_challenge, through_nonce);
	hmac_sha256_hex(through_key, text, proof);
	snprintf(text, sizeof(text), "AUTH client %.40s %s %s", through_wanted, through_nonce,
		proof);
	send_a_line(text);
	through = TO_BE_WELCOMED;
}

// whether what answered has the client password too
static int is_the_service(const char *its_proof) {
	char text[LINE_MAX_LENGTH * 2];
	char proof[SHA256_HEX_ROOM];
	unsigned char differs = 0;
	size_t i;

	if (strlen(its_proof) != SHA256_HEX_ROOM - 1)
		return 0;
	snprintf(text, sizeof(text), "zbitx-auth-1 server %.40s %s %s", through_wanted,
		through_challenge, through_nonce);
	hmac_sha256_hex(through_key, text, proof);
	for (i = 0; i < SHA256_HEX_ROOM - 1; i++)
		differs |= (unsigned char)(proof[i] ^ its_proof[i]);
	return differs == 0;
}

// a line from the service, before the radio is on the line
static void take_through_line(char *line) {
	const char *asks = "schema\nspectrum on\nmessages on\ntime";
	char text[LINE_MAX_LENGTH];
	char *rest = strchr(line, ' ');

	if (rest != NULL)
		*rest++ = 0;
	else
		rest = line + strlen(line);
	if (!strcmp(line, "ERROR")) {
		if (!strncmp(rest, "AUTH", strlen("AUTH")) && through == TO_BE_WELCOMED)
			forget_password();
		disconnect(rest);
		return;
	}
	if (through == TO_BE_CHALLENGED && !strcmp(line, "CHALLENGE")) {
		answer_the_challenge(rest);
		return;
	}
	if (through == TO_BE_WELCOMED && !strcmp(line, "WELCOME")) {
		rest = strchr(rest, ' ');
		if (rest == NULL || !is_the_service(rest + 1)) {
			disconnect("what answered does not have the client password");
			return;
		}
		through = TO_BE_CONNECTED;
		return;
	}
	if (through == TO_BE_CONNECTED && !strcmp(line, "CONNECTED")) {
		through = THROUGH;
		send_a_line(asks);
		snprintf(text, sizeof(text), "connected to %.40s through %.40s", rest, radio_name);
		show_status(text);
	}
}

static void on_line(GObject *source, GAsyncResult *result, gpointer unused) {
	GError *error = NULL;
	char *line;
	char *value;

	(void)unused;
	line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source),
		result, NULL, &error);
	// a connection already let go has nothing more to say
	if ((gpointer)source != (gpointer)reader) {
		g_free(line);
		g_clear_error(&error);
		return;
	}
	if (line == NULL) {
		g_clear_error(&error);
		disconnect("gone");
		return;
	}
	if (is_remote && through != THROUGH) {
		take_through_line(line);
		g_free(line);
		if (reader != NULL)
			g_data_input_stream_read_line_async(reader, G_PRIORITY_DEFAULT, NULL,
				on_line, NULL);
		return;
	}
	value = strchr(line, ' ');
	if (value != NULL) {
		*value = 0;
		handle_line(line, value + 1);
	}
	g_free(line);
	g_data_input_stream_read_line_async(reader, G_PRIORITY_DEFAULT, NULL,
		on_line, NULL);
}

static void on_connected(GObject *source, GAsyncResult *result, gpointer unused) {
	GError *error = NULL;
	char text[LINE_MAX_LENGTH];
	const char *asks = "schema\nspectrum on\nmessages on\ntime\n";
	GSocketConnection *made;

	(void)unused;
	made = g_socket_client_connect_to_host_finish(G_SOCKET_CLIENT(source), result, &error);
	is_connecting = 0;
	if (made == NULL) {
		disconnect(error->message);
		g_error_free(error);
		return;
	}
	if (connection != NULL) {
		g_object_unref(made);
		return;
	}
	connection = made;
	reader = g_data_input_stream_new(
		g_io_stream_get_input_stream(G_IO_STREAM(connection)));
	writer = g_io_stream_get_output_stream(G_IO_STREAM(connection));
	drop_all_messages();
	if (is_remote) {
		through = TO_BE_CHALLENGED;
		snprintf(text, sizeof(text), "%s: saying who this is", radio_name);
	} else {
		g_output_stream_write_all(writer, asks, strlen(asks), NULL, NULL, NULL);
		snprintf(text, sizeof(text), "connected to %s", radio_name);
	}
	show_status(text);
	g_data_input_stream_read_line_async(reader, G_PRIORITY_DEFAULT, NULL,
		on_line, NULL);
}

static void try_to_connect(void) {
	char text[LINE_MAX_LENGTH];

	if (connection != NULL || is_connecting)
		return;
	if (is_remote && !has_password_or_asks())
		return;
	is_connecting = 1;
	snprintf(text, sizeof(text), "connecting to %s", radio_name);
	show_status(text);
	g_socket_client_connect_to_host_async(connector, radio_name, radio_port,
		NULL, on_connected, NULL);
}

static int wheel_steps(const GdkEventScroll *event) {
	if (event->direction == GDK_SCROLL_UP)
		return 1;
	if (event->direction == GDK_SCROLL_DOWN)
		return -1;
	return 0;
}

static gboolean on_frequency_wheel(GtkWidget *widget, GdkEventScroll *event, gpointer unused) {
	(void)widget;
	(void)unused;
	tune(wheel_steps(event));
	return TRUE;
}

// a click on the frequency selects it; on it when it is selected, goes to
// the other VFO
/* the tuning wheel */

// the wheel fills the smaller way of its box, less a margin
static double wheel_radius(void) {
	double across = gtk_widget_get_allocated_width(wheel_area);
	double down = gtk_widget_get_allocated_height(wheel_area);

	if (down < across)
		across = down;
	return across / 2.0 - px(WHEEL_MARGIN);
}

// the angle of a place on the wheel from its middle, which grows clockwise
static double wheel_angle_at(double x, double y) {
	double middle_x = gtk_widget_get_allocated_width(wheel_area) / 2.0;
	double middle_y = gtk_widget_get_allocated_height(wheel_area) / 2.0;

	return atan2(y - middle_y, x - middle_x);
}

// the hub, which has the cross that puts the box away
static int is_on_wheel_close(double x, double y) {
	double across = x - gtk_widget_get_allocated_width(wheel_area) / 2.0;
	double down = y - gtk_widget_get_allocated_height(wheel_area) / 2.0;
	double hub = wheel_radius() * WHEEL_HUB_SHARE;

	return across * across + down * down <= hub * hub;
}

static void show_wheel(int on) {
	is_wheel_held = 0;
	gtk_widget_set_visible(wheel_area, on);
	if (on && gtk_widget_get_window(wheel_area) != NULL)
		gdk_window_raise(gtk_widget_get_window(wheel_area));
}

static gboolean on_draw_wheel(GtkWidget *widget, cairo_t *cr, gpointer unused) {
	double width = gtk_widget_get_allocated_width(widget);
	double height = gtk_widget_get_allocated_height(widget);
	double radius = wheel_radius();
	double arm = px(WHEEL_CLOSE_ARM);
	double angle;
	int i;

	(void)unused;
	cairo_set_source_rgb(cr, 0.063, 0.094, 0.125);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 1.0, 0.75, 0.0);
	cairo_set_line_width(cr, px(1));
	cairo_rectangle(cr, px(1) / 2.0, px(1) / 2.0, width - px(1), height - px(1));
	cairo_stroke(cr);

	cairo_translate(cr, width / 2.0, height / 2.0);
	cairo_set_source_rgb(cr, 0.16, 0.22, 0.28);
	if (is_wheel_held)
		cairo_set_source_rgb(cr, 0.22, 0.30, 0.38);
	cairo_arc(cr, 0, 0, radius, 0, 2.0 * G_PI);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.56, 0.63, 0.69);
	cairo_set_line_width(cr, px(1));
	for (i = 0; i < WHEEL_TICKS; i++) {
		angle = wheel_angle + i * WHEEL_NOTCH;
		cairo_move_to(cr, radius * cos(angle), radius * sin(angle));
		cairo_line_to(cr, radius * (1.0 - WHEEL_TICK_SHARE) * cos(angle),
			radius * (1.0 - WHEEL_TICK_SHARE) * sin(angle));
	}
	cairo_stroke(cr);
	// the dimple goes round with the wheel
	cairo_set_source_rgb(cr, 0.04, 0.06, 0.08);
	cairo_arc(cr, radius * WHEEL_DIMPLE_AT * cos(wheel_angle),
		radius * WHEEL_DIMPLE_AT * sin(wheel_angle), radius * WHEEL_DIMPLE_SHARE, 0,
		2.0 * G_PI);
	cairo_fill(cr);
	// the hub does not turn, and has the cross that puts the box away
	cairo_set_source_rgb(cr, 0.063, 0.094, 0.125);
	cairo_arc(cr, 0, 0, radius * WHEEL_HUB_SHARE, 0, 2.0 * G_PI);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_line_width(cr, px(1) + 1);
	cairo_move_to(cr, -arm, -arm);
	cairo_line_to(cr, arm, arm);
	cairo_move_to(cr, arm, -arm);
	cairo_line_to(cr, -arm, arm);
	cairo_stroke(cr);
	return TRUE;
}

// a finger, or the mouse, is put on the box: on the hub it puts the box
// away, anywhere else it takes hold of the wheel
static void on_wheel_held(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer unused) {
	(void)gesture;
	(void)unused;
	if (is_on_wheel_close(x, y)) {
		show_wheel(0);
		return;
	}
	is_wheel_held = 1;
	wheel_began_x = x;
	wheel_began_y = y;
	wheel_finger_angle = wheel_angle_at(x, y);
	wheel_owed = 0;
	gtk_widget_queue_draw(wheel_area);
}

// the finger has moved: the wheel goes round by the angle it went through,
// and the radio is tuned a step for each notch of it
static void on_wheel_turned(GtkGestureDrag *gesture, gdouble offset_x, gdouble offset_y,
		gpointer unused) {
	double now;
	double turned;
	int notches;

	(void)gesture;
	(void)unused;
	if (!is_wheel_held)
		return;
	now = wheel_angle_at(wheel_began_x + offset_x, wheel_began_y + offset_y);
	turned = now - wheel_finger_angle;
	// the short way round, across where the angle wraps
	if (turned > G_PI)
		turned -= 2.0 * G_PI;
	if (turned < -G_PI)
		turned += 2.0 * G_PI;
	wheel_finger_angle = now;
	wheel_angle += turned;
	wheel_owed += turned;
	notches = (int)(wheel_owed / WHEEL_NOTCH);
	wheel_owed -= notches * WHEEL_NOTCH;
	if (notches != 0)
		tune(notches);
	gtk_widget_queue_draw(wheel_area);
}

static void on_wheel_let_go(GtkGestureDrag *gesture, gdouble offset_x, gdouble offset_y,
		gpointer unused) {
	(void)gesture;
	(void)offset_x;
	(void)offset_y;
	(void)unused;
	is_wheel_held = 0;
	gtk_widget_queue_draw(wheel_area);
}

// a tap on the frequency drops the tuning wheel down, or puts it away
static gboolean on_frequency_click(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
	(void)widget;
	(void)event;
	(void)unused;
	focus = ON_FREQUENCY;
	show_wheel(!gtk_widget_get_visible(wheel_area));
	show_values();
	return TRUE;
}

static gboolean on_row_wheel(GtkWidget *widget, GdkEventScroll *event, gpointer row) {
	(void)widget;
	step_row(GPOINTER_TO_INT(row), wheel_steps(event));
	return TRUE;
}

// a tap on a row selects it; on a choice that is selected, moves it on
static void on_row_tap(GtkGestureMultiPress *gesture, gint presses, gdouble x, gdouble y,
		gpointer row) {
	int tapped = GPOINTER_TO_INT(row);
	struct known *k = find_known(row_control(&rows[tapped]));

	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	if (focus == tapped && k != NULL && k->is_choice) {
		next_choice_round(k);
		if (k->is_own)
			apply_own(k);
		else
			send_line(k->name, k->value);
	}
	focus = tapped;
	show_values();
}

// a widget is made to tell a tap: a press and a release in one place, by
// the mouse or a finger. In a list that scrolls, a finger that moves is
// taken by the list for scrolling, and no tap is told
static void tell_taps(GtkWidget *widget, GCallback tapped, gpointer data) {
	GtkGesture *gesture = gtk_gesture_multi_press_new(widget);

	gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
		| GDK_TOUCH_MASK);
	g_signal_connect(gesture, "released", tapped, data);
	g_object_set_data_full(G_OBJECT(widget), "tap", gesture, g_object_unref);
}

// the control is put at the value, here and on the radio
static void set_and_send(const char *control, const char *value) {
	struct known *k = find_known(control);

	if (k != NULL && k->is_listed)
		snprintf(k->value, CONTROL_VALUE_MAX, "%s", value);
	send_line(control, value);
	show_values();
}

// a band's button: to the band, or on the band in use, to the next place
// in its stack
static gboolean on_band_click(GtkWidget *widget, GdkEventButton *event, gpointer band) {
	const char *name = band_names[GPOINTER_TO_INT(band)];
	struct known *stack = find_known("STACK");
	char place[CONTROL_VALUE_MAX];

	(void)widget;
	(void)event;
	if (strcmp(value_of("BAND"), name) || stack == NULL) {
		set_and_send("BAND", name);
		return TRUE;
	}
	snprintf(place, sizeof(place), "%d", stack->min);
	if (atoi(stack->value) < stack->max)
		snprintf(place, sizeof(place), "%d", atoi(stack->value) + 1);
	set_and_send("STACK", place);
	return TRUE;
}

static gboolean on_footer_click(GtkWidget *widget, GdkEventButton *event, gpointer slot) {
	char label[CONTROL_VALUE_MAX];
	char control[CONTROL_VALUE_MAX];
	char choice[CONTROL_VALUE_MAX];

	(void)widget;
	(void)event;
	if (footer_button_at(GPOINTER_TO_INT(slot), label, control, choice, sizeof(label)))
		set_and_send(control, choice);
	return TRUE;
}

static void fill_settings(void);

static void show_page(const char *name) {
	gtk_stack_set_visible_child_name(GTK_STACK(pages), name);
}

static int is_page_shown(const char *name) {
	const char *shown = gtk_stack_get_visible_child_name(GTK_STACK(pages));

	return shown != NULL && !strcmp(shown, name);
}

static void show_typed(void) {
	char with_mark[CONTROL_VALUE_MAX + 2];

	snprintf(with_mark, sizeof(with_mark), "%s_", typed);
	gtk_label_set_text(GTK_LABEL(typed_label), with_mark);
}

// the screen a text is typed on, for a control, beginning with what it has
static void open_text(struct known *k) {
	typed_for = k;
	snprintf(typed, sizeof(typed), "%s", k->value);
	gtk_label_set_text(GTK_LABEL(text_title), k->name);
	gtk_button_set_label(GTK_BUTTON(cancel_key), k == &cw_sending ? "DONE" : "CANCEL");
	gtk_button_set_label(GTK_BUTTON(save_key), k == &cw_sending ? "ESC" : "SAVE");
	show_typed();
	show_page(PAGE_TEXT);
}

// the same screen, to send CW with
static void open_cw_keyboard(void) {
	snprintf(cw_sending.name, sizeof(cw_sending.name), "SENDING CW");
	cw_sending.value[0] = 0;
	open_text(&cw_sending);
}

// a character touched while sending CW goes to the radio at once
static void send_cw_character(char c) {
	char text[2] = { c, 0 };

	if (c == ' ')
		send_line(CW_TEXT_CONTROL, CW_TEXT_SPACE);
	else if (c != '_')
		send_line(CW_TEXT_CONTROL, text);
}

static void type_a_character(char c) {
	size_t length = strlen(typed);

	if (typed_for == &cw_sending) {
		send_cw_character(c);
		// the box keeps the newest of what was sent
		if (length >= CW_SENT_SHOWN) {
			memmove(typed, typed + 1, length);
			length--;
		}
	}
	if (length + 1 >= sizeof(typed))
		return;
	typed[length] = c;
	typed[length + 1] = 0;
	show_typed();
}

static void rub_out(void) {
	size_t length = strlen(typed);

	if (length > 0)
		typed[length - 1] = 0;
	show_typed();
}

// what was typed goes to the radio, if it is something and is new, and the
// settings come back
static void save_typed(void) {
	// while sending CW the key is ESC: what the radio is still sending is
	// stopped, and the record of it cleared
	if (typed_for == &cw_sending) {
		send_line(CW_DO_CONTROL, "ESC");
		typed[0] = 0;
		show_typed();
		return;
	}
	if (typed_for == &password_asked) {
		take_typed_password(typed);
		return;
	}
	if (typed_for != NULL && typed[0] != 0 && strcmp(typed, typed_for->value)) {
		snprintf(typed_for->value, CONTROL_VALUE_MAX, "%s", typed);
		send_line(typed_for->name, typed_for->value);
	}
	fill_settings();
	show_page(PAGE_SETTINGS);
}

static void on_key_pressed(GtkButton *button, gpointer character) {
	(void)button;
	type_a_character((char)GPOINTER_TO_INT(character));
}

static void on_rub_out_pressed(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	rub_out();
}

static void on_save_pressed(GtkButton *button, gpointer unused) {
	(void)button;
	(void)unused;
	save_typed();
}

// back from a screen to the one it was reached from; from the keyboard
// that is the settings, unless it was sending CW, which is reached from the
// radio's own screen
static void on_back_pressed(GtkButton *button, gpointer page) {
	(void)button;
	if (is_page_shown(PAGE_TEXT) && typed_for == &cw_sending)
		page = PAGE_MAIN;
	show_page(page);
}

// a tap on a setting: a text is typed, a choice goes to its next
static void on_setting_tap(GtkGestureMultiPress *gesture, gint presses, gdouble x,
		gdouble y, gpointer setting) {
	struct known *k = setting;

	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	if (k->is_text) {
		open_text(k);
		return;
	}
	next_choice_round(k);
	send_line(k->name, k->value);
	fill_settings();
}

// a list's rows are taken away, for it to be made afresh
static void empty_list(GtkWidget *list) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(list));
	GList *child;

	for (child = children; child != NULL; child = child->next)
		gtk_widget_destroy(GTK_WIDGET(child->data));
	g_list_free(children);
}

// a row to each of the radio's texts and choices in a group, to be tapped
static void fill_with_group(GtkWidget *list, const char *group) {
	GtkWidget *row;
	GtkWidget *value;
	int i;

	for (i = 0; i < known_count; i++) {
		if (strcmp(known[i].group, group) || (!known[i].is_text && !known[i].is_choice))
			continue;
		row = list_row_new(known[i].name, &value);
		gtk_label_set_text(GTK_LABEL(value), known[i].value);
		gtk_widget_set_size_request(row, -1, px_down(ROW_HEIGHT));
		tell_taps(row, G_CALLBACK(on_setting_tap), &known[i]);
		gtk_box_pack_start(GTK_BOX(list), row, FALSE, FALSE, 0);
	}
}

static void on_connections_tap(GtkGestureMultiPress *gesture, gint presses, gdouble x,
		gdouble y, gpointer unused) {
	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	(void)unused;
	show_page(PAGE_CONNECTIONS);
}

// the two screens' rows, made afresh each time they are shown. The
// settings: the radio's texts and choices in the group STATION, and a row
// that opens the connections. The connections: the switches in the group
// CONNECTIONS, and what the radio says of its link to my.sbitx.net
static void fill_settings(void) {
	GtkWidget *row;
	GtkWidget *value;
	char said[TEXT_MAX];
	int i;

	empty_list(settings_list);
	fill_with_group(settings_list, STATION_GROUP);
	// set at the radio and nowhere else
	if (!is_remote) {
		row = list_row_new("CONNECTIONS", &value);
		gtk_label_set_text(GTK_LABEL(value), "\u203a");
		gtk_widget_set_size_request(row, -1, px_down(ROW_HEIGHT));
		tell_taps(row, G_CALLBACK(on_connections_tap), NULL);
		gtk_box_pack_start(GTK_BOX(settings_list), row, FALSE, FALSE, 0);
	}
	gtk_widget_show_all(settings_list);

	empty_list(connections_list);
	fill_with_group(connections_list, CONNECTIONS_GROUP);
	// what the radio says of being reached from afar: shown, not tapped
	for (i = 0; i < known_count; i++) {
		if (strcmp(known[i].group, REMOTE_GROUP) || !known[i].is_text)
			continue;
		row = list_row_new(known[i].name, &value);
		snprintf(said, sizeof(said), "%s", known[i].value);
		if (!strncmp(known[i].value, COOKIE_WORD, strlen(COOKIE_WORD)))
			snprintf(said, sizeof(said), "type %s at %s",
				known[i].value + strlen(COOKIE_WORD), ACCOUNT_SITE);
		gtk_label_set_text(GTK_LABEL(value), said);
		gtk_widget_set_size_request(row, -1, px_down(ROW_HEIGHT));
		gtk_box_pack_start(GTK_BOX(connections_list), row, FALSE, FALSE, 0);
	}
	gtk_widget_show_all(connections_list);
}

// the column's buttons: SET, and in FT8 its two; the others do nothing yet
static gboolean on_column_click(GtkWidget *widget, GdkEventButton *event, gpointer row) {
	int clicked = GPOINTER_TO_INT(row);

	(void)widget;
	(void)event;
	if (clicked == SET_ROW) {
		fill_settings();
		show_page(PAGE_SETTINGS);
		return TRUE;
	}
	if (clicked == LOG_ROW) {
		logbook_window_open(GTK_WINDOW(main_window), send_line, value_of);
		return TRUE;
	}
	if (clicked == CW_KBD_ROW && !strcmp(mode_group(), MACRO_GROUP)) {
		open_cw_keyboard();
		return TRUE;
	}
	if (clicked == CW_ESC_ROW && !strcmp(mode_group(), MACRO_GROUP))
		send_line(CW_DO_CONTROL, "ESC");
	if (strcmp(mode_group(), MESSAGES_GROUP))
		return TRUE;
	if (clicked == FT8_CQ_ROW)
		send_line(FT8_DO_CONTROL, "CQ");
	if (clicked == FT8_HALT_ROW)
		send_line(FT8_DO_CONTROL, "HALT");
	return TRUE;
}

static int hz_of_x(double x, int width);

// in FT8 a tap on the graph or the waterfall puts our pitch where it fell,
// at the nearest step the radio's control has, within what it allows
static void take_pitch_tap(double x) {
	struct known *pitch = find_known(TX_PITCH_CONTROL);
	char value[CONTROL_VALUE_MAX];
	int hz;

	if (pitch == NULL || pitch->step <= 0)
		return;
	hz = hz_of_x(x, gtk_widget_get_allocated_width(spectrum_area));
	hz = ((hz + pitch->step / 2) / pitch->step) * pitch->step;
	if (hz < pitch->min)
		hz = pitch->min;
	if (hz > pitch->max)
		hz = pitch->max;
	snprintf(value, sizeof(value), "%d", hz);
	snprintf(pitch->value, sizeof(pitch->value), "%s", value);
	send_line(pitch->name, value);
	show_values();
}

// a tap on the graph or the waterfall: the next span, or in FT8 the pitch
static void on_spectrum_tap(GtkGestureMultiPress *gesture, gint presses, gdouble x,
		gdouble y, gpointer unused) {
	struct known *span = find_known("SPAN");

	(void)gesture;
	(void)presses;
	(void)y;
	(void)unused;
	if (!strcmp(mode_group(), MESSAGES_GROUP)) {
		take_pitch_tap(x);
		return;
	}
	if (span == NULL || !span->is_choice)
		return;
	next_choice_round(span);
	send_line(span->name, span->value);
	show_values();
}

// the list is scrolled, if it must be, to show the row with the focus
static void scroll_to_focus(void) {
	GtkAdjustment *scroll;
	GtkAllocation where;
	double top;
	double page;

	if (focus == ON_FREQUENCY || rows[focus].place != IN_LIST)
		return;
	scroll = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(list_scroller));
	gtk_widget_get_allocation(rows[focus].box, &where);
	top = gtk_adjustment_get_value(scroll);
	page = gtk_adjustment_get_page_size(scroll);
	if (where.y < top)
		gtk_adjustment_set_value(scroll, where.y);
	else if (where.y + where.height > top + page)
		gtk_adjustment_set_value(scroll, where.y + where.height - page);
}

// the next row shown, down the list or up it; past either end is the
// frequency
static void move_focus(int direction) {
	int next = focus;

	if (next == ON_FREQUENCY && direction < 0)
		next = row_count;
	do {
		next += direction;
	} while (next >= 0 && next < row_count && !is_row_shown(&rows[next]));
	if (next < 0 || next >= row_count)
		next = ON_FREQUENCY;
	focus = next;
	show_values();
	scroll_to_focus();
}

// a key of a keyboard plugged into the radio, while a text is being typed
static gboolean on_key_while_typing(const GdkEventKey *event) {
	guint32 character = gdk_keyval_to_unicode(gdk_keyval_to_upper(event->keyval));

	if (typed_for == &cw_sending && (event->keyval == GDK_KEY_Return
			|| event->keyval == GDK_KEY_KP_Enter))
		type_a_character(' ');
	else if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
		save_typed();
	else if (event->keyval == GDK_KEY_Escape && typed_for == &cw_sending)
		save_typed();
	else if (event->keyval == GDK_KEY_Escape)
		show_page(PAGE_SETTINGS);
	else if (event->keyval == GDK_KEY_BackSpace)
		rub_out();
	else if (character >= ' ' && character <= '~')
		type_a_character((char)character);
	return TRUE;
}

// in CW a key struck is sent: the radio is told the character
static gboolean on_key_in_cw(const GdkEventKey *event) {
	char text[2] = { 0, 0 };
	guint32 letter = gdk_keyval_to_unicode(event->keyval);

	if (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))
		return FALSE;
	if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter
			|| event->keyval == GDK_KEY_space) {
		send_line(CW_TEXT_CONTROL, CW_TEXT_SPACE);
		return TRUE;
	}
	if (letter <= ' ' || letter >= 127 || letter == '_')
		return FALSE;
	text[0] = (char)letter;
	send_line(CW_TEXT_CONTROL, text);
	return TRUE;
}

static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer unused) {
	(void)widget;
	(void)unused;
	if (is_page_shown(PAGE_TEXT))
		return on_key_while_typing(event);
	if (!is_page_shown(PAGE_MAIN))
		return FALSE;
	if (event->keyval == GDK_KEY_Up) {
		step_focused(1);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Down) {
		step_focused(-1);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Tab) {
		move_focus(1);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_ISO_Left_Tab) {
		move_focus(-1);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Escape) {
		if (!strcmp(mode_group(), MACRO_GROUP))
			send_line(CW_DO_CONTROL, "ESC");
		focus = ON_FREQUENCY;
		show_values();
		return TRUE;
	}
	if (!strcmp(mode_group(), MACRO_GROUP))
		return on_key_in_cw(event);
	return FALSE;
}

static int row_of(const char *control) {
	int i;

	for (i = 0; i < row_count; i++) {
		if (!strcmp(rows[i].control, control))
			return i;
	}
	return NOT_FOUND;
}

// the knobs' thread: both knobs looked at, and the button, which counts as
// pressed once when it has been down for BUTTON_DOWN_LOOKS looks running
static void *look_at_knobs(void *unused) {
	int down_for = 0;

	(void)unused;
	while (1) {
		atomic_fetch_add(&tuning_notches, knob_read(&tuning_knob));
		atomic_fetch_add(&control_notches, knob_read(&control_knob));
		if (gpio_read(control_button_pin) == 0)
			down_for++;
		else
			down_for = 0;
		if (down_for == BUTTON_DOWN_LOOKS)
			atomic_fetch_add(&control_presses, 1);
		usleep(KNOBS_LOOKED_AT_EVERY_US);
	}
	return NULL;
}

// what the knobs have counted since the last time is acted on
static gboolean take_up_knobs(gpointer unused) {
	int tuned = atomic_exchange(&tuning_notches, 0);
	int turned = atomic_exchange(&control_notches, 0);
	int pressed = atomic_exchange(&control_presses, 0);
	int row = focus;

	(void)unused;
	if (tuned != 0)
		tune(tuned);
	// while the logbook is open the knob that works the controls works it
	if (logbook_window_is_open()) {
		logbook_window_turned(turned);
		while (pressed > 0) {
			logbook_window_pressed();
			pressed--;
		}
		return G_SOURCE_CONTINUE;
	}
	if (row == ON_FREQUENCY)
		row = row_of(VOLUME_CONTROL);
	if (turned != 0 && row != NOT_FOUND)
		step_row(row, turned);
	while (pressed > 0) {
		if (marked_message != NULL)
			answer_marked();
		else
			move_focus(1);
		pressed--;
	}
	return G_SOURCE_CONTINUE;
}

// the knobs of the radio the window is on, if it is on one that has them
static void open_knobs(void) {
	const struct board *board = board_named_in(HW_SETTINGS_FILE);
	const struct radio_knobs *knobs = radios_with_knobs;
	pthread_t thread;

	if (board == NULL)
		return;
	while (knobs->radio != NULL && strcmp(knobs->radio, board->radio))
		knobs++;
	if (knobs->radio == NULL)
		return;
	if (gpio_open() < 0)
		return;
	if (knob_open(&tuning_knob, knobs->tuning.a_pin, knobs->tuning.b_pin) < 0)
		return;
	if (knob_open(&control_knob, knobs->control.a_pin, knobs->control.b_pin) < 0)
		return;
	control_button_pin = knobs->control.button_pin;
	if (gpio_make_input(control_button_pin, GPIO_PULL_UP) < 0)
		return;
	if (pthread_create(&thread, NULL, look_at_knobs, NULL) != 0)
		return;
	g_timeout_add(KNOBS_TAKEN_UP_EVERY_MS, take_up_knobs, NULL);
}

// where the receiver listens, in Hz from the middle of the spectrum, which
// is where the radio's oscillator puts the tuned frequency. In CW the
// oscillator is set off by the pitch, so that a signal on the tuned
// frequency lies the pitch away from the middle
static void passband(int *low_hz, int *high_hz, int *tuned_hz) {
	const char *mode = value_of("MODE");
	int pitch = atoi(value_of("PITCH"));
	int bw_cw = atoi(value_of("BW_CW"));
	int bw_voice = atoi(value_of("BW_VOICE"));

	*tuned_hz = 0;
	*low_hz = VOICE_LOW_HZ;
	*high_hz = VOICE_LOW_HZ + bw_voice;
	if (!strcmp(mode, "LSB")) {
		*low_hz = -VOICE_LOW_HZ - bw_voice;
		*high_hz = -VOICE_LOW_HZ;
	}
	if (!strcmp(mode, "AM")) {
		*low_hz = -bw_voice;
		*high_hz = bw_voice;
	}
	if (!strcmp(mode, "FT8")) {
		*tuned_hz = atoi(value_of(TX_PITCH_CONTROL));
		*low_hz = FT8_LOW_HZ;
		*high_hz = FT8_HIGH_HZ;
	}
	if (!strcmp(mode, "CW")) {
		*tuned_hz = pitch;
		*low_hz = pitch - bw_cw / 2;
		*high_hz = pitch + bw_cw / 2;
	}
	if (!strcmp(mode, "CWR")) {
		*tuned_hz = -pitch;
		*low_hz = -pitch - bw_cw / 2;
		*high_hz = -pitch + bw_cw / 2;
	}
}

// how many Hz across the graph and the waterfall show: the span chosen,
// "10K" or "2.5K", or all the radio sends if that is less
static int shown_hz(void) {
	const char *span = value_of("SPAN");
	int chosen = (int)(atof(span) * HZ_PER_K);

	if (chosen <= 0 || chosen > span_hz)
		return span_hz;
	return chosen;
}

// what the graph and the waterfall show, in Hz from the dial: the span
// chosen, the dial in the middle; in FT8 its passband and no more, the dial
// at the left edge, whatever the span
static void shown_range(int *low_hz, int *high_hz) {
	*low_hz = -shown_hz() / 2;
	*high_hz = shown_hz() / 2;
	if (!strcmp(mode_group(), MESSAGES_GROUP) && span_hz / 2 >= FT8_HIGH_HZ) {
		*low_hz = 0;
		*high_hz = FT8_HIGH_HZ;
	}
}

// the points of the radio's line that are in what is shown: how many, and
// the first of them
static int shown_points(int *first) {
	int low_hz;
	int high_hz;
	int count;

	shown_range(&low_hz, &high_hz);
	count = (int)((double)point_count * (high_hz - low_hz) / span_hz);
	if (count < 2)
		count = 2;
	if (count > point_count)
		count = point_count;
	*first = point_count / 2 + (int)((double)point_count * low_hz / span_hz);
	if (*first < 0)
		*first = 0;
	if (*first > point_count - count)
		*first = point_count - count;
	return count;
}

static double x_of_hz(int hz, int width) {
	int low_hz;
	int high_hz;

	shown_range(&low_hz, &high_hz);
	return (double)(hz - low_hz) * width / (high_hz - low_hz);
}

static int hz_of_x(double x, int width) {
	int low_hz;
	int high_hz;

	shown_range(&low_hz, &high_hz);
	return low_hz + (int)(x * (high_hz - low_hz) / width);
}

static void draw_graph(cairo_t *cr, int width, int height) {
	int low_hz;
	int high_hz;
	int tuned_hz;
	double x;
	double y;
	int first;
	int count;
	int i;

	cairo_set_source_rgb(cr, 0.0, 0.05, 0.1);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	if (point_count < 2 || span_hz == 0)
		return;

	passband(&low_hz, &high_hz, &tuned_hz);
	cairo_set_source_rgba(cr, 0.4, 0.4, 0.5, 0.35);
	x = x_of_hz(low_hz, width);
	cairo_rectangle(cr, x, 0, x_of_hz(high_hz, width) - x, height);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, 1.0, 0.9, 0.0);
	cairo_set_line_width(cr, 1.0);
	count = shown_points(&first);
	for (i = 0; i < count; i++) {
		x = (double)i * width / (count - 1);
		y = height - (double)points[first + i] * height / FULL_SCALE_DB;
		if (y < 0)
			y = 0;
		if (i == 0)
			cairo_move_to(cr, x, y);
		else
			cairo_line_to(cr, x, y);
	}
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 1.0, 0.3, 0.3);
	x = x_of_hz(tuned_hz, width);
	cairo_move_to(cr, x, 0);
	cairo_line_to(cr, x, height);
	cairo_stroke(cr);
}

// the envelope of what is being sent, each point a line as far above the
// middle as below it
static void draw_modulation(cairo_t *cr, int width, int height) {
	double middle = height / 2.0;
	double reach;
	double x;
	int i;

	cairo_set_source_rgb(cr, 0.0, 0.05, 0.1);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.2, 0.25, 0.3);
	cairo_set_line_width(cr, 1.0);
	cairo_move_to(cr, 0, middle);
	cairo_line_to(cr, width, middle);
	cairo_stroke(cr);
	if (modulation_count < 1)
		return;
	cairo_set_source_rgb(cr, 1.0, 0.9, 0.0);
	for (i = 0; i < width; i++) {
		x = i + 0.5;
		reach = modulation[(i * modulation_count) / width] * middle / MODULATION_FULL_SCALE;
		cairo_move_to(cr, x, middle - reach - 0.5);
		cairo_line_to(cr, x, middle + reach + 0.5);
	}
	cairo_stroke(cr);
}

// how far across the dial a strength is, from 0 at its left end to 1 at
// its right: S9 is SMETER_S9_AT of the way, and the rest is the dB over it
static double smeter_place(int strength) {
	double units = strength / SMETER_PER_UNIT + (strength % SMETER_PER_UNIT) / DB_PER_S_UNIT;
	double over_db;

	if (units <= S_UNITS_MOST)
		return units / S_UNITS_MOST * SMETER_S9_AT;
	over_db = (units - S_UNITS_MOST) * DB_PER_S_UNIT;
	if (over_db > SMETER_OVER_MOST_DB)
		over_db = SMETER_OVER_MOST_DB;
	return SMETER_S9_AT + over_db / SMETER_OVER_MOST_DB * (1.0 - SMETER_S9_AT);
}

// a place on the dial as an angle for cairo, which counts from three
// o'clock: the middle of the dial is straight up
static double smeter_angle(double place) {
	return -G_PI / 2 - SMETER_SWEEP + 2 * SMETER_SWEEP * place;
}

// a line along the dial's radius at an angle, from one distance out from
// the pivot to another
static void draw_smeter_ray(cairo_t *cr, double x, double y, double angle, double from,
		double to) {
	cairo_move_to(cr, x + from * cos(angle), y + from * sin(angle));
	cairo_line_to(cr, x + to * cos(angle), y + to * sin(angle));
	cairo_stroke(cr);
}

// a mark on the scale at an S unit: a tick outward from the arc, and its
// figure over the tick
static void draw_smeter_mark(cairo_t *cr, double x, double y, double radius, int units) {
	double angle = smeter_angle((double)units / S_UNITS_MOST * SMETER_S9_AT);
	double outer = radius * (1.0 + SMETER_TICK);
	double lettered = outer + radius * SMETER_MARK_ROOM;
	cairo_text_extents_t size;
	char text[TEXT_MAX];

	draw_smeter_ray(cr, x, y, angle, radius, outer);
	snprintf(text, sizeof(text), "%d", units);
	cairo_text_extents(cr, text, &size);
	cairo_move_to(cr, x + lettered * cos(angle) - size.width / 2 - size.x_bearing,
		y + lettered * sin(angle));
	cairo_show_text(cr, text);
}

// a mark on the power's scale, with its figure if it has one
static void draw_power_mark(cairo_t *cr, double x, double y, double radius, double place,
		const char *text) {
	double angle = smeter_angle(place);
	double outer = radius * (1.0 + SMETER_TICK);
	double lettered = outer + radius * SMETER_MARK_ROOM;
	double left;
	cairo_text_extents_t size;

	draw_smeter_ray(cr, x, y, angle, radius, outer);
	if (text == NULL)
		return;
	cairo_text_extents(cr, text, &size);
	// a figure at the scale's end is kept inside the dial's edge
	left = x + lettered * cos(angle) - size.width / 2;
	if (left < POWER_MARK_EDGE)
		left = POWER_MARK_EDGE;
	if (left + size.width > 2 * x - POWER_MARK_EDGE)
		left = 2 * x - POWER_MARK_EDGE - size.width;
	cairo_move_to(cr, left - size.x_bearing, y + lettered * sin(angle));
	cairo_show_text(cr, text);
}

// under the dial, over the needle's foot: the power and the SWR in figures,
// in the middle, "25W 1.3", and under them the SWR as a bar
static const double swr_zone_colours[SWR_ZONES][3] = {
	{0.19, 0.82, 0.38}, {1.0, 0.88, 0.0}, {1.0, 0.25, 0.2},
};

static void draw_power_and_swr(cairo_t *cr, int width, int height) {
	int power = atoi(value_of("POWER"));
	int swr_tenths = atoi(value_of("SWR"));
	double swr = swr_tenths / (double)TENTHS;
	double bar = height * SWR_BAR_HEIGHT;
	double left = width * SWR_BAR_MARGIN;
	double across = width * (1.0 - 2 * SWR_BAR_MARGIN);
	double top = height * SWR_BAR_TOP;
	double filled;
	double from;
	double lit;
	cairo_text_extents_t size;
	char text[TEXT_MAX];
	int used;
	int zone;

	cairo_set_source_rgb(cr, 0.063, 0.094, 0.125);
	cairo_rectangle(cr, 1, height * METER_FOOT_TOP, width - 2,
		height * (1.0 - METER_FOOT_TOP) - 1);
	cairo_fill(cr);

	if (power < POWER_WHOLE_FROM)
		used = snprintf(text, sizeof(text), "%d.%dW", power / TENTHS, power % TENTHS);
	else
		used = snprintf(text, sizeof(text), "%dW", (power + TENTHS / 2) / TENTHS);
	if (swr_tenths >= TENTHS)
		snprintf(text + used, sizeof(text) - used, " %d.%d", swr_tenths / TENTHS,
			swr_tenths % TENTHS);
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, height * METER_FIGURES_SIZE);
	cairo_text_extents(cr, text, &size);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_move_to(cr, (width - size.width) / 2 - size.x_bearing,
		height * METER_FIGURES_AT - size.height / 2 - size.y_bearing);
	cairo_show_text(cr, text);

	// the bar is in three parts, each a third of it: nought to 1.5, green,
	// 1.5 to 3, yellow, and from 3, red, the author's; each is dim until
	// the SWR has reached into it, and bright as far as it has. A matched
	// aerial, at 1, fills two thirds of the green
	filled = swr / SWR_BAR_FULL;
	if (filled > 1)
		filled = 1;
	for (zone = 0; zone < SWR_ZONES; zone++) {
		from = (double)zone / SWR_ZONES;
		lit = filled - from;
		if (lit < 0)
			lit = 0;
		if (lit > 1.0 / SWR_ZONES)
			lit = 1.0 / SWR_ZONES;
		cairo_set_source_rgb(cr, swr_zone_colours[zone][0] * SWR_DIM,
			swr_zone_colours[zone][1] * SWR_DIM, swr_zone_colours[zone][2] * SWR_DIM);
		cairo_rectangle(cr, left + across * from, top, across / SWR_ZONES - 1, bar);
		cairo_fill(cr);
		cairo_set_source_rgb(cr, swr_zone_colours[zone][0], swr_zone_colours[zone][1],
			swr_zone_colours[zone][2]);
		cairo_rectangle(cr, left + across * from, top,
			lit >= 1.0 / SWR_ZONES ? across * lit - 1 : across * lit, bar);
		cairo_fill(cr);
	}
}

// the meter's place on the top row: a dial with a needle. While the radio
// receives it is the signal's strength, its scale an arc with S9 seven
// tenths across and red beyond. While it sends it is the power, nought to
// a hundred watts, with the SWR as a bar under it.
// The arc is as wide as the place allows and its radius follows from that
static gboolean on_draw_meter(GtkWidget *widget, cairo_t *cr, gpointer unused) {
	int width = gtk_widget_get_allocated_width(widget);
	int height = gtk_widget_get_allocated_height(widget);
	double radius = width * SMETER_ARC_SHARE / (2 * sin(SMETER_SWEEP));
	double x = width / 2.0;
	double y = height * SMETER_ARC_TOP + radius;
	double angle = smeter_angle(smeter_place(atoi(value_of("SMETER"))));
	double watts = atoi(value_of("POWER")) / (double)TENTHS;
	int is_sending = is_on("TX");
	int units;

	(void)unused;
	cairo_set_source_rgb(cr, 0.063, 0.094, 0.125);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.25, 0.31, 0.38);
	cairo_set_line_width(cr, 1.0);
	cairo_rectangle(cr, 0.5, 0.5, width - 1, height - 1);
	cairo_stroke(cr);
	cairo_rectangle(cr, 1, 1, width - 2, height - 2);
	cairo_clip(cr);

	cairo_set_line_width(cr, px(1));
	cairo_set_source_rgb(cr, 0.85, 0.88, 0.9);
	cairo_set_font_size(cr, radius * SMETER_MARK_SIZE);
	if (is_sending) {
		cairo_arc(cr, x, y, radius, smeter_angle(0), smeter_angle(1));
		cairo_stroke(cr);
		draw_power_mark(cr, x, y, radius, 0, "0");
		draw_power_mark(cr, x, y, radius, 0.25, NULL);
		draw_power_mark(cr, x, y, radius, 0.5, "50");
		draw_power_mark(cr, x, y, radius, 0.75, NULL);
		draw_power_mark(cr, x, y, radius, 1, "100");
		if (watts > POWER_DIAL_WATTS)
			watts = POWER_DIAL_WATTS;
		angle = smeter_angle(watts / POWER_DIAL_WATTS);
	}
	else {
		cairo_arc(cr, x, y, radius, smeter_angle(0), smeter_angle(SMETER_S9_AT));
		cairo_stroke(cr);
		for (units = 1; units <= S_UNITS_MOST; units += 2)
			draw_smeter_mark(cr, x, y, radius, units);
		cairo_set_source_rgb(cr, 1.0, 0.25, 0.2);
		cairo_arc(cr, x, y, radius, smeter_angle(SMETER_S9_AT), smeter_angle(1));
		cairo_stroke(cr);
		draw_smeter_ray(cr, x, y, smeter_angle(1), radius, radius * (1.0 + SMETER_TICK));
	}

	cairo_set_source_rgb(cr, 1.0, 0.9, 0.0);
	draw_smeter_ray(cr, x, y, angle, radius * SMETER_NEEDLE_FROM,
		radius * (1.0 + SMETER_TICK));
	if (is_sending)
		draw_power_and_swr(cr, width, height);
	return TRUE;
}

// in CW the pitch, where a station tuned in is heard, and in FT8 the pitch
// we send at, is carried down the waterfall from the graph, dotted, so that
// the station right under it is not hidden
static void draw_waterfall(cairo_t *cr, int width, int height) {
	const char *mode = value_of("MODE");
	const double dashes[] = { PITCH_DASH, PITCH_DASH };
	int low_hz;
	int high_hz;
	int tuned_hz;
	int first;
	int count;
	double x;

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	if (point_count < 2 || span_hz == 0)
		return;
	count = shown_points(&first);
	cairo_save(cr);
	cairo_scale(cr, (double)width / count, (double)height / FALL_ROWS);
	cairo_set_source_surface(cr, fall, -first, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
	cairo_rectangle(cr, 0, 0, count, FALL_ROWS);
	cairo_fill(cr);
	cairo_restore(cr);
	if (span_hz == 0 || (strcmp(mode, "CW") && strcmp(mode, "CWR") && strcmp(mode, "FT8")))
		return;
	passband(&low_hz, &high_hz, &tuned_hz);
	x = x_of_hz(tuned_hz, width);
	cairo_save(cr);
	cairo_set_source_rgb(cr, 1.0, 0.3, 0.3);
	cairo_set_line_width(cr, 1.0);
	cairo_set_dash(cr, dashes, 2, 0);
	cairo_move_to(cr, x, 0);
	cairo_line_to(cr, x, height);
	cairo_stroke(cr);
	cairo_restore(cr);
}

static gboolean on_draw_spectrum(GtkWidget *widget, cairo_t *cr, gpointer unused) {
	int width = gtk_widget_get_allocated_width(widget);
	int height = gtk_widget_get_allocated_height(widget);
	int graph_height = px_down(GRAPH_HEIGHT);

	(void)unused;
	if (is_modulation_shown) {
		draw_modulation(cr, width, graph_height);
	} else {
		draw_graph(cr, width, graph_height);
		cairo_set_source_rgb(cr, 0.56, 0.63, 0.69);
		cairo_set_font_size(cr, px(CORNER_SIZE));
		cairo_move_to(cr, px(SPAN_LEFT), px(SPAN_BASELINE));
		// FT8 shows its passband whatever the span, so the span is not named
		if (strcmp(mode_group(), MESSAGES_GROUP))
			cairo_show_text(cr, value_of("SPAN"));
	}
	cairo_translate(cr, 0, graph_height);
	draw_waterfall(cr, width, height - graph_height);
	return TRUE;
}

static int has_row(const char *control) {
	int i;

	for (i = 0; i < row_count; i++) {
		if (!strcmp(rows[i].control, control))
			return 1;
	}
	return 0;
}

static void place(const struct placed *it) {
	gtk_widget_set_size_request(it->widget,
		px_across(it->x + it->width) - px_across(it->x),
		px_down(it->y + it->height) - px_down(it->y));
	gtk_layout_move(GTK_LAYOUT(screen), it->widget, px_across(it->x), px_down(it->y));
}

// a widget that has gone is forgotten
static void on_placed_destroyed(GtkWidget *widget, gpointer unused) {
	int i;

	(void)unused;
	for (i = 0; i < PLACED_MAX; i++) {
		if (placed[i].widget == widget)
			placed[i].widget = NULL;
	}
}

// a widget is put on the screen at a place in the panel's plan
static void put(GtkWidget *widget, int x, int y, int width, int height) {
	struct placed *it = placed;

	while (it < placed + PLACED_MAX && it->widget != NULL)
		it++;
	gtk_layout_put(GTK_LAYOUT(screen), widget, 0, 0);
	if (it == placed + PLACED_MAX)
		return;
	it->widget = widget;
	it->x = x;
	it->y = y;
	it->width = width;
	it->height = height;
	g_signal_connect(widget, "destroy", G_CALLBACK(on_placed_destroyed), NULL);
	place(it);
}

// a widget already on the screen is given another place in the plan
static void put_again(GtkWidget *widget, int x, int y, int width, int height) {
	struct placed *it;

	for (it = placed; it < placed + PLACED_MAX; it++) {
		if (it->widget != widget)
			continue;
		it->x = x;
		it->y = y;
		it->width = width;
		it->height = height;
		place(it);
		return;
	}
}

static void add_label_class(GtkWidget *label, const char *name) {
	gtk_style_context_add_class(gtk_widget_get_style_context(label), name);
}

// a tile: a box with something in it, that takes clicks and the wheel
static GtkWidget *tile_new(GtkWidget *inside) {
	GtkWidget *box = gtk_event_box_new();

	gtk_style_context_add_class(gtk_widget_get_style_context(box), "tile");
	gtk_container_add(GTK_CONTAINER(box), inside);
	gtk_widget_add_events(box, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK);
	return box;
}

// a button: a tile with a label alone
static GtkWidget *button_new(const char *text, GtkWidget **label) {
	*label = gtk_label_new(text);
	add_label_class(*label, "tile-value");
	return tile_new(*label);
}

// a control's tile, as on the panel: its name small in the top left corner
// and its value under it
static GtkWidget *control_tile_new(const char *name, GtkWidget **value) {
	GtkWidget *inside = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *title = gtk_label_new(name);

	*value = gtk_label_new("");
	gtk_widget_set_margin_top(title, px(TITLE_ROOM_ABOVE));
	add_label_class(title, "tile-title");
	add_label_class(*value, "tile-value");
	gtk_box_pack_start(GTK_BOX(inside), title, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(inside), *value, TRUE, TRUE, 0);
	return tile_new(inside);
}

// a row of the list: the control's name at the left and its value at the
// right
static GtkWidget *list_row_new(const char *name, GtkWidget **value) {
	GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *title = gtk_label_new(name);

	*value = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_label_set_xalign(GTK_LABEL(*value), 1);
	add_label_class(title, "tile-title");
	add_label_class(*value, "tile-value");
	gtk_widget_set_margin_start(line, px(ROW_MARGIN_SIDE));
	gtk_widget_set_margin_end(line, px(ROW_MARGIN_SIDE));
	gtk_box_pack_start(GTK_BOX(line), title, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(line), *value, FALSE, FALSE, 0);
	return tile_new(line);
}

static const struct top_place *place_of(const struct top_place *places, const char *control) {
	const struct top_place *p;

	for (p = places; p->control != NULL; p++) {
		if (!strcmp(p->control, control))
			return p;
	}
	return NULL;
}

static void add_row(const char *control, enum place place) {
	const struct top_place *where = place_of(top_places, control);
	GtkWidget *box;
	GtkWidget *value;
	int index = row_count;

	if (row_count == KNOWN_MAX || has_row(control))
		return;
	if (place == IN_LIST) {
		box = list_row_new(label_of(control), &value);
		gtk_widget_set_size_request(box, -1, px_down(ROW_HEIGHT));
		gtk_box_pack_start(GTK_BOX(list), box, FALSE, FALSE, 0);
	}
	else {
		box = control_tile_new(label_of(control), &value);
		put(box, where->x, where->y, TILE_WIDTH, ROW_HEIGHT);
	}
	gtk_widget_set_no_show_all(box, TRUE);
	gtk_widget_show_all(gtk_bin_get_child(GTK_BIN(box)));
	g_signal_connect(box, "scroll-event", G_CALLBACK(on_row_wheel), GINT_TO_POINTER(index));
	tell_taps(box, G_CALLBACK(on_row_tap), GINT_TO_POINTER(index));
	snprintf(rows[index].control, CONTROL_NAME_MAX, "%s", control);
	rows[index].place = place;
	rows[index].box = box;
	rows[index].value_label = value;
	row_count++;
}

// what is never a row of the list: what has a place or a button of its own,
// and the two bandwidths, which BW stands for
static int is_in_list(const struct known *k) {
	// TX is nowhere in the list: the voice modes and 2TONE have their
	// button for it, and CW and FT8 key themselves
	static const char *const elsewhere[] = {
		"FREQ", "BAND", "BW_VOICE", "BW_CW", "TX", NULL,
	};
	int i;

	if (!k->is_listed && !k->is_own)
		return 0;
	for (i = 0; elsewhere[i] != NULL; i++) {
		if (!strcmp(k->name, elsewhere[i]))
			return 0;
	}
	return 1;
}

// the rows, made again each time the radio sends its schema: those with a
// place of their own, at their places; and in the list, BW and the modems'
// own, of which only the mode in use's are shown, and after them the rest
// of the radio's own. The order of CW's is the author's: what is touched
// often is at the top, BW, the speed and the pitch, and what is set once
// and left is under it
static void make_list(void) {
	static const char *const first_in_cw[] = {
		"WPM", "PITCH", "SIDETONE", "CW_DELAY", "CW_INPUT", NULL,
	};
	const struct top_place *p;
	struct known *k;
	int i;

	for (i = 0; i < row_count; i++)
		gtk_widget_destroy(rows[i].box);
	row_count = 0;
	focus = ON_FREQUENCY;
	for (p = top_places; p->control != NULL; p++)
		add_row(p->control, ON_TOP);
	add_row("BW", IN_LIST);
	for (i = 0; first_in_cw[i] != NULL; i++) {
		k = find_known(first_in_cw[i]);
		if (k != NULL && is_in_list(k))
			add_row(k->name, IN_LIST);
	}
	for (i = 0; i < known_count; i++) {
		if (known[i].group[0] != 0 && strcmp(known[i].group, "RADIO")
				&& strcmp(known[i].group, "WINDOW") && is_in_list(&known[i]))
			add_row(known[i].name, IN_LIST);
	}
	for (i = 0; i < known_count; i++) {
		if (!strcmp(known[i].group, "RADIO") && is_in_list(&known[i]))
			add_row(known[i].name, IN_LIST);
	}
	show_values();
}

// the panel's colours; the sizes of the letters go with the scale
// the frequency's letters are the size the plan gives them, unless its
// longest line would then be wider than its box, as it would in a window
// much taller for its width than the radio's screen; then they are as
// large as fits, and the waiting line is smaller by as much
static int frequency_px(int size) {
	double fits = px_across(FREQ_WIDTH) / (FREQUENCY_LETTERS * LETTER_WIDTH_SHARE);

	if (px(FREQUENCY_SIZE) <= fits)
		return px(size);
	return (int)(fits * size / FREQUENCY_SIZE);
}

static void load_style(void) {
	char rules[CSS_MAX];

	if (css == NULL) {
		css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}
	snprintf(rules, sizeof(rules),
		"window { background-color: #000000; color: #FFFFFF; }"
		".tile { background-color: #101820; box-shadow: inset 0 0 0 1px #405060; }"
		".tile.in-use { background-color: #604800; }"
		".tile.on { background-color: #B02020; }"
		".tile.focused { box-shadow: inset 0 0 0 %dpx #FFC000; }"
		".tile-title { font-size: %dpx; color: #00FFFF; }"
		".dial-waiting { font-family: monospace; font-size: %dpx; font-weight: bold;"
		" color: #00FFFF; }"
		".status.qso { color: #FFFFFF; background-color: #B02020; }"
		".status.warning { font-family: sans-serif; font-weight: bold; color: #FFE000;"
		" background-color: #000000; }"
		".status.meter { color: #FFC000; background-color: #000000; }"
		".tile-value { font-size: %dpx; font-weight: bold; color: #FFFFFF; }"
		".dim .tile-value { color: #506070; }"
		".frequency { font-family: monospace; font-size: %dpx; font-weight: bold;"
		" color: #FFFFFF; }"
		".status { font-size: %dpx; color: #90A0B0; }"
		".message { font-family: monospace; font-size: %dpx; color: #FFFFFF; }"
		".message.sent { color: #FF6060; }"
		".message.calling { color: #60E060; }"
		".message.to-us { color: #FFE000; }"
		".message-row { background-color: #101820; }"
		".message-row.other-slot { background-color: #1C2834; }"
		".message-row.marked { background-color: #604800; }"
		".key { font-size: %dpx; font-weight: bold; color: #FFFFFF;"
		" background-image: none; background-color: #101820;"
		" border: 1px solid #405060; border-radius: 0; box-shadow: none; }"
		".key:active { background-color: #604800; }"
		".typed { font-family: monospace; font-size: %dpx; font-weight: bold;"
		" color: #FFFFFF; background-color: #101820;"
		" box-shadow: inset 0 0 0 %dpx #FFC000; }",
		px(1), px(TITLE_SIZE), frequency_px(WAITING_SIZE), px(VALUE_SIZE),
		frequency_px(FREQUENCY_SIZE),
		px(STATUS_SIZE),
		px(MESSAGE_SIZE), px(VALUE_SIZE), px(TYPED_SIZE), px(1));
	gtk_css_provider_load_from_data(css, rules, -1, NULL);
}

// the plan is stretched to the size the window has now: everything is put
// again and the letters sized again. It is done when the window is next
// idle and not while GTK is giving out the new size, when a widget moved
// or sized is not seen to until the size changes once more: the buttons
// then stayed as they were when the window was maximised, and took the
// maximised size when it was restored.
// the wheel's box is square on the screen. The plan is stretched across
// and down by different amounts to fill the window, so a box square in the
// plan is not: on the sbitx it came out wider than tall, and the wheel,
// drawn to its width, was cut off above and below. The box is as wide as
// the list it lies over, and as tall as that is on the screen, or as tall
// as the list and then no wider than that
static void place_wheel(void) {
	double across = WHEEL_SIZE;
	double down = WHEEL_SIZE * scale_across / scale_down;

	if (down > PANEL_HEIGHT - WHEEL_TOP) {
		down = PANEL_HEIGHT - WHEEL_TOP;
		across = down * scale_down / scale_across;
	}
	put_again(wheel_area, WHEEL_LEFT, WHEEL_TOP, (int)(across + 0.5), (int)(down + 0.5));
}

static gboolean lay_out_again(gpointer unused) {
	int i;

	(void)unused;
	is_lay_out_due = 0;
	scale_across = (double)laid_out_width / PANEL_WIDTH;
	scale_down = (double)laid_out_height / PANEL_HEIGHT;
	scale = scale_down;
	if (scale_across < scale)
		scale = scale_across;
	load_style();
	place_wheel();
	for (i = 0; i < PLACED_MAX; i++) {
		if (placed[i].widget != NULL)
			place(&placed[i]);
	}
	for (i = 0; i < row_count; i++) {
		if (rows[i].place == IN_LIST)
			gtk_widget_set_size_request(rows[i].box, -1, px_down(ROW_HEIGHT));
	}
	return G_SOURCE_REMOVE;
}

// the window has a new size
static void on_screen_sized(GtkWidget *widget, GdkRectangle *size, gpointer unused) {
	(void)widget;
	(void)unused;
	if (size->width == laid_out_width && size->height == laid_out_height)
		return;
	laid_out_width = size->width;
	laid_out_height = size->height;
	if (!is_lay_out_due)
		g_idle_add(lay_out_again, NULL);
	is_lay_out_due = 1;
}

// the frequency's box: the VFO in waiting over the figures
static void create_frequency(void) {
	GtkWidget *lines = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	// the two lines are one column in the middle of the box, as wide as
	// the large line, and both begin at its left edge, so that what the
	// small line is, "B:", stands right over what the large one is, "A:"
	vfo_label = gtk_label_new("");
	frequency_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(vfo_label), 0);
	gtk_label_set_xalign(GTK_LABEL(frequency_label), 0);
	add_label_class(vfo_label, "dial-waiting");
	add_label_class(frequency_label, "frequency");
	gtk_box_pack_start(GTK_BOX(lines), vfo_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(lines), frequency_label, FALSE, FALSE, 0);
	gtk_widget_set_halign(lines, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(lines, GTK_ALIGN_CENTER);

	frequency_box = tile_new(lines);
	g_signal_connect(frequency_box, "scroll-event", G_CALLBACK(on_frequency_wheel), NULL);
	g_signal_connect(frequency_box, "button-press-event",
		G_CALLBACK(on_frequency_click), NULL);
	put(frequency_box, FREQ_LEFT, 0, FREQ_WIDTH, ROW_HEIGHT);
	dial_area = gtk_drawing_area_new();
	g_signal_connect(dial_area, "draw", G_CALLBACK(on_draw_meter), NULL);
	put(dial_area, DIAL_LEFT, 0, TILE_WIDTH, ROW_HEIGHT);
}

// the buttons that are always there: the bands, the column and the bottom
// row's
static void create_buttons(void) {
	GtkWidget *label;
	int i;

	for (i = 0; i < BANDS; i++) {
		band_box[i] = button_new(band_names[i], &label);
		g_signal_connect(band_box[i], "button-press-event", G_CALLBACK(on_band_click),
			GINT_TO_POINTER(i));
		put(band_box[i], i * TILE_WIDTH, BANDS_TOP, TILE_WIDTH, ROW_HEIGHT);
	}
	for (i = 0; i < COLUMN_BUTTONS; i++) {
		column_box[i] = button_new(column_names[i], &column_label[i]);
		gtk_widget_set_no_show_all(column_box[i], TRUE);
		gtk_widget_show(column_label[i]);
		g_signal_connect(column_box[i], "button-press-event",
			G_CALLBACK(on_column_click), GINT_TO_POINTER(i));
		put(column_box[i], 0, MIDDLE_TOP + i * ROW_HEIGHT, TILE_WIDTH, ROW_HEIGHT);
	}
	for (i = 0; i < FOOTER_TILES; i++) {
		footer_box[i] = button_new("", &footer_label[i]);
		gtk_widget_set_no_show_all(footer_box[i], TRUE);
		gtk_widget_show(footer_label[i]);
		g_signal_connect(footer_box[i], "button-press-event",
			G_CALLBACK(on_footer_click), GINT_TO_POINTER(i));
		put(footer_box[i], i * TILE_WIDTH, FOOTER_TOP, TILE_WIDTH, ROW_HEIGHT);
	}
}

// the status line, and under it the graph and the waterfall
static void create_middle(void) {
	status_label = gtk_label_new("");
	meter_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(status_label), 0);
	gtk_label_set_xalign(GTK_LABEL(meter_label), 1);
	gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_END);
	add_label_class(status_label, "status");
	add_label_class(meter_label, "status");
	// the status line is the whole width; the meter is laid over its right
	// end, on a ground of its own, only while there is power to show
	put(status_label, GRAPH_LEFT, MIDDLE_TOP, GRAPH_WIDTH, STATUS_HEIGHT);
	add_label_class(meter_label, "meter");
	gtk_widget_set_no_show_all(meter_label, TRUE);
	put(meter_label, GRAPH_LEFT + GRAPH_WIDTH - METER_WIDTH, MIDDLE_TOP, METER_WIDTH,
		STATUS_HEIGHT);

	spectrum_area = gtk_drawing_area_new();
	gtk_widget_add_events(spectrum_area, GDK_SCROLL_MASK);
	g_signal_connect(spectrum_area, "draw", G_CALLBACK(on_draw_spectrum), NULL);
	g_signal_connect(spectrum_area, "scroll-event", G_CALLBACK(on_frequency_wheel), NULL);
	tell_taps(spectrum_area, G_CALLBACK(on_spectrum_tap), NULL);
	put(spectrum_area, GRAPH_LEFT, MIDDLE_TOP + STATUS_HEIGHT, GRAPH_WIDTH,
		GRAPH_HEIGHT + FALL_HEIGHT);

	// FT8's messages, in the same place and on down to the foot
	messages = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	messages_scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(messages_scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(messages_scroller),
		FALSE);
	gtk_container_add(GTK_CONTAINER(messages_scroller), messages);
	gtk_widget_set_no_show_all(messages_scroller, TRUE);
	gtk_widget_show_all(gtk_bin_get_child(GTK_BIN(messages_scroller)));
	g_signal_connect(messages, "size-allocate", G_CALLBACK(on_messages_sized), NULL);
	put(messages_scroller, GRAPH_LEFT, MIDDLE_TOP + STATUS_HEIGHT, MESSAGES_WIDTH,
		PANEL_HEIGHT - MIDDLE_TOP - STATUS_HEIGHT);
}

static void create_list(void) {
	list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	list_scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	// the scroll bar has a strip of its own beside the rows, and is not
	// drawn over the values at their right ends
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(list_scroller), FALSE);
	gtk_container_add(GTK_CONTAINER(list_scroller), list);
	put(list_scroller, LIST_LEFT, MIDDLE_TOP, LIST_WIDTH, LIST_HEIGHT);
}

static GtkWidget *key_new(const char *text, GCallback pressed, gpointer data) {
	GtkWidget *key = gtk_button_new_with_label(text);

	gtk_style_context_add_class(gtk_widget_get_style_context(key), "key");
	gtk_widget_set_can_focus(key, FALSE);
	gtk_widget_set_hexpand(key, TRUE);
	gtk_widget_set_vexpand(key, TRUE);
	g_signal_connect(key, "clicked", pressed, data);
	return key;
}

// the top of a screen that is not the main one: the arrow that goes back,
// and what the screen is
static GtkWidget *create_top_bar(const char *back_to, GtkWidget **title) {
	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *back = key_new("\u2190", G_CALLBACK(on_back_pressed), (gpointer)back_to);

	*title = gtk_label_new("");
	add_label_class(*title, "tile-value");
	gtk_widget_set_hexpand(back, FALSE);
	gtk_widget_set_vexpand(back, FALSE);
	gtk_widget_set_size_request(back, px_across(TILE_WIDTH), px_down(ROW_HEIGHT));
	gtk_box_pack_start(GTK_BOX(bar), back, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), *title, TRUE, TRUE, 0);
	return bar;
}

// a screen that is a list of rows under a bar with its name and the arrow
// that goes back: the settings, and the connections under them
static GtkWidget *create_list_page(const char *name, const char *back_to, GtkWidget **list) {
	GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget *title;

	gtk_box_pack_start(GTK_BOX(page), create_top_bar(back_to, &title), FALSE, FALSE, 0);
	gtk_label_set_text(GTK_LABEL(title), name);
	*list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
		GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroller), FALSE);
	gtk_container_add(GTK_CONTAINER(scroller), *list);
	gtk_box_pack_start(GTK_BOX(page), scroller, TRUE, TRUE, 0);
	return page;
}

// the screen a text is typed on: what is typed in the upper part, and in
// the lower part the keyboard, which has no more keys than are needed
static GtkWidget *create_text_page(void) {
	static const char *const key_rows[KEY_ROWS] = {
		"1234567890", "QWERTYUIOP", "ASDFGHJKL/", "ZXCVBNM.?-",
	};
	GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *keys = gtk_grid_new();
	char text[2] = "";
	int column = 0;
	int row;
	int i;

	gtk_box_pack_start(GTK_BOX(page), create_top_bar(PAGE_SETTINGS, &text_title), FALSE,
		FALSE, 0);
	typed_label = gtk_label_new("");
	add_label_class(typed_label, "typed");
	gtk_widget_set_size_request(typed_label, -1, px_down(ROW_HEIGHT));
	gtk_box_pack_start(GTK_BOX(page), typed_label, FALSE, FALSE, 0);

	gtk_grid_set_row_homogeneous(GTK_GRID(keys), TRUE);
	gtk_grid_set_column_homogeneous(GTK_GRID(keys), TRUE);
	for (row = 0; row < KEY_ROWS; row++) {
		for (i = 0; i < KEYS_A_ROW; i++) {
			text[0] = key_rows[row][i];
			gtk_grid_attach(GTK_GRID(keys), key_new(text, G_CALLBACK(on_key_pressed),
				GINT_TO_POINTER(text[0])), i, row, 1, 1);
		}
	}
	cancel_key = key_new("CANCEL", G_CALLBACK(on_back_pressed), PAGE_SETTINGS);
	gtk_grid_attach(GTK_GRID(keys), cancel_key, column, KEY_ROWS, CANCEL_KEYS, 1);
	column += CANCEL_KEYS;
	gtk_grid_attach(GTK_GRID(keys), key_new("SPACE", G_CALLBACK(on_key_pressed),
		GINT_TO_POINTER(' ')), column, KEY_ROWS, SPACE_KEYS, 1);
	column += SPACE_KEYS;
	gtk_grid_attach(GTK_GRID(keys), key_new("\u232B", G_CALLBACK(on_rub_out_pressed),
		NULL), column, KEY_ROWS, RUB_OUT_KEYS, 1);
	column += RUB_OUT_KEYS;
	save_key = key_new("SAVE", G_CALLBACK(on_save_pressed), NULL);
	gtk_grid_attach(GTK_GRID(keys), save_key, column, KEY_ROWS, SAVE_KEYS, 1);
	gtk_box_pack_end(GTK_BOX(page), keys, TRUE, TRUE, 0);
	return page;
}

// the tuning wheel's box, made last so that it lies over the list of
// controls, and not shown until the frequency is tapped
static void create_wheel(void) {
	GtkGesture *drag;

	wheel_area = gtk_drawing_area_new();
	gtk_widget_add_events(wheel_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK | GDK_TOUCH_MASK);
	g_signal_connect(wheel_area, "draw", G_CALLBACK(on_draw_wheel), NULL);
	drag = gtk_gesture_drag_new(wheel_area);
	g_signal_connect(drag, "drag-begin", G_CALLBACK(on_wheel_held), NULL);
	g_signal_connect(drag, "drag-update", G_CALLBACK(on_wheel_turned), NULL);
	g_signal_connect(drag, "drag-end", G_CALLBACK(on_wheel_let_go), NULL);
	g_object_set_data_full(G_OBJECT(wheel_area), "drag", drag, g_object_unref);
	gtk_widget_set_no_show_all(wheel_area, TRUE);
	put(wheel_area, WHEEL_LEFT, WHEEL_TOP, WHEEL_SIZE, WHEEL_SIZE);
}

static GtkWidget *create_window(void) {
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	main_window = window;

	load_style();
	gtk_window_set_title(GTK_WINDOW(window), WINDOW_NAME);
	gtk_window_set_default_size(GTK_WINDOW(window), WINDOW_WIDTH, WINDOW_HEIGHT);
	gtk_window_maximize(GTK_WINDOW(window));
	screen = gtk_layout_new(NULL, NULL);
	create_frequency();
	create_buttons();
	create_middle();
	create_list();
	create_wheel();

	pages = gtk_stack_new();
	gtk_stack_add_named(GTK_STACK(pages), screen, PAGE_MAIN);
	gtk_stack_add_named(GTK_STACK(pages),
		create_list_page("SETTINGS", PAGE_MAIN, &settings_list), PAGE_SETTINGS);
	gtk_stack_add_named(GTK_STACK(pages),
		create_list_page("CONNECTIONS", PAGE_SETTINGS, &connections_list), PAGE_CONNECTIONS);
	gtk_stack_add_named(GTK_STACK(pages), create_text_page(), PAGE_TEXT);
	gtk_container_add(GTK_CONTAINER(window), pages);

	g_signal_connect(screen, "size-allocate", G_CALLBACK(on_screen_sized), NULL);
	g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	return window;
}

int main(int argc, char **argv) {
	GtkWidget *window;

	gtk_init(&argc, &argv);
	if (argc > 1 && !strcmp(argv[1], REMOTE_WORD)) {
		if (argc < 3) {
			printf("zbitx_gui remote <radio, as vu2ese-home> [server] [port]\n");
			return 1;
		}
		is_remote = 1;
		through_wanted = argv[2];
		radio_name = DEFAULT_SERVER;
		if (argc > 3)
			radio_name = argv[3];
		if (argc > 4)
			radio_port = atoi(argv[4]);
	} else {
		if (argc > 1)
			radio_name = argv[1];
		if (argc > 2)
			radio_port = atoi(argv[2]);
	}

	fall = cairo_image_surface_create(CAIRO_FORMAT_RGB24, POINTS_MAX, FALL_ROWS);
	add_own_controls();
	window = create_window();
	gtk_widget_show_all(window);
	show_values();

	g_timeout_add(CLOCK_SHOWN_EVERY_MS, show_clock, NULL);
	open_knobs();
	connector = g_socket_client_new();
	try_to_connect();
	gtk_main();
	return 0;
}
