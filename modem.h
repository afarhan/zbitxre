// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// modem.h — what a modem is.
//
// A modem is what makes one mode of the radio: USB, CW, AM, FT8, FreeDV. Each
// is a file of its own in modems/, and each defines one or more struct modem,
// named modem_ and the mode, as modem_usb or modem_cw:
//
//     const struct modem modem_cw = {
//         .name = "CW",
//         .order = 3,
//         .rx = rx_cw,
//     };
//
// Nothing else has to be done to add a mode. The build gathers every file in
// modems/, and makes the list of modems, modem_list, from the definitions it
// finds in them. The list is written to build/modem_list.c, which can be
// read like any other file.
//
//   name     the mode, as sdr_request() is asked for it: r1:mode=CW.
//   order    where the mode comes in a list shown to the operator. The list
//            of modems is in the order of the files' names; order is for a
//            user interface to sort by.
//   start    is called once, when the radio starts. It registers the
//            modem's own controls, in a group of its own; control.c says how.
//            It may be left out.
//   select   is called when its mode is chosen, by the thread that owns the
//            controls, once the mode is set in the core. It sets the
//            receiver as the mode wants it, its passband above all, with
//            sdr_request(). The mode is set in the core once more after
//            it; radio.c says why. It may be left out.
//   reset    is called when the modem is put to use, and whenever the signal
//            breaks off and starts again. It clears what the modem remembers.
//            It may be left out.
//   rx       is called by the audio thread once for each block from the sound
//            card, about every 10 ms, with the block's frequency bins. It
//            makes sound of them with the primitives in sdr.h, demod_ssb(),
//            demod_am() and speaker_play(), and does whatever else the mode
//            does. It must be done within the block: a modem whose work takes
//            longer, as decoding FT8 does, hands its samples to a thread of
//            its own and returns.
//
// Several modes may share a file and its functions, as USB and LSB do.

#pragma once

#include <complex.h>
#include <fftw3.h>

struct modem {
	const char *name;
	int order;
	void (*start)(void);
	void (*select)(void);
	void (*reset)(void);
	void (*rx)(fftw_complex *bins);
};

extern const struct modem *modem_list[];

const struct modem *modem_find(const char *name);

