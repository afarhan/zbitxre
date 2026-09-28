#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# run.sh — shows that zbitxre's receiver gives the same samples as zbitxv2's.
#
#     tests/against_zbitxv2/run.sh <directory of zbitxv2's source>
#
# It builds script.c twice, once with zbitxv2's sbitx.c, fft_filter.c and
# vfo.c, and once with zbitxre's dsp/. It runs both and compares the two
# files they write. It needs no radio and runs on a PC.
#
# zbitxv2's sbitx.c reads hw_settings.ini from $HOME/sbitx/data, so the old
# program is run with a HOME made here, holding the zbitx's settings.
#
# zbitxv2 is built without warnings shown, because it has many and none of
# them are this test's business.

set -e

if [ $# -lt 1 ] || [ ! -f "$1/sbitx.c" ]
then
	echo "run.sh <directory of zbitxv2's source>"
	exit 1
fi

old=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
new=$(cd "$here/../.." && pwd)
work="$new/build/against_zbitxv2"
libs="-lfftw3 -lfftw3f -lpthread -lm"

mkdir -p "$work/home/sbitx/data"
cp "$here/hw_settings.ini" "$work/home/sbitx/data/hw_settings.ini"

gcc -std=gnu11 -O2 -w -DOLD -I"$here" -I"$old" -o "$work/old" \
	"$here/script.c" "$here/stand_ins.c" \
	"$old/sbitx.c" "$old/fft_filter.c" "$old/vfo.c" \
	"$old/ini.c" "$old/queue.c" $libs

gcc -std=gnu11 -O2 -Wall -Wextra -I"$new/hw" -I"$new/dsp" \
	-I"$new/front_panel" -o "$work/new" \
	"$here/script.c" \
	"$new/dsp/sdr.c" "$new/dsp/filter.c" "$new/dsp/vfo.c" \
	"$new"/hw/*.c "$new/front_panel/link.c" $libs -lasound

cd "$work"
rm -f w.wis wf.wis
./new first.bin > /dev/null
./new new.bin
HOME="$work/home" ./old old.bin

if cmp new.bin old.bin
then
	echo "the same, every byte"
else
	echo "THEY DIFFER"
	exit 1
fi
