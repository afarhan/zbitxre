#!/bin/bash
# desktop_icon.sh — puts the zbitxre icon on the radio's desktop and in its
# menu, among the Internet programs, where either is missing. install.sh and update.sh
# run it, the author's wish of 17 October 2026, so that a radio set up
# before there was an icon gets one with its next update. An icon the owner
# has removed from the desktop is put back by the next update.
#
# The icon runs setup/open.sh, which opens the window and starts the radio
# program if it is not running. The entry names /home/pi/zbitxre and is
# written with the place this repository is in.

cd "$(dirname "$0")/.." || exit 1

entry=$(sed "s|/home/pi/zbitxre|$PWD|" setup/zbitxre-icon.desktop)
for place in "$HOME/Desktop" "$HOME/.local/share/applications"; do
	[ -f "$place/zbitxre.desktop" ] && continue
	mkdir -p "$place"
	echo "$entry" > "$place/zbitxre.desktop"
	# the desktop runs an entry without asking only when it may be run
	chmod +x "$place/zbitxre.desktop"
	echo "zbitxre: the zbitxre icon is in $place"
done
