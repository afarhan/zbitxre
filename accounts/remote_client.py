#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
remote_client.py — a radio reached through my.sbitx.net, from a terminal.

    remote_client.py vu2ese-home     that radio: what it says is printed, and
                                     what is typed is sent to it, as with nc

It is the smallest client there can be, written to try the service with and
as a model for a client on any platform: PROTOCOL.md says what is spoken,
and this is all of it in a page.

The radio is named as the website names it, the owner's callsign and the
radio's own name. The first time, the radio's client password is asked
for, the twelve letters the website shows beside the radio; it is kept in
~/.config/zbitx/<radio>.password, which only this user can read, where
zbitx_gui keeps it too. A password the service refuses is forgotten.

--server and --port point it at another service than my.sbitx.net.
"""

import argparse
import getpass
import hashlib
import hmac
import os
import re
import secrets
import socket
import sys
import threading

STRETCH_ROUNDS = 50000


def password_file(radio):
    return os.path.expanduser("~/.config/zbitx/%s.password" % radio)


def password_of(radio):
    """the radio's client password, its twelve letters, from the file or
    asked for and then kept"""
    path = password_file(radio)
    if os.path.exists(path):
        with open(path) as file:
            return file.read().strip()
    letters = re.sub(r"[^a-z]", "", getpass.getpass(
        "The client password of %s, from my.sbitx.net: " % radio).lower())
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    with open(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), "w") as file:
        file.write(letters + "\n")
    return letters


def proof(password, side, radio, challenge, nonce):
    """the answer to a challenge: a keyed hash whose key is the password
    stretched, with the radio's name as salt"""
    key = hashlib.pbkdf2_hmac("sha256", password.encode(),
                              ("zbitx-1 " + radio.lower()).encode(), STRETCH_ROUNDS).hex()
    text = "zbitx-auth-1 %s %s %s %s" % (side, radio, challenge, nonce)
    return hmac.new(key.encode(), text.encode(), hashlib.sha256).hexdigest()


def main():
    parser = argparse.ArgumentParser(description="a radio reached through my.sbitx.net")
    parser.add_argument("radio", help="as the website names it: vu2ese-home")
    parser.add_argument("--server", default="my.sbitx.net")
    parser.add_argument("--port", type=int, default=8081)
    args = parser.parse_args()
    password = password_of(args.radio)

    line = socket.create_connection((args.server, args.port), timeout=20)
    said = line.makefile("r", newline="\n")
    challenge = said.readline().split()[1]
    nonce = secrets.token_hex(16)
    line.sendall(("AUTH client %s %s %s\n" % (
        args.radio, nonce, proof(password, "client", args.radio, challenge, nonce))).encode())
    welcome = said.readline().split()
    if len(welcome) != 3 or welcome[0] != "WELCOME":
        os.remove(password_file(args.radio))
        sys.exit("The service refused: %s" % " ".join(welcome))
    if not hmac.compare_digest(welcome[2],
                               proof(password, "server", args.radio, challenge, nonce)):
        sys.exit("Whatever answered at %s does not have the client password." % args.server)
    answer = said.readline().strip()
    print(answer)
    if not answer.startswith("CONNECTED"):
        return
    line.settimeout(None)

    def show():
        for told in said:
            sys.stdout.write(told)
            sys.stdout.flush()
        os._exit(0)

    threading.Thread(target=show, daemon=True).start()
    for typed in sys.stdin:
        line.sendall(typed.encode())


if __name__ == "__main__":
    main()
