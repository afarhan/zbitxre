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
# the last line, short, which the radio program puts in the update's last
# step, "DONE: now 38ec5ba; panel 6b8501c, as before": the author found on
# 19 October 2026 that an update said nothing of the panel's firmware. The
# panel shows 47 characters of a step, so it is kept to 25
outcome() { echo "outcome: $*"; }

release=release/panel
# the firmware for the cable is GitHub's, where every build comes from and
# nowhere else, the author's ruling of 18 October 2026; the Upgrade page
# that shows how is the service's, my.sbitx.net unless the settings say
# SERVER= another
SERVER=$(sed -n 's/^SERVER=//p' data/user_settings.ini 2>/dev/null | tail -1)
SERVER=${SERVER:-my.sbitx.net}
UF2_ADDRESS=https://raw.githubusercontent.com/afarhan/zbitxre/main/release/panel/front_panel.uf2
[ -f "$release/VERSION" ] || exit 0
grep -q '^radio=sbitx' data/hw_settings.ini 2>/dev/null && exit 0
# --from-radio: the radio program runs this itself as it starts after an
# update, before it opens the panel, and is not in the way
if [ "$1" != "--from-radio" ] && pgrep -x sdr > /dev/null; then
	say "the radio program is running; the panel is left as it is"
	exit 1
fi
if ! make --quiet panel_update; then
	outcome "panel update not built"
	exit 1
fi

want=$(cat "$release/VERSION")
# panel_update ends with OLDER_PANEL when the panel's firmware is from
# before updates over I2C; anything else that fails is said as it is, and
# not taken for an old panel, as it was until 17 October 2026
OLDER_PANEL=3
have=$(./panel_update --version 2>/tmp/panel_update.err)
asked=$?
if [ "$asked" = "$OLDER_PANEL" ]; then
	say "the front panel has its older firmware, which is updated with the USB cable, once,"
	say "as https://$SERVER/upgrade shows:"
	say "  1. Switch the radio off. Unplug any cable from the micro-USB port marked CAT:"
	say "     plugged in, it keeps the panel powered, and the panel does not restart."
	say "  2. Hold the tuning knob in while switching the radio on, then let go."
	say "  3. With a USB cable, connect the port marked CAT (not the one marked USB) to a"
	say "     computer. A drive called RPI-RP2 appears."
	say "  4. Copy onto it $UF2_ADDRESS, downloaded on that computer."
	say "  5. Unplug the cable, switch the radio off, and switch it back on."
	say "  Future updates of zbitxre will update the front panel automatically."
	outcome "panel needs the USB cable"
	exit 0
fi
if [ "$asked" != 0 ] || [ -z "$have" ]; then
	say "the panel's firmware could not be asked: $(head -1 /tmp/panel_update.err)"
	outcome "panel did not answer"
	exit 1
fi
if [ "$have" = "$want" ]; then
	say "the front panel's firmware, $have, is up to date"
	outcome "panel $have, as before"
	exit 0
fi
say "the front panel has $have; giving it $want, which takes under a minute"
if ! ./panel_update "$release/front_panel.bin.gz"; then
	outcome "panel update failed"
	exit 1
fi
outcome "panel now $want"
