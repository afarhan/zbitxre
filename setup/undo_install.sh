#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# undo_install.sh — puts back the program a radio had before install.sh.
#
#     ~/zbitxre/setup/undo_install.sh
#
# The old program in ~/sbitx was never changed. This starts it with the
# desktop again in place of zbitxre, and on a zbitx puts back the boot
# configuration as it was, since the old program drives the clock chip's
# I2C pins itself and the kernel must not have them. ~/zbitxre and its
# data/ are left, so install.sh can put zbitxre back.

say() { echo "zbitxre: $*"; }

autostart=$HOME/.config/autostart
rm -f "$autostart/zbitxre.desktop"
for kept in "$autostart"/*.desktop.disabled; do
	[ -f "$kept" ] || continue
	mv "$kept" "${kept%.disabled}"
	say "the old program starts with the desktop again ($(basename "${kept%.disabled}"))"
done

for config in /boot/config.txt /boot/firmware/config.txt; do
	if [ -f "$config.before-zbitxre" ]; then
		sudo cp "$config.before-zbitxre" "$config" && say "$config is as it was"
	fi
done

pgrep -x sdr > /dev/null && echo quit > /tmp/sdr.in
pkill -x zbitx_gui
pkill -x zbitx_remote
say "restart the radio for the old program to start: sudo reboot"
