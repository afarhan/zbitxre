#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# install.sh — puts zbitxre on a zbitx or an sbitx that runs the software it
# came with, ours or JJ's, in ~/sbitx. Run on the radio, as its user, with
# the one line the user manual gives:
#
#     curl -fsSL https://my.sbitx.net/install | bash
#
# The author's plan of 16 October 2026: an owner upgrades either with a new
# SD card or with this one line, and gets on my.sbitx.net quickly after.
#
# WHAT IT DOES, IN ORDER, ASKING BEFORE ANYTHING CHANGES
#
#  1. Finds out which radio this is. A Raspberry Pi Zero 2 W is a zbitx. A
#     Raspberry Pi 4 is an sbitx, and zbitxre supports only the third
#     version, the one with an ATtiny85 on the I2C bus at 0x08, which the old
#     program looked for in the same way; an older sbitx is told so, and
#     nothing is changed.
#  2. Closes the old program, once the owner says it is not transmitting.
#  3. Installs what building needs, with apt.
#  4. Fetches zbitxre into ~/zbitxre, or brings an earlier copy up to date,
#     fetches RADE's weights, and builds the radio program, the window and
#     the gateway to my.sbitx.net. On a Pi Zero 2 W that takes a while.
#  5. Takes the radio's own files from ~/sbitx/data, unless zbitxre has its
#     own already:
#       hw_settings.ini, the radio's calibration, as it is, with the radio's
#         name put at the top when it has none (zbitxre reads a zbitx's old
#         hw=4 as well);
#       user_settings.ini, whose names are not zbitxre's, turned into
#         zbitxre's for what carries over: the callsign, the grid, the
#         frequency and the mode, the levels, and the keyer's and the
#         passbands' settings; the rest belonged to the old screen;
#       sbitx.db, the logbook, which is the same in both.
#  6. On a zbitx, adds the line to the boot configuration that gives the
#     clock chip's I2C pins to the kernel (add-overlay.sh, setup/README.md).
#  7. Starts zbitxre with the desktop in place of the old program, whose
#     start file is renamed and kept.
#  8. Asks whether to put the radio on my.sbitx.net. If so, it asks for the
#     callsign and the grid when the old settings had none, turns REMOTE on,
#     makes the radio's cookie, and prints the address that adds the radio
#     with the cookie already typed in.
#  9. Asks to restart the radio.
#
# ~/sbitx is not changed, and setup/undo_install.sh puts the old program
# back. Run again, this brings zbitxre up to date and changes nothing that
# is already done.
#
# ZBITXRE_REPO names another place to fetch zbitxre from, for trying this
# before the repository is public. ZBITXRE_SKIP_SUDO=1 is for trying it
# where nobody is there to type a password: what apt installs is taken to
# be there, and a step that would need sudo stops it instead.

REPO=${ZBITXRE_REPO:-https://github.com/afarhan/zbitxre.git}
DEST=$HOME/zbitxre
OLD=$HOME/sbitx/data
SITE=https://my.sbitx.net
COOKIE_LETTERS=abcdefghjkmnpqrstuvwxyz

say() { echo "zbitxre: $*"; }
stop() { echo "zbitxre: $*"; echo "zbitxre: nothing more was changed."; exit 1; }

# the script itself comes down the pipe, so answers are read from the terminal
ask() {
	local answer
	read -r -p "zbitxre: $1 " answer < /dev/tty || answer=""
	echo "$answer"
}
is_yes() { case "$1" in [yY]*) return 0 ;; *) return 1 ;; esac }

[ "$(id -u)" -ne 0 ] || stop "run this as the radio's user, not as root."
if [ -n "$ZBITXRE_SKIP_SUDO" ]; then
	sudo() { stop "sudo $* is needed here, and ZBITXRE_SKIP_SUDO is set."; }
else
	sudo -v || stop "this needs sudo, for apt and the boot configuration."
fi

# 1. which radio
model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null)
case "$model" in
*"Zero 2 W"*) radio=zbitx ;;
*"Raspberry Pi 4"*) radio=sbitxv3 ;;
*) stop "this is a $model; zbitxre runs on a zbitx or an sbitx v3." ;;
esac
say "this is a $model, so a $radio."

