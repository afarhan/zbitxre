#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# run.sh — shows that zbitxre's receiver gives the samples zbitxv2's does.
#
#     tests/against_zbitxv2/run.sh <directory of zbitxv2's source>
#
# It builds script.c twice, once with zbitxv2's sbitx.c, fft_filter.c and
# vfo.c, and once with zbitxre's core_sdr.c and dsp/, the hardware stood in
# for by tests/hw_stand_ins.c. It runs both and compares the two
# files they write. It needs no radio and runs on a PC.
#
# The two are not the same byte for byte, as they were while zbitxre did its
# transforms with FFTW in double precision as zbitxv2 does: zbitxre now uses
# kiss_fft in single precision. So the difference is measured: it must be
# FAR_ENOUGH_BELOW_DB below zbitxv2's own numbers, and is some 127 dB below.
# A mistake in the receiver would not be that small.
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

"$new/tools/make_modem_list.sh" "$new"/modems/*.c > "$work/modem_list.c"

gcc -std=gnu11 -O2 -w -I"$new/dsp/kiss_fft" -c -o "$work/kiss_fft.o" \
	"$new/dsp/kiss_fft/kiss_fft.c"

gcc -std=gnu11 -O2 -Wall -Wextra -I"$new" -I"$new/hw" -I"$new/dsp" \
	-I"$new/dsp/kiss_fft" -I"$new/tests" \
	-o "$work/new" \
	"$here/script.c" \
	"$new/core_sdr.c" "$new/control.c" "$new/dsp/filter.c" "$new/dsp/vfo.c" \
	"$work/kiss_fft.o" \
	"$new"/modems/*.c "$work/modem_list.c" \
	"$new/tests/hw_stand_ins.c" "$new/hw/board.c" "$new/hw/si5351.c" -lm

cd "$work"
rm -f w.wis wf.wis
HOME="$work/home" ./old first.bin first.spectrum > /dev/null
./new new.bin new.spectrum
HOME="$work/home" ./old old.bin old.spectrum

python3 - <<'PY'
import struct
a = open("new.spectrum", "rb").read()
b = open("old.spectrum", "rb").read()
n = len(a) // 4
x = struct.unpack("<%di" % n, a)
y = struct.unpack("<%di" % n, b)
worst = max(abs(p - q) for p, q in zip(x, y))
mean = sum(abs(p - q) for p, q in zip(x, y)) / n
print("the spectrum differs from zbitxv2's by %.2f dB on average, %d dB at most" % (mean, worst))
PY

FAR_ENOUGH_BELOW_DB=80 python3 - <<'PY'
import math, os, struct, sys
a = open("new.bin", "rb").read()
b = open("old.bin", "rb").read()
if len(a) != len(b):
    print("THEY DIFFER in length")
    sys.exit(1)
n = len(a) // 4
x = struct.unpack("<%di" % n, a)
y = struct.unpack("<%di" % n, b)
signal = sum(float(q) * q for q in y)
error = sum(float(p - q) * (p - q) for p, q in zip(x, y))
same = sum(1 for p, q in zip(x, y) if p == q)
below = 10 * math.log10(signal / error) if error else float("inf")
print("%d of %d numbers are the same; where they differ, the difference is %.1f dB below zbitxv2's own"
    % (same, n, below))
print("the largest difference is %d, in numbers as large as %d"
    % (max(abs(p - q) for p, q in zip(x, y)), max(abs(q) for q in y)))
if below < float(os.environ["FAR_ENOUGH_BELOW_DB"]):
    print("THEY DIFFER")
    sys.exit(1)
print("the sound, the S meter and the oscillator are zbitxv2's, to single precision")
PY
