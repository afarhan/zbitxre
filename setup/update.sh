#!/bin/bash
# update.sh — brings a radio's zbitxre up to GitHub's main and builds it.
#
#     setup/update.sh           pull and build; the radio uses what is built
#                               when it is next started
#     setup/update.sh --if-new  the same, but only if GitHub has something
#                               not yet built; what the radio's UPDATE, in
#                               SET, runs
#
# Written on 14 October 2026 for the sbitx of Sasi, a tester in the
# development team, whose radio is across the town. The author's ruling: a
# tester follows main, as the author pushes it, by pulling and building.
# The radio reads GitHub with a read-only deploy key of its own (setup/
# README.md, "Updating a radio from GitHub").
#
# It only fetches and builds. When the radio's UPDATE runs it, the radio
# program reads what it says through a pipe, shows it in the view of the
# update, and, when something new was built, restarts itself with it and
# gives the panel its firmware: tools/sdr.c says how. Until 17 October 2026
# this script stopped the radio and started it again in a new terminal,
# which was handed the radio's port for clients and kept it; the author
# asked for the radio to run the update itself instead.
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
# It ends with 0 when something new was built, UP_TO_DATE when there was
# nothing to build, and 1 when it failed or was cancelled.

cd "$(dirname "$0")/.." || exit 1

UP_TO_DATE=3
# the commit the programs were last built from, written when sdr has
# built. --if-new goes by it and not by what git has: on 16 October 2026 a
# production zbitx said "up to date" at the newest commit, which an
# earlier update had pulled and not built, and was left on its old
# programs
BUILT_FROM=data/built_from
# the update can be cancelled, from the view of it in the GTK client and
# on the front panel, as the author asked on 16 October 2026: the radio sends this
# script's process group TERM. A build stopped part way leaves no program,
# make taking away the one it was writing, so the programs are kept in
# KEPT before the build and put back. git is left where the pull took it:
# BUILT_FROM, put back too, says that it was not built, and the next update
# builds it
KEPT=/tmp/update_kept
PROGRAMS="sdr zbitx_gui zbitx_remote"

if_new=0
if [ "$1" = "--if-new" ]; then
	if_new=1
elif [ -n "$1" ] && [ "$1" != "--build-only" ]; then
	echo "usage: setup/update.sh [--if-new]"
	exit 2
fi

# each line is a step of the update, which the GTK client and the front
# panel show;
# the panel shows 47 characters, so each is kept to that, in plain words
say() {
	echo "update: $*"
}

# how the update ended, the last that is said, which the GTK client and
# the front panel show as its outcome
done_with() {
	say "DONE: $*"
	exit "$UP_TO_DATE"
}

failed() {
	say "FAILED: $*"
	exit 1
}

cancelled() {
	trap '' TERM INT
	wait 2>/dev/null
	for program in $PROGRAMS; do
		[ -f "$KEPT/$program" ] && cp -p "$KEPT/$program" "$program"
	done
	if [ -f "$KEPT/built_from" ]; then
		cp -p "$KEPT/built_from" "$BUILT_FROM"
	else
		rm -f "$BUILT_FROM"
	fi
	# a git stopped in the middle leaves its lock
	pgrep -x git > /dev/null || rm -f .git/index.lock
	say "CANCELLED: the radio is as it was"
	exit 1
}

trap cancelled TERM INT
rm -rf "$KEPT" && mkdir -p "$KEPT"
for program in $PROGRAMS; do
	[ -f "$program" ] && cp -p "$program" "$KEPT/"
done
[ -f "$BUILT_FROM" ] && cp -p "$BUILT_FROM" "$KEPT/"

# the clock first: a radio whose clock says 2000, as a zbitx's can after
# install.sh gives the kernel its clock chip, finds GitHub's certificate
# not yet valid. set_clock.sh says why and sets it from my.sbitx.net
if ! setup/set_clock.sh > /dev/null; then
	failed "the clock is wrong and could not be set"
fi

before=$(git rev-parse --short HEAD)
say "at $before; asking GitHub for news"
if ! git pull --ff-only --quiet origin main; then
	failed "the pull failed; nothing changed"
fi
after=$(git rev-parse --short HEAD)
if [ "$before" = "$after" ]; then
	say "already at the newest, $after"
else
	say "fetched $after"
	git log --oneline "$before..$after" | sed 's/^/    /'
fi
# a radio set up before there was an icon on the desktop is given one,
# whatever else the update finds to do
setup/desktop_icon.sh > /dev/null
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
	say "building the GTK client"
	make --quiet gui || say "GTK client build failed; old one kept"
fi
say "building the gateway"
make --quiet remote || say "gateway build failed; old one kept"

# nothing to cancel any more; the radio, if it ran this, restarts with
# what was built
trap '' TERM INT
say "built $after"
[ -z "$ZBITXRE_FROM_RADIO" ] && echo "update: restart the radio to use it"
exit 0
