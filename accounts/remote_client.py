#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
remote_client.py — a radio reached through my.sbitx.net, from a terminal.

    remote_client.py                 the owner's radios, and which are on line
    remote_client.py <key id>        that radio: what it says is printed, and
                                     what is typed is sent to it, as with nc
    remote_client.py first           the first radio that is on line

It is the smallest client there can be, written to try the service with and
as a model for a client on any platform: PROTOCOL.md says what is spoken,
and this is all of it in a page.

The first time it is run, this computer has no key. It asks the website
for a code and prints it; the owner signs in at my.sbitx.net, chooses to
link a radio or a client, and types the code. The key then comes back and
is kept in ~/.config/zbitx/client.key, which only this user can read.

--web and --server point it at another service than my.sbitx.net.
"""

import argparse
import hashlib
import hmac
import json
import os
import secrets
import socket
import sys
import threading
import time
import urllib.request

KEY_FILE = os.path.expanduser("~/.config/zbitx/client.key")


def ask(web, path, asked):
    request = urllib.request.Request(web + path, json.dumps(asked).encode(),
                                     {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=20) as answer:
        return json.load(answer)


def link(web):
    started = ask(web, "/api/radio/link", {"name": socket.gethostname(), "kind": "client"})
    print("To link this computer, sign in at %s and type the code  %s"
          % (started["verify_url"], started["user_code"]))
    ends = time.time() + started["expires_in"]
    while time.time() < ends:
        time.sleep(started["interval"])
        answer = ask(web, "/api/radio/link/poll", {"device_code": started["device_code"]})
        if answer["status"] == "linked":
            os.makedirs(os.path.dirname(KEY_FILE), mode=0o700, exist_ok=True)
            with open(os.open(KEY_FILE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), "w") as f:
                f.write("%s %s\n" % (answer["key_id"], answer["key"]))
            print("Linked, as %s." % answer["callsign"])
            return
        if answer["status"] != "pending":
            break
    sys.exit("The code ran out. Run this again for another.")


def proof(key, side, challenge, nonce):
    text = "zbitx-auth-1 %s %s %s" % (side, challenge, nonce)
    return hmac.new(key.encode(), text.encode(), hashlib.sha256).hexdigest()


def connect(server, port):
    with open(KEY_FILE) as file:
        key_id, key = file.read().split()
    line = socket.create_connection((server, port), timeout=20)
    said = line.makefile("r", newline="\n")
    challenge = said.readline().split()[1]
    nonce = secrets.token_hex(16)
    line.sendall(("AUTH %s %s %s\n" % (key_id, nonce,
                                       proof(key, "client", challenge, nonce))).encode())
    welcome = said.readline().split()
    if len(welcome) != 3 or welcome[0] != "WELCOME":
        sys.exit("The service refused this computer's key: %s" % " ".join(welcome))
    if not hmac.compare_digest(welcome[2], proof(key, "server", challenge, nonce)):
        sys.exit("Whatever answered at %s does not have this computer's key." % server)
    return line, said, welcome[1]


def radios(line, said):
    line.sendall(b"radios\n")
    found = []
    while True:
        words = said.readline().split(None, 3)
        if not words or words[:2] == ["RADIOS", "END"]:
            return found
        found.append((words[1], words[2], words[3] if len(words) > 3 else ""))


def main():
    parser = argparse.ArgumentParser(description="a radio reached through my.sbitx.net")
    parser.add_argument("radio", nargs="?")
    parser.add_argument("--web", default="https://my.sbitx.net")
    parser.add_argument("--server", default="my.sbitx.net")
    parser.add_argument("--port", type=int, default=8081)
    args = parser.parse_args()
    if not os.path.exists(KEY_FILE):
        link(args.web)
    line, said, callsign = connect(args.server, args.port)
    found = radios(line, said)
    if args.radio is None:
        print("%s's radios:" % callsign)
        for key_id, state, name in found:
            print("  %-14s %-8s %s" % (key_id, state, name))
        return
    wanted = args.radio
    if wanted == "first":
        on_line = [key_id for key_id, state, _ in found if state == "online"]
        if not on_line:
            sys.exit("None of %s's radios is on line." % callsign)
        wanted = on_line[0]
    line.sendall(("connect %s\n" % wanted).encode())
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
