#!/bin/bash
# update.sh — brings a radio's zbitxre up to GitHub's main, builds it, and
# starts the new programs.
#
#     setup/update.sh               pull, build, and restart the radio
#     setup/update.sh --build-only  pull and build; the radio runs on
#     setup/update.sh --if-new      as the first, but only if GitHub has
#                                   something new; what the radio's UPDATE,
#                                   in SET, runs, and it says how it goes on
#                                   the radio's status line
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
# the commit the programs were last built from, written when sdr has
# built. --if-new goes by it and not by what git has: on 16 October 2026 a
# production zbitx said "up to date" at the newest commit, which an
# earlier update had pulled and not built, and was left on its old
# programs
BUILT_FROM=data/built_from

build_only=0
if_new=0
if [ "$1" = "--build-only" ]; then
	build_only=1
elif [ "$1" = "--if-new" ]; then
	if_new=1
elif [ -n "$1" ]; then
	echo "usage: setup/update.sh [--build-only | --if-new]"
	exit 2
fi

# said here, and to the radio, when it is running, as its UPDATE_STATUS,
# which the window and the panel show in a view of their own while the
# update goes on; the pipe is given two seconds, so that an update with no
# radio program to read it does not wait for ever. The panel shows 47
# characters, so each is kept to that, in plain words
say() {
	echo "update: $*"
	[ -p /tmp/sdr.in ] && pgrep -x sdr > /dev/null \
		&& timeout 2 sh -c 'echo "update_status $1" > /tmp/sdr.in' sh "$*"
}

# how the update ended, the last that is said, which the window and the
# panel show as its outcome
done_with() {
	say "DONE: $*"
	exit 0
}

failed() {
	say "FAILED: $*"
	exit 1
}

# what was not as it should be, but did not stop the update, said at its
# end
notes=""
note() {
	say "$*"
	notes="$notes; $1"
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

# the clock first: a radio whose clock says 2000, as a zbitx's can after
# install.sh gives the kernel its clock chip, finds GitHub's certificate
# not yet valid. set_clock.sh says why and sets it from my.sbitx.net
if ! setup/set_clock.sh > /dev/null; then
	failed "the clock is wrong and could not be set"
fi

before=$(git rev-parse --short HEAD)
say "at $before; asking GitHub for news"
if ! git pull --ff-only --quiet origin main; then
	failed "could not reach GitHub; nothing changed"
fi
after=$(git rev-parse --short HEAD)
if [ "$before" = "$after" ]; then
	say "already at the newest, $after"
else
	say "fetched $after"
	git log --oneline "$before..$after" | sed 's/^/    /'
fi
built=$(cat "$BUILT_FROM" 2>/dev/null)
if [ "$if_new" = 1 ] && [ "$built" = "$after" ]; then
	done_with "already up to date, at $after"
fi

if ! setup/fetch_rade_weights.sh > /dev/null; then
	failed "RADE files not fetched; nothing changed"
fi

say "building; about ten minutes on a zbitx"
if ! make --quiet sdr; then
	failed "the build failed; the radio is as it was"
fi
mkdir -p data && echo "$after" > "$BUILT_FROM"
if pkg-config --exists gtk+-3.0 2>/dev/null; then
	say "building the window"
	make --quiet gui || note "window build failed; old one kept"
fi
say "building the gateway"
make --quiet remote || note "gateway build failed; old one kept"

if [ "$build_only" = 1 ]; then
	done_with "built $after; used at the next start$notes"
fi

if pgrep -x sdr > /dev/null; then
	while [ "$(radio_tx)" = "ON" ]; do
		say "waiting for TX OFF to restart"
		sleep "$TX_POLL_SECONDS"
	done
	say "restarting the radio"
	[ -p /tmp/sdr.in ] && echo quit > /tmp/sdr.in
	for _ in $(seq 20); do
		pgrep -x sdr > /dev/null || break
		sleep 0.5
	done
	if pgrep -x sdr > /dev/null; then
		failed "sdr did not stop; restart the radio"
	fi
fi
pkill -x zbitx_gui
pkill -x zbitx_remote

# a zbitx's front panel, brought to the firmware of this release over I2C,
# while the radio program is stopped
setup/panel_firmware.sh || notes="$notes; the panel kept its firmware"

# how it ended, for the radio to say when it has started again
mkdir -p data
echo "DONE: updated to $after$notes" > data/update_result

export DISPLAY=${DISPLAY:-:0}
export XAUTHORITY=${XAUTHORITY:-$HOME/.Xauthority}
if command -v x-terminal-emulator > /dev/null && xset q > /dev/null 2>&1; then
	setsid x-terminal-emulator -e "$PWD/setup/start.sh" < /dev/null > /dev/null 2>&1 &
	say "the radio is starting on its screen"
else
	say "there is no screen to start the radio on; run setup/start.sh"
	exit 1
fi
