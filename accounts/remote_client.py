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
radio's own name. The first time, the password of that callsign's account
is asked for, the one the website is signed in to with. It is not kept:
what it stretches to is, the key the proofs are made with, in
~/.config/zbitx/<callsign>.key, which only this user can read, where
zbitx_gui keeps it too. A key the service refuses is forgotten. (Until
2026-10-16 each radio had a client password of its own for this.)

--server and --port point it at another service than my.sbitx.net.
"""

import argparse
import getpass
import hashlib
import hmac
import os
import secrets
import socket
import sys
import threading

STRETCH_ROUNDS = 50000


def key_file(radio):
    return os.path.expanduser("~/.config/zbitx/%s.key" % radio.partition("-")[0].lower())


def key_of(radio):
    """the stretched password of the radio's account, from the file, or
    from the password asked for, and then kept"""
    path = key_file(radio)
    if os.path.exists(path):
        with open(path) as file:
            return file.read().strip()
    callsign = radio.partition("-")[0]
    password = getpass.getpass("The password of %s at my.sbitx.net: " % callsign.upper())
    key = hashlib.pbkdf2_hmac("sha256", password.encode(),
                              ("zbitx-1 " + callsign.lower()).encode(), STRETCH_ROUNDS).hex()
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    with open(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), "w") as file:
        file.write(key + "\n")
    return key


def proof(key, side, radio, challenge, nonce):
    """the answer to a challenge: a keyed hash whose key is the password
    stretched, with the callsign as salt"""
    text = "zbitx-auth-1 %s %s %s %s" % (side, radio, challenge, nonce)
    return hmac.new(key.encode(), text.encode(), hashlib.sha256).hexdigest()


def main():
    parser = argparse.ArgumentParser(description="a radio reached through my.sbitx.net")
    parser.add_argument("radio", help="as the website names it: vu2ese-home")
    parser.add_argument("--server", default="my.sbitx.net")
    parser.add_argument("--port", type=int, default=8081)
    args = parser.parse_args()
    key = key_of(args.radio)

    line = socket.create_connection((args.server, args.port), timeout=20)
    said = line.makefile("r", newline="\n")
    challenge = said.readline().split()[1]
    nonce = secrets.token_hex(16)
    line.sendall(("AUTH client %s %s %s\n" % (
        args.radio, nonce, proof(key, "client", args.radio, challenge, nonce))).encode())
    welcome = said.readline().split()
    if len(welcome) != 3 or welcome[0] != "WELCOME":
        os.remove(key_file(args.radio))
        sys.exit("The service refused: %s" % " ".join(welcome))
    if not hmac.compare_digest(welcome[2],
                               proof(key, "server", args.radio, challenge, nonce)):
        sys.exit("Whatever answered at %s does not have the password." % args.server)
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
