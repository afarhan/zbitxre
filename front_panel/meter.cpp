// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// meter.cpp — the forward power, the SWR and the battery, read off the
// zbitx's SWR bridge and its supply, so that the operator knows how well
// the radio is transmitting.
//
// It is the first zbitx panel's measure_voltages(), from
// zbitx_front_panel_v2, which the author asked to have back. The bridge's
// forward and reflected voltages come to A0 and A1, and the battery's,
// through a divider, to A2. They are read every 50 ms, with the Pico's
// ADC at its 10 bits, and scaled as that panel scaled them:
//
//   forward, reflected   56 × reading / 460
//   battery              500 × reading / 278, in hundredths of a volt
//
// The bridge's two are held at their peaks and let fall slowly, as a
// needle would: a reading higher than the one held is taken at once, and a
// lower one is averaged in, one part to ten of what is held. Speech and CW
// go up and down far faster than the eye can follow, and what the operator
// wants is the peak.
//
// From them, as that panel worked them out:
//
//   power   (3 + forward / 2)² / 10, in tenths of a watt; the 3 is the
//           voltage the bridge's diode does not pass
//   SWR     10 × (forward + reflected) / (forward − reflected), in tenths
//
// That panel halved its held forward voltage, for the power, in the same
// variable that held the peak, so that each reading was compared with the
// halved one; here the peak is kept as it was read and halved only for
// the power. That panel also divided by nothing when the two were equal,
// as they are when nothing is sent; here there is no SWR while there is no
// forward voltage, and 10.0 when the reflected is as large as the forward,
// as zbitxv2's sbitx said for its own bridge.
//
// What is measured goes to the radio five times a second, each value only
// when it has changed: LINK_POWER, LINK_SWR and LINK_BATTERY. The battery
// goes only when it has moved a tenth of a volt: its reading wanders by a
// few hundredths from one look to the next, and the link should carry as
// little as it can. And while
// there is power, it is shown at the right of the status line, with the
// SWR, "8.4 W  SWR 1.3"; main.cpp shows it.

#include <Arduino.h>
#include "link.h"
#include "ui.h"

#define FORWARD_PIN A0
#define REFLECTED_PIN A1
#define BATTERY_PIN A2
#define ADC_BITS 10
#define READ_EVERY_MS 50
#define SEND_EVERY_MS 200
#define BRIDGE_TIMES 56
#define BRIDGE_OVER 460
#define BATTERY_TIMES 500
#define BATTERY_OVER 278
#define HELD_PARTS 10
#define DIODE_DROP 3
#define HALVED 2
#define POWER_OVER 10
#define TENTHS 10
#define HUNDREDTHS 100
#define SWR_NONE 0
#define SWR_TOO_HIGH 100
#define METER_TEXT_MAX 32
#define NEVER_SENT (-1)
#define BATTERY_MOVED_HUNDREDTHS 10

//what is sent, each the last value the radio was given
enum sent_value {
	SENT_POWER,
	SENT_SWR,
	SENT_BATTERY,
	SENT_VALUES,
};

static int forward;
static int reflected;
static int32_t power_tenths;
static int32_t swr_tenths;
static int32_t battery_hundredths;
static int32_t sent[SENT_VALUES] = { NEVER_SENT, NEVER_SENT, NEVER_SENT };
static unsigned long read_at;
static unsigned long send_at;

static int held(int was, int now){
	if (now > was)
		return now;
	return (was * HELD_PARTS + now) / (HELD_PARTS + 1);
}

static void measure(){
	int f = BRIDGE_TIMES * analogRead(FORWARD_PIN) / BRIDGE_OVER;
	int r = BRIDGE_TIMES * analogRead(REFLECTED_PIN) / BRIDGE_OVER;
	int volts;

	battery_hundredths = BATTERY_TIMES * analogRead(BATTERY_PIN) / BATTERY_OVER;
	forward = held(forward, f);
	reflected = held(reflected, r);
	volts = DIODE_DROP + forward / HALVED;
	power_tenths = volts * volts / POWER_OVER;
	if (forward == 0)
		power_tenths = 0;
	if (forward == 0)
		swr_tenths = SWR_NONE;
	else if (reflected >= forward)
		swr_tenths = SWR_TOO_HIGH;
	else
		swr_tenths = TENTHS * (forward + reflected) / (forward - reflected);
}

static void send_if_changed(int which, int control, int32_t value){
	if (sent[which] == value)
		return;
	sent[which] = value;
	i2c_link_changed(control, value);
}

static void show(){
	static char shown[METER_TEXT_MAX];
	char text[METER_TEXT_MAX];

	text[0] = 0;
	if (power_tenths > 0)
		snprintf(text, sizeof(text), "%d.%d W  SWR %d.%d", (int)(power_tenths / TENTHS),
			(int)(power_tenths % TENTHS), (int)(swr_tenths / TENTHS),
			(int)(swr_tenths % TENTHS));
	if (!strcmp(text, shown))
		return;
	snprintf(shown, sizeof(shown), "%s", text);
	main_meter(shown);
}

void meter_init(){
	analogReadResolution(ADC_BITS);
}

//from loop()
void meter_slice(){
	unsigned long now = millis();

	if (now - read_at >= READ_EVERY_MS){
		read_at = now;
		measure();
		show();
	}
	if (now - send_at >= SEND_EVERY_MS){
		send_at = now;
		send_if_changed(SENT_POWER, LINK_POWER, power_tenths);
		send_if_changed(SENT_SWR, LINK_SWR, swr_tenths);
		if (sent[SENT_BATTERY] == NEVER_SENT
				|| abs(battery_hundredths - sent[SENT_BATTERY]) >= BATTERY_MOVED_HUNDREDTHS)
			send_if_changed(SENT_BATTERY, LINK_BATTERY, battery_hundredths);
	}
}

//for the serial port's "meter": what is read and what is worked out
void meter_print(){
	Serial.printf("meter: forward %d, reflected %d, power %d.%d W, swr %d.%d, "
		"battery %d.%02d V\n", forward, reflected, (int)(power_tenths / TENTHS),
		(int)(power_tenths % TENTHS), (int)(swr_tenths / TENTHS), (int)(swr_tenths % TENTHS),
		(int)(battery_hundredths / HUNDREDTHS), (int)(battery_hundredths % HUNDREDTHS));
}
