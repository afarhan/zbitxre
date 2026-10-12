#!/bin/bash
# start.sh — starts zbitxre on a radio with a screen of its own: the window
# (zbitx_gui) and the radio program (sdr). zbitxre.desktop, beside this
# file, runs it in a terminal each time the desktop comes up.
#
# The radio program runs in the terminal this is started in, so that what it
# prints is seen there, as the author asked. Lines written to /tmp/sdr.in
# reach it as if they were typed, and what it prints is kept in /tmp/sdr.out
# as well. When it is told to quit, the terminal closes; if it stops any
# other way the terminal stays, with what it last said, until Enter is
# pressed.
#
# The window is started first and on its own, so that it outlives the
# terminal. It opens maximised and tries the radio program every three
# seconds until it answers, so the order does not matter. It is not started
# a second time if it is already there, nor is the radio program.
#
# zbitx_remote, the radio's way out to my.sbitx.net, is started too if it
# has been built. It does nothing, and connects to nothing outside the
# radio, until the owner turns REMOTE on in the settings.

cd "$(dirname "$0")/.." || exit 1

if [ -x ./zbitx_gui ] && ! pgrep -x zbitx_gui > /dev/null; then
	setsid ./zbitx_gui localhost < /dev/null > /tmp/zbitx_gui.out 2>&1 &
fi

if [ -x ./zbitx_remote ] && ! pgrep -x zbitx_remote > /dev/null; then
	setsid ./zbitx_remote < /dev/null > /tmp/zbitx_remote.out 2>&1 &
fi

if pgrep -x sdr > /dev/null; then
	echo "sdr is already running; press Enter to close this window"
	read -r _
	exit 0
fi

[ -p /tmp/sdr.in ] || mkfifo /tmp/sdr.in
exec 3<> /tmp/sdr.in
stdbuf -oL ./sdr <&3 2>&1 | tee /tmp/sdr.out
status=${PIPESTATUS[0]}
if [ "$status" != 0 ]; then
	echo "sdr stopped with status $status; press Enter to close this window"
	read -r _ < /dev/tty
fi
