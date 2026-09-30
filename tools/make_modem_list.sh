#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# make_modem_list.sh — writes the list of modems, from the files in modems/.
#
#     tools/make_modem_list.sh modems/*.c > build/modem_list.c
#
# It finds every line that begins a definition of a modem,
#
#     const struct modem modem_xxx = {
#
# and writes a C file that names each and lists them, in the order of the
# files and, within a file, the order of the definitions. modem.h says what a
# modem is.

echo "// build/modem_list.c — made by tools/make_modem_list.sh from modems/*.c."
echo "// Do not change it; add a file to modems/ instead."
echo
echo '#include <stddef.h>'
echo
echo '#include "modem.h"'
echo

NAMES=$(sed -n 's/^const struct modem \(modem_[A-Za-z0-9_]*\) *=.*/\1/p' "$@")

for name in $NAMES; do
	echo "extern const struct modem $name;"
done
echo
echo "const struct modem *modem_list[] = {"
for name in $NAMES; do
	echo "	&$name,"
done
echo "	NULL"
echo "};"
