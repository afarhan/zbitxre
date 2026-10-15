// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem_voip.c — the mode VOIP: the owner speaks over the internet, to the
// radios tuned where this one is, and the transmitter is not keyed.
//
// WHAT THE AUTHOR ASKED FOR
//
// The dial is the room, and VoIP is one more way of speaking in it: "we just
// add VOIP as an option". With the switch VOIP online, among the radio's
// CONNECTIONS, another's speech over the internet is heard in any voice
// mode. To speak, the owner puts the radio in this mode, on the same dial,
// and presses PTT: the microphone then goes to my.sbitx.net, and to every
// radio there whose dial rounds to the same kilohertz. The same PTT is used
// as for the air, and what decides is the mode; the author ruled out a
// switch that "arms" the PTT, which could be left armed and forgotten, or
// thought armed when it was not and the finals keyed with no antenna.
//
// SO IT HAS NO TX
//
// A modem without tx() does not transmit, and the core refuses "TX ON" in
// its mode: that is what keeps the transmitter off in VOIP, whatever a
// client asks. The PTT does not ask, in this mode: the radio program
// follows it as VOIP_TX instead of TX, and rtp_voip.c sends the
// microphone to the gateway while VOIP_TX is ON. tools/sdr.c does that,
// with the rest of VoIP's controls, since hearing another works in every
// voice mode and not only in this one.
//
// WHAT IS HEARD
//
// The receiver, as in USB or LSB, on the band's sideband, the lower below
// 10 MHz, as RADE's modes are: the owner hears the band, and someone
// speaking over VoIP takes its place in the speaker while they speak. Its
// passband is the voice modes', BW_VOICE.

#include <stdint.h>

#include "sdr.h"
#include "control.h"
#include "modem.h"

#define VOICE_LOW_HZ 300

static int32_t baseband_output[MAX_BINS/2];

static void rx_voip(complex float *bins) {
	demod_ssb(bins, baseband_output);
	speaker_play(baseband_output, MAX_BINS/2);
}

static void voip_select(void) {
	set_passband(VOICE_LOW_HZ, VOICE_LOW_HZ + control_get_int("BW_VOICE"));
}

const struct modem modem_voip = {
	.name = "VOIP",
	.order = 10,
	.select = voip_select,
	.rx = rx_voip,
};
