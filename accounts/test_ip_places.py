#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
test_ip_places.py — where a network address roughly is, to the grid square.

    python3 -m unittest test_ip_places

A list is made here in the form DB-IP's has, with a few made-up ranges, and
built and looked up. Nothing is fetched.
"""

import gzip
import os
import tempfile
import unittest

import ip_places

LIST = """\
0.0.0.0,0.255.255.255,ZZ,ZZ,,,,
0.255.255.255,0.255.255.255,ZZ,ZZ,,,0,0
1.0.0.0,1.0.0.255,OC,AU,Queensland,South Brisbane,-27.4748,153.017
1.0.1.0,1.0.3.255,AS,CN,Fujian,Fuzhou,26.0614,119.306
1.0.4.0,1.0.7.255,OC,AU,Victoria,Melbourne,-37.8159,144.967
49.204.0.0,49.204.127.255,AS,IN,Telangana,Hyderabad,17.385,78.4867
49.204.128.0,49.204.255.255,AS,IN,Telangana,Secunderabad,17.45,78.5
49.205.0.0,49.205.255.255,AS,IN,Karnataka,Bengaluru,12.9716,77.5946
2001:200::,2001:200:ffff:ffff:ffff:ffff:ffff:ffff,AS,JP,Tokyo,Tokyo,35.6895,139.692
"""


class IpPlacesTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        self.source = os.path.join(self.folder, "list.csv.gz")
        self.path = os.path.join(self.folder, "places.bin")
        with gzip.open(self.source, "wt") as file:
            file.write(LIST)

    def test_a_place_is_named_by_its_grid_square(self):
        self.assertEqual(ip_places.grid_of(17.385, 78.4867), "MK97")
        self.assertEqual(ip_places.grid_of(40.71, -74.0), "FN30")
        self.assertEqual(ip_places.grid_of(-33.87, 151.21), "QF56")
        self.assertEqual(ip_places.grid_of(90, 180), "RR99")
        self.assertEqual(ip_places.grid_of(-90, -180), "AA00")

    def test_an_address_is_in_the_square_of_the_range_it_falls_in(self):
        written = ip_places.build_file(self.source, self.path)
        places = ip_places.Places(self.path)
        self.assertEqual(places.where("49.204.3.9"), "MK97")
        self.assertEqual(places.where("49.204.255.255"), "MK97")
        self.assertEqual(places.where("49.205.0.0"), "MK82")
        self.assertEqual(places.where("1.0.0.77"), "QG62")
        # ranges side by side in one square are one run: Hyderabad's two
        self.assertEqual(written, 6)
        self.assertEqual(os.path.getsize(self.path), 6 * 8)

    def test_what_has_no_place_is_nowhere(self):
        ip_places.build_file(self.source, self.path)
        places = ip_places.Places(self.path)
        self.assertIsNone(places.where("0.1.2.3"), "a range the list gives no place for")
        self.assertIsNone(places.where("0.255.255.255"), "one the list puts at nought, nought")
        self.assertIsNone(places.where("2001:200::1"), "an IPv6 address")
        self.assertIsNone(places.where("not an address"))
        self.assertIsNone(ip_places.Places("").where("49.204.3.9"), "with no file")
        self.assertIsNone(ip_places.Places(self.path + ".none").where("49.204.3.9"))

    def test_a_file_that_is_replaced_is_taken_up(self):
        ip_places.build_file(self.source, self.path)
        places = ip_places.Places(self.path)
        self.assertEqual(places.where("49.205.0.1"), "MK82")
        with gzip.open(self.source, "wt") as file:
            file.write("0.0.0.0,255.255.255.255,EU,GB,England,London,51.5,-0.12\n")
        ip_places.build_file(self.source, self.path)
        os.utime(self.path, (1, 1))
        self.assertEqual(places.where("49.205.0.1"), "IO91")


if __name__ == "__main__":
    unittest.main()
