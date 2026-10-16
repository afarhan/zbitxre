#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# panel_firmware.sh — brings a zbitx's front panel to the firmware in
# release/panel, over I2C, when it has another.
#
#     setup/panel_firmware.sh
#
# The author's plan of 16 October 2026: the release of that day goes on the
# panel with the USB cable, as owners are used to, and every later one over
# the I2C bus, by this, from update.sh and install.sh. The radio program
# must not be running: it polls the panel on the same bus.
#
# release/panel has what `make panel-release` puts there: the firmware
# gzipped for I2C, the same as a .uf2 for the cable, and VERSION, the
# version the panel will say it has. A panel that does not answer the update
# at all has older firmware, the one it came with; then this says how to put
# the .uf2 on it with the cable, once, and changes nothing. An sbitx has no
# such panel, and this does nothing there.

cd "$(dirname "$0")/.." || exit 1

say() { echo "panel: $*"; }

release=release/panel
UF2_ADDRESS=https://my.sbitx.net/zbitx/front_panel.uf2
[ -f "$release/VERSION" ] || exit 0
grep -q '^radio=sbitx' data/hw_settings.ini 2>/dev/null && exit 0
if pgrep -x sdr > /dev/null; then
	say "the radio program is running; the panel is left as it is"
	exit 1
fi
make --quiet panel_update || exit 1

want=$(cat "$release/VERSION")
have=$(./panel_update --version 2>/dev/null)
if [ -z "$have" ]; then
	say "the front panel has its older firmware, which is updated with the USB cable, once:"
	say "  1. With the radio off, hold the Pico's BOOTSEL button while plugging its USB"
	say "     cable into a computer, as for earlier updates of the panel."
	say "  2. A drive called RPI-RP2 appears on the computer. Copy onto it the file"
	say "     $UF2_ADDRESS"
	say "     downloaded on that computer."
	say "  The panel restarts with it, and later updates come over I2C by themselves."
	exit 0
fi
if [ "$have" = "$want" ]; then
	say "the front panel's firmware, $have, is up to date"
	exit 0
fi
say "the front panel has $have; giving it $want, which takes under a minute"
./panel_update "$release/front_panel.bin.gz"
