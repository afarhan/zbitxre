#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# panel_firmware.sh — brings a zbitx's front panel to the firmware in
# release/panel, over I2C, when it has another.
#
#     setup/panel_firmware.sh
#     setup/panel_firmware.sh --from-radio
#
# The author's plan of 16 October 2026: the release of that day goes on the
# panel with the USB cable, as owners are used to, and every later one over
# the I2C bus, by this, from install.sh and from the radio program as it
# starts after an update. The radio program must not have the panel open:
# it polls the panel on the same bus. Run by hand it will not go on while
# the radio program runs; the radio program runs it with --from-radio
# before it opens the panel.
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
# --from-radio: the radio program runs this itself as it starts after an
# update, before it opens the panel, and is not in the way
if [ "$1" != "--from-radio" ] && pgrep -x sdr > /dev/null; then
	say "the radio program is running; the panel is left as it is"
	exit 1
fi
make --quiet panel_update || exit 1

want=$(cat "$release/VERSION")
# panel_update ends with OLDER_PANEL when the panel's firmware is from
# before updates over I2C; anything else that fails is said as it is, and
# not taken for an old panel, as it was until 17 October 2026
OLDER_PANEL=3
have=$(./panel_update --version 2>/tmp/panel_update.err)
asked=$?
if [ "$asked" = "$OLDER_PANEL" ]; then
	say "the front panel has its older firmware, which is updated with the USB cable, once,"
	say "as https://my.sbitx.net/upgrade shows:"
	say "  1. Switch the radio off. Hold the tuning knob in while switching it on, then let go."
	say "  2. Plug the panel's USB cable into a computer. A drive called RPI-RP2 appears."
	say "  3. Copy onto it $UF2_ADDRESS, downloaded on that computer."
	say "  The panel restarts with it, and later updates come over I2C by themselves."
	exit 0
fi
if [ "$asked" != 0 ] || [ -z "$have" ]; then
	say "the panel's firmware could not be asked: $(head -1 /tmp/panel_update.err)"
	exit 1
fi
if [ "$have" = "$want" ]; then
	say "the front panel's firmware, $have, is up to date"
	exit 0
fi
say "the front panel has $have; giving it $want, which takes under a minute"
./panel_update "$release/front_panel.bin.gz"
