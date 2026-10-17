#!/bin/bash
# open.sh — what the zbitxre icon on the desktop runs: the window, and the
# radio program if it is not running. The author asked on 17 October 2026
# for an icon to start the window from, for when it has been closed.
#
# The window is started if it is not there already. When the radio program
# is not running either, start.sh is run in a terminal, as at login, which
# starts it and the gateway too; when it is, no terminal is opened.

cd "$(dirname "$0")/.." || exit 1

if ! pgrep -x sdr > /dev/null; then
	exec x-terminal-emulator -e "$PWD/setup/start.sh"
fi
if [ -x ./zbitx_gui ] && ! pgrep -x zbitx_gui > /dev/null; then
	setsid ./zbitx_gui localhost < /dev/null > /tmp/zbitx_gui.out 2>&1 &
fi
