#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# panel_screen.py — makes a picture of what the front panel has on its screen.
#
#     tools/panel_screen.py [picture.png] [port] [restart]
#
# The port is found by the panel's name on USB, because the number of its
# ttyACM changes when it restarts. To name one, give its path.
#
# It asks the panel, over its USB serial port, for the screen as LVGL has
# drawn it. The panel answers with the screen's size, then the screen in
# pieces, each piece a line "AREA x y w h" followed by w * h pixels of two
# bytes, 5 bits of red, 6 of green and 5 of blue, low byte first. The pieces
# are put together and written as a PNG.
#
# With "restart" as the third word it restarts the panel first and prints
# what the panel says as it starts. The panel takes the request only once it
# is running, so after asking, this waits for the port to go away before it
# looks for it again.
#
# This shows what the firmware drew. It does not show what the glass did with
# it: wrong colours or a display turned the wrong way will not be seen here.
# The colour bars the panel shows as it starts are for that.

import glob
import os
import struct
import sys
import termios
import time
import zlib

PICTURE = "build/panel.png"
PORT = "/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*"
WAIT_FOR_PORT_S = 15
WAIT_FOR_ANSWER_S = 10
STARTING_TAKES_S = 9


def open_port(pattern):
    ends_at = time.time() + WAIT_FOR_PORT_S
    while time.time() < ends_at:
        try:
            found = glob.glob(pattern)
            port = os.open(found[0], os.O_RDWR | os.O_NOCTTY)
            break
        except (OSError, IndexError):
            time.sleep(0.2)
    else:
        sys.exit("no panel on " + pattern)
    settings = termios.tcgetattr(port)
    settings[0] = 0
    settings[1] = 0
    settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    settings[3] = 0
    settings[6][termios.VMIN] = 0
    settings[6][termios.VTIME] = 1
    termios.tcsetattr(port, termios.TCSANOW, settings)
    return port


class Reader:
    def __init__(self, port):
        self.port = port
        self.held = b""

    def fill(self, ends_at):
        if time.time() > ends_at:
            sys.exit("the panel stopped answering")
        self.held += os.read(self.port, 65536)

    def line(self):
        ends_at = time.time() + WAIT_FOR_ANSWER_S
        while b"\n" not in self.held:
            self.fill(ends_at)
        line, self.held = self.held.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def bytes(self, count):
        ends_at = time.time() + WAIT_FOR_ANSWER_S
        while len(self.held) < count:
            self.fill(ends_at)
        taken = self.held[:count]
        self.held = self.held[count:]
        return taken


def wait_until_gone(port, path):
    ends_at = time.time() + WAIT_FOR_PORT_S
    while time.time() < ends_at:
        try:
            os.read(port, 4096)
        except OSError:
            return
        if not glob.glob(path):
            return
        time.sleep(0.1)
    sys.exit("the panel did not restart")


def restart(port, path):
    os.write(port, b"r")
    wait_until_gone(port, path)
    os.close(port)
    time.sleep(0.5)
    port = open_port(path)
    ends_at = time.time() + STARTING_TAKES_S
    said = b""
    while time.time() < ends_at:
        said += os.read(port, 4096)
    for line in said.decode(errors="replace").splitlines():
        print("panel:", line)
    return port


def read_screen(port):
    reader = Reader(port)
    os.write(port, b"s")
    line = reader.line()
    while not line.startswith("SCREEN"):
        line = reader.line()
    width, height = [int(word) for word in line.split()[1:3]]
    screen = [bytearray(width * 3) for _ in range(height)]
    covered = 0
    while True:
        line = reader.line()
        if line == "END":
            break
        if not line.startswith("AREA"):
            continue
        x, y, w, h = [int(word) for word in line.split()[1:5]]
        pixels = struct.unpack("<%dH" % (w * h), reader.bytes(w * h * 2))
        covered += w * h
        for row in range(h):
            for column in range(w):
                pixel = pixels[row * w + column]
                at = (x + column) * 3
                screen[y + row][at] = (pixel >> 11) << 3
                screen[y + row][at + 1] = ((pixel >> 5) & 0x3F) << 2
                screen[y + row][at + 2] = (pixel & 0x1F) << 3
    return width, height, screen, covered


def chunk(name, body):
    check = zlib.crc32(name + body) & 0xFFFFFFFF
    return struct.pack(">I", len(body)) + name + body + struct.pack(">I", check)


def write_png(path, width, height, screen):
    rows = b"".join(b"\x00" + bytes(row) for row in screen)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as picture:
        picture.write(b"\x89PNG\r\n\x1a\n")
        picture.write(chunk(b"IHDR", header))
        picture.write(chunk(b"IDAT", zlib.compress(rows)))
        picture.write(chunk(b"IEND", b""))


def main():
    picture = PICTURE
    path = PORT
    if len(sys.argv) > 1:
        picture = sys.argv[1]
    if len(sys.argv) > 2:
        path = sys.argv[2]
    port = open_port(path)
    if len(sys.argv) > 3 and sys.argv[3] == "restart":
        port = restart(port, path)
    width, height, screen, covered = read_screen(port)
    os.close(port)
    write_png(picture, width, height, screen)
    print("%s: %d by %d, %d of %d pixels sent" %
          (picture, width, height, covered, width * height))


if __name__ == "__main__":
    main()
