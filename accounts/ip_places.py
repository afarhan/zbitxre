#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
ip_places.py — roughly where on the earth a network address is, to the
grid square.

WHAT IT IS FOR

The globe at my.sbitx.net shows the radios that are on line. The author
asked that it show those listening to them as well. All that is known of
someone listening is the address their browser came from, so that is
looked up here for a place.

HOW ROUGH, AND WHY

What is given for an address is a four-character grid square, MK97: two
degrees of longitude by one of latitude, some two hundred kilometres by a
hundred. Nothing finer is kept or can be had from this file. That is
deliberate. A listener has not been asked where they are, and a dot on a
public map should not be able to mark a town, let alone a house. It is
also about as much as the lookup is good for: a telephone's address is its
network's, and may be placed where the network meets the internet, a city
or two away from the telephone.

WHERE THE PLACES COME FROM

DB-IP's "IP to City Lite" database (https://db-ip.com), which is free to
use under the Creative Commons Attribution 4.0 licence, so the page that
shows the dots says whose it is. It is a list of ranges of addresses, each
with a latitude and a longitude, some 86 MB compressed, issued afresh each
month. It is not used as it comes. `build` goes through it once and writes
a small file of this program's own: for each run of neighbouring ranges
that fall in the same square, where the run begins and the square, eight
bytes. Only IPv4 is kept; the service is reached at an IPv4 address only.

    ip_places.py fetch OUT.bin         this month's list from DB-IP, or last
                                       month's if this month's is not out,
                                       made into OUT.bin; for cron, monthly
    ip_places.py build LIST.csv.gz OUT.bin     the same from a list on disk
    ip_places.py where OUT.bin ADDRESS         the square of one address

Nothing is asked of anyone when a listener comes: the lookup is in the
file, on this machine.

THE FILE

Records of eight bytes, in the order of the addresses: four bytes, the
first address of a run as a number, most significant byte first; and four
characters, the square, or four spaces where the list gave no place. An
address belongs to the last record that begins at or before it. The file
is mapped into memory, not read in, and looked up by halving.
"""

import bisect
import csv
import gzip
import ipaddress
import mmap
import os
import struct
import sys
import tempfile
import time
import urllib.request

RECORD = struct.Struct(">I4s")
NO_PLACE = b"    "
SOURCE = "https://download.db-ip.com/free/dbip-city-lite-%s.csv.gz"


def grid_of(latitude, longitude):
    """the four-character grid square a place is in"""
    east = min(359.999, max(0.0, longitude + 180.0))
    north = min(179.999, max(0.0, latitude + 90.0))
    return "%s%s%d%d" % (chr(ord("A") + int(east // 20)), chr(ord("A") + int(north // 10)),
                         int(east % 20 // 2), int(north % 10))


def build(lines, out):
    """DB-IP's list, as lines of text, is written out as this program's
    file; says how many runs were written"""
    written = 0
    last = None
    for row in csv.reader(lines):
        if len(row) < 8 or ":" in row[0]:
            continue
        try:
            start = int(ipaddress.IPv4Address(row[0]))
        except ValueError:
            continue
        try:
            latitude, longitude = float(row[6]), float(row[7])
            square = grid_of(latitude, longitude).encode()
            # the list puts what has no place, private addresses among it,
            # at nought and nought, which is the sea off Africa
            if latitude == 0 and longitude == 0:
                square = NO_PLACE
        except ValueError:
            square = NO_PLACE
        if square != last:
            out.write(RECORD.pack(start, square))
            written += 1
            last = square
    return written


def build_file(source, path):
    """the same, from a compressed list on disk to a file that takes the
    old one's place only when it is whole"""
    folder = os.path.dirname(os.path.abspath(path))
    with tempfile.NamedTemporaryFile(dir=folder, delete=False) as out:
        with gzip.open(source, "rt", encoding="utf-8", errors="replace", newline="") as lines:
            written = build(lines, out)
    if written == 0:
        os.remove(out.name)
        raise ValueError("no addresses in %s" % source)
    os.chmod(out.name, 0o644)
    os.replace(out.name, path)
    return written


def fetch(path):
    """this month's list, or failing that last month's, is fetched and
    built"""
    now = time.gmtime()
    months = ["%04d-%02d" % (now.tm_year, now.tm_mon)]
    months.append("%04d-%02d" % ((now.tm_year, now.tm_mon - 1) if now.tm_mon > 1
                                 else (now.tm_year - 1, 12)))
    folder = os.path.dirname(os.path.abspath(path))
    for month in months:
        with tempfile.NamedTemporaryFile(dir=folder, suffix=".csv.gz") as kept:
            try:
                # DB-IP's server refuses a request that does not say what is
                # asking; this says so truthfully
                asked = urllib.request.Request(SOURCE % month, headers={
                    "User-Agent": "ip_places.py (my.sbitx.net; listeners' grid squares)"})
                with urllib.request.urlopen(asked, timeout=60) as answer:
                    while True:
                        part = answer.read(1 << 20)
                        if not part:
                            break
                        kept.write(part)
            except OSError as failed:
                print("ip_places: %s: %s" % (month, failed))
                continue
            kept.flush()
            return month, build_file(kept.name, path)
    raise OSError("the list could not be fetched")


class Places:
    """the file, looked up. With no file, or one that cannot be read, every
    address is nowhere, and the service runs as it did before there were
    any places"""

    def __init__(self, path):
        self.path = path
        self.changed = None
        self.held = None
        self.count = 0

    def open(self):
        """the file is taken up, or taken up again if it has been replaced"""
        if not self.path:
            return
        try:
            changed = os.stat(self.path).st_mtime
            if changed == self.changed:
                return
            with open(self.path, "rb") as file:
                held = mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ)
        except (OSError, ValueError):
            return
        self.held, self.count, self.changed = held, len(held) // RECORD.size, changed

    def __len__(self):
        return self.count

    def __getitem__(self, at):
        return RECORD.unpack_from(self.held, at * RECORD.size)[0]

    def where(self, address):
        """the grid square of an address, MK97, or None"""
        self.open()
        if self.held is None:
            return None
        try:
            number = int(ipaddress.IPv4Address(address))
        except ValueError:
            return None
        at = bisect.bisect_right(self, number) - 1
        if at < 0:
            return None
        square = RECORD.unpack_from(self.held, at * RECORD.size)[1]
        if square == NO_PLACE:
            return None
        return square.decode()


def main(argv):
    if len(argv) == 3 and argv[0] == "build":
        print("ip_places: %d runs of addresses" % build_file(argv[1], argv[2]))
    elif len(argv) == 2 and argv[0] == "fetch":
        print("ip_places: the list of %s, %d runs of addresses" % fetch(argv[1]))
    elif len(argv) == 3 and argv[0] == "where":
        print(Places(argv[1]).where(argv[2]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