# 2. the old program, closed
if pgrep -x sbitx > /dev/null; then
	is_yes "$(ask "The radio program that is running will be closed. Is it receiving, not transmitting? [y/N]")" \
		|| stop "close it yourself when it is not transmitting, and run this again."
	pkill -x sbitx
	sleep 2
fi

# 1, again: an sbitx v3 has its ATtiny85, which can be asked only once the
# old program has let go of the bus
if [ "$radio" = sbitxv3 ]; then
	command -v i2cdetect > /dev/null || sudo apt-get install -y i2c-tools > /dev/null
	si5351=""
	attiny=""
	for bus in /dev/i2c-*; do
		[ -e "$bus" ] || continue
		n=${bus#/dev/i2c-}
		if sudo i2cdetect -y -r "$n" 0x60 0x60 2>/dev/null | grep -q " 60"; then
			si5351=$n
			sudo i2cdetect -y -r "$n" 0x08 0x08 2>/dev/null | grep -q " 08" && attiny=$n
		fi
	done
	if [ -n "$attiny" ]; then
		say "the sbitx's ATtiny85 answered on I2C bus $attiny."
	elif [ -n "$si5351" ]; then
		stop "no ATtiny85 answered beside the Si5351: this is an sbitx older than the third version, which zbitxre does not support yet."
	else
		# the system has no kernel bus on the Si5351's pins, so nothing
		# could be asked
		is_yes "$(ask "Is this an sbitx of the third version, with the power and SWR meter? [y/N]")" \
			|| stop "zbitxre supports the zbitx and the sbitx's third version only."
		stop "this sbitx's system has no I2C bus on GPIO 13 and 6, where the Si5351 is. The user manual says which line to add to its boot configuration; then run this again."
	fi
fi

# 3. what building needs
if [ -z "$ZBITXRE_SKIP_SUDO" ]; then
	say "installing what is needed to build it"
	sudo apt-get update -q > /dev/null
	sudo apt-get install -y -q git gcc make pkg-config curl i2c-tools \
		libasound2-dev libsqlite3-dev libgtk-3-dev > /dev/null \
		|| stop "apt could not install what is needed."
fi

# 4. zbitxre, fetched and built
if [ -d "$DEST/.git" ]; then
	say "bringing $DEST up to date"
	git -C "$DEST" pull --ff-only --quiet || stop "$DEST has changes of its own; 'git status' there says what."
else
	say "fetching zbitxre into $DEST"
	git clone --quiet "$REPO" "$DEST" || stop "zbitxre could not be fetched from $REPO."
fi
cd "$DEST" || exit 1
setup/fetch_rade_weights.sh > /dev/null || stop "RADE's weights could not be fetched."
say "building; on a zbitx this takes a while"
make --quiet sdr || stop "the radio program did not build."
make --quiet gui || stop "the window did not build."
make --quiet remote || stop "the gateway to my.sbitx.net did not build."

# a zbitx's front panel: over I2C when its firmware can take it, or how to
# put this release's on it with the USB cable, once
[ "$radio" = zbitx ] && setup/panel_firmware.sh

# 5. the radio's own files
mkdir -p data
if [ ! -f data/hw_settings.ini ]; then
	[ -f "$OLD/hw_settings.ini" ] || stop "there is no $OLD/hw_settings.ini, the radio's calibration."
	if grep -q '^radio=' "$OLD/hw_settings.ini" || grep -q '^hw *= *4' "$OLD/hw_settings.ini"; then
		cp "$OLD/hw_settings.ini" data/hw_settings.ini
	else
		{ echo "radio=$radio"; cat "$OLD/hw_settings.ini"; } > data/hw_settings.ini
	fi
	say "took the radio's calibration from $OLD/hw_settings.ini"
fi
if [ ! -f data/user_settings.ini ] && [ -f "$OLD/user_settings.ini" ]; then
	# the old names, and zbitxre's for each; the radio passes over a value
	# it will not take
	awk -F= '
	BEGIN {
		n["#mycallsign"] = "MYCALLSIGN"; n["#mygrid"] = "MYGRID"
		n["r1:freq"] = "FREQ"; n["r1:mode"] = "MODE"
		n["r1:volume"] = "AUDIO"; n["r1:gain"] = "IF"; n["r1:agc"] = "AGC"
		n["tx_power"] = "DRIVE"; n["tx_gain"] = "MIC"
		n["#tx_wpm"] = "WPM"; n["rx_pitch"] = "PITCH"; n["sidetone"] = "SIDETONE"
		n["#tx_pitch"] = "TX_PITCH"; n["#bw_voice"] = "BW_VOICE"; n["#bw_cw"] = "BW_CW"
	}
	{
		key = $1; gsub(/ /, "", key)
		value = substr($0, index($0, "=") + 1)
		if ((key in n) && value != "") print n[key] "=" value
	}' "$OLD/user_settings.ini" > data/user_settings.ini
	say "took $(grep -c = data/user_settings.ini) settings from $OLD/user_settings.ini"
fi
if [ ! -f data/sbitx.db ] && [ -f "$OLD/sbitx.db" ]; then
	cp "$OLD/sbitx.db" data/sbitx.db
	say "took the logbook from $OLD/sbitx.db"
fi
[ -f data/cw.mc ] || cp setup/cw.mc data/cw.mc

# 6. the boot configuration of a zbitx
restart=no
if [ "$radio" = zbitx ]; then
	config=/boot/config.txt
	[ -f /boot/firmware/config.txt ] && config=/boot/firmware/config.txt
	if ! grep -q '^dtoverlay=i2c-rtc-gpio' "$config"; then
		sudo setup/add-overlay.sh "$config" || stop "the boot configuration could not be changed."
		restart=yes
	fi
fi

# 7. started with the desktop in place of the old program
mkdir -p "$HOME/.config/autostart"
for old in "$HOME"/.config/autostart/*.desktop; do
	[ -f "$old" ] || continue
	[ "$(basename "$old")" = zbitxre.desktop ] && continue
	if grep -q 'sbitx' "$old"; then
		mv "$old" "$old.disabled"
		say "the old program no longer starts with the desktop ($(basename "$old").disabled)"
	fi
done
sed "s|/home/pi/zbitxre|$DEST|" setup/zbitxre.desktop > "$HOME/.config/autostart/zbitxre.desktop"

# 8. my.sbitx.net
setting() { sed -n "s/^$1=//p" data/user_settings.ini 2>/dev/null | tail -1; }
put_setting() {
	touch data/user_settings.ini
	sed -i "/^$1=/d" data/user_settings.ini
	echo "$1=$2" >> data/user_settings.ini
}
if is_yes "$(ask "Put this radio on my.sbitx.net, to operate it from a browser or a phone? [Y/n]")y"; then
	callsign=$(setting MYCALLSIGN)
	while [ -z "$callsign" ]; do
		callsign=$(ask "Your callsign:" | tr 'a-z' 'A-Z' | tr -cd 'A-Z0-9/')
	done
	put_setting MYCALLSIGN "$callsign"
	if [ -z "$(setting MYGRID)" ]; then
		grid=$(ask "Your grid locator, such as MK97fj, to be on the map (Enter to leave it):" | tr -cd 'A-Za-z0-9')
		[ -n "$grid" ] && put_setting MYGRID "$grid"
	fi
	put_setting REMOTE ON
	if [ ! -s data/remote.key ]; then
		cookie=""
		while [ ${#cookie} -lt 12 ]; do
			byte=$(od -An -N1 -tu1 /dev/urandom | tr -d ' ')
			[ "$byte" -lt 230 ] && cookie="$cookie${COOKIE_LETTERS:$((byte % 23)):1}"
		done
		( umask 077; echo "$cookie" > data/remote.key )
	fi
	cookie=$(head -1 data/remote.key)
	shown="${cookie:0:4}-${cookie:4:4}-${cookie:8:4}"
	echo
	echo "    The radio's cookie is $shown"
	echo
	echo "    Open this on a computer or a phone to add the radio to my.sbitx.net,"
	echo "    signed in as $callsign, or registering first:"
	echo
	echo "        $SITE/radios/add#$shown"
	echo
fi

# 9. a restart
say "zbitxre is installed. setup/undo_install.sh puts the old program back."
[ "$restart" = yes ] && say "the radio has to restart for the boot configuration to take effect."
if is_yes "$(ask "Restart the radio now? [Y/n]")y"; then
	sudo reboot
fi
