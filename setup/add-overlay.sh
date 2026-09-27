#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# add-overlay.sh - gives the Si5351 and the clock chip to the kernel.
#
# Adds one line to the boot configuration, unless it is there already, and
# keeps a copy of the file as it was. The line makes a kernel I2C bus of
# GPIO 13 and GPIO 6 and loads the kernel's driver for the DS3231 clock chip.
# setup/README.md says what that does and why.
#
#     sudo setup/add-overlay.sh                 changes /boot/config.txt
#     setup/add-overlay.sh some/other/file      changes that file instead
#
# Reboot afterwards. Nothing changes until the Pi starts again.

LINE='dtoverlay=i2c-rtc-gpio,ds3231,i2c_gpio_sda=13,i2c_gpio_scl=6'
CONFIG="${1:-/boot/config.txt}"

if [ ! -f "$CONFIG" ]; then
	echo "there is no $CONFIG"
	exit 1
fi

if grep -q "^$LINE\$" "$CONFIG"; then
	echo "$CONFIG already has the line; nothing changed"
	exit 0
fi

if grep -q '^dtoverlay=i2c-gpio' "$CONFIG"; then
	echo "$CONFIG has an i2c-gpio overlay of its own."
	echo "Remove that line first: two overlays cannot share the same pins."
	exit 1
fi

if ! cp "$CONFIG" "$CONFIG.before-zbitxre"; then
	echo "could not keep a copy of $CONFIG; nothing changed"
	exit 1
fi

{
	echo
	echo "# the Si5351 and the DS3231 clock chip on a kernel I2C bus;"
	echo "# the kernel keeps the clock (zbitxre)"
	echo "$LINE"
} >> "$CONFIG"

echo "added to $CONFIG; the file as it was is $CONFIG.before-zbitxre"
echo "reboot for it to take effect"
