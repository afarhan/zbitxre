#!/bin/bash
# update.sh — brings a radio's zbitxre up to GitHub's main, builds it, and
# starts the new programs.
#
#     setup/update.sh               pull, build, and restart the radio
#     setup/update.sh --build-only  pull and build; the radio runs on
#
# Written on 14 October 2026 for the sbitx of Sasi, a tester in the
# development team, whose radio is across the town. The author's ruling: a
# tester follows main, as the author pushes it, by pulling and building.
# The radio reads GitHub with a read-only deploy key of its own (setup/
# README.md, "Updating a radio from GitHub").
#
# What it does, and what it will not:
#
# The pull only goes forward (--ff-only). If the radio's copy has commits or
# changes of its own that are not on GitHub, it stops and says so, and
# nothing on the radio is touched: a tester's own work is not overwritten.
#
# Everything in data/ is the radio's own and not in git: its settings, its
# hardware's calibration, its logbook. A pull never reaches it.
#
# The weights RADE2 needs are not in git; fetch_rade_weights.sh fetches any
# that are missing and leaves those already there.
#
# A failed build leaves the programs that were there: make writes a program
# only when it has been built whole. The update then stops, and the radio
# goes on as it was.
#
# The radio is not restarted in the middle of a transmission. If sdr is
# running, the update asks it on port 8081 whether it is transmitting and
# waits, for as long as it takes, until TX is OFF. Then it tells sdr to quit
# and starts start.sh in a terminal on the radio's screen, as the desktop
# does at login, with the new window and zbitx_remote too.

cd "$(dirname "$0")/.." || exit 1

RADIO_PORT=8081
TX_POLL_SECONDS=5

build_only=0
if [ "$1" = "--build-only" ]; then
	build_only=1
elif [ -n "$1" ]; then
	echo "usage: setup/update.sh [--build-only]"
	exit 2
fi

say() {
	echo "update: $*"
}

# the last TX line the radio program tells a client that connects: ON or
# OFF, or nothing if it does not answer
radio_tx() {
	python3 - "$RADIO_PORT" <<'EOF'
import socket, sys, time
try:
    s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=3)
except OSError:
    sys.exit(0)
s.settimeout(0.5)
held = b""
until = time.time() + 2
while time.time() < until:
    try:
        held += s.recv(65536)
    except OSError:
        pass
said = [l for l in held.decode(errors="replace").split("\n") if l.startswith("TX ")]
if said:
    print(said[-1].split(" ")[1])
EOF
}

before=$(git rev-parse --short HEAD)
say "this radio is at $before; asking GitHub for main"
if ! git pull --ff-only --quiet origin main; then
	say "the pull did not go forward; nothing was changed. 'git status' says why"
	exit 1
fi
after=$(git rev-parse --short HEAD)
if [ "$before" = "$after" ]; then
	say "already at main, $after"
else
	say "now at $after:"
	git log --oneline "$before..$after" | sed 's/^/    /'
fi

if ! setup/fetch_rade_weights.sh > /dev/null; then
	say "RADE's weights could not all be fetched; the build stops here"
	exit 1
fi

say "building sdr"
if ! make --quiet sdr; then
	say "sdr did not build; the radio keeps the program it had"
	exit 1
fi
if pkg-config --exists gtk+-3.0 2>/dev/null; then
	say "building the window"
	make --quiet gui || say "the window did not build; it keeps the one it had"
fi
say "building zbitx_remote"
make --quiet remote || say "zbitx_remote did not build; it keeps the one it had"

if [ "$build_only" = 1 ]; then
	say "built; the radio goes on with the programs it started with"
	exit 0
fi

if pgrep -x sdr > /dev/null; then
	while [ "$(radio_tx)" = "ON" ]; do
		say "the radio is transmitting; waiting for TX OFF"
		sleep "$TX_POLL_SECONDS"
	done
	say "TX is OFF; stopping the radio"
	[ -p /tmp/sdr.in ] && echo quit > /tmp/sdr.in
	for _ in $(seq 20); do
		pgrep -x sdr > /dev/null || break
		sleep 0.5
	done
	if pgrep -x sdr > /dev/null; then
		say "sdr did not quit; the new build is in place for its next start"
		exit 1
	fi
fi
pkill -x zbitx_gui
pkill -x zbitx_remote

# a zbitx's front panel, brought to the firmware of this release over I2C,
# while the radio program is stopped
setup/panel_firmware.sh

export DISPLAY=${DISPLAY:-:0}
export XAUTHORITY=${XAUTHORITY:-$HOME/.Xauthority}
if command -v x-terminal-emulator > /dev/null && xset q > /dev/null 2>&1; then
	setsid x-terminal-emulator -e "$PWD/setup/start.sh" < /dev/null > /dev/null 2>&1 &
	say "the radio is starting on its screen"
else
	say "there is no screen to start the radio on; run setup/start.sh"
fi
