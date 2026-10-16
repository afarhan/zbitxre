#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# set_clock.sh — sets the radio's clock when it is plainly wrong, from the
# Date of a plain HTTP answer from my.sbitx.net, and writes it to the clock
# chip, if the kernel has one.
#
#     setup/set_clock.sh
#
# Why: on a production zbitx on 16 October 2026 the clock read 1 January
# 2000 for more than ten minutes after it started, and every certificate
# on the internet looked not yet valid, GitHub's among them, so the update
# could not pull. Its clock chip, given to the kernel by install.sh, had
# never been set, and its time service, ntpd, looked for its servers before
# the WiFi was up, failed, and was slow to look again. So a clock before
# EARLIEST, in UTC, is not waited on: it is set. Plain HTTP needs no
# certificate, and the Date it carries is right to the second, which is all
# that certificates and FT8's fifteen-second slots want.
#
# It changes the clock only through sudo with no password, as on a zbitx,
# and gives up quietly where sudo asks for one. start.sh, install.sh and
# update.sh run it; a clock that is right is left alone.

EARLIEST=20261001
SOURCE=http://my.sbitx.net/

[ "$(date -u +%Y%m%d)" -ge "$EARLIEST" ] && exit 0
sudo -n true 2>/dev/null || exit 1

for _ in $(seq 12); do
	said=$(curl -sI --max-time 5 "$SOURCE" | tr -d '\r' | sed -n 's/^[Dd]ate: //p')
	if [ -n "$said" ] && sudo -n date -u -s "$said" > /dev/null 2>&1; then
		[ -e /dev/rtc0 ] && sudo -n hwclock -w 2>/dev/null
		echo "clock: set from my.sbitx.net, $(date -u '+%d %B %Y %H:%M:%S') UTC"
		exit 0
	fi
	# the network may not be up yet, just after the radio started
	sleep 5
done
echo "clock: still $(date -u '+%d %B %Y'); my.sbitx.net did not answer"
exit 1
