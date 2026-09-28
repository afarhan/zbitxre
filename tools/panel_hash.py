#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
#
# panel_hash.py — gives a number to the hash the front panel puts in the
# receiver, under one setting of the panel after another.
#
#     tools/panel_hash.py <list of settings> <how to reach the radio>
#
# The radio program must be running on the radio with its requests taken from
# /tmp/sdr.in and its answers written to /tmp/sdr.out, and the panel must be
# on this computer's USB.
#
# It switches both of the radio's oscillators off, so that nothing reaches
# the sound card but what the radio makes for itself. Then for each setting
# it puts the panel in that setting, shows the setting's name on the panel,
# waits for the radio's spectrum to settle, and asks the radio to measure.
# At the end it puts everything back: both oscillators on, the panel painting
# all rows at 2 mA with its port at full speed.
#
# The list of settings is a Python list of (name, letters for the panel,
# words for the radio). The letters are the ones front_panel.ino takes on
# its serial port and the words are the ones tools/sdr.c takes.
#
# The second argument is a program that runs a command on the radio, as
# "ssh pi@radio" would.

import glob, os, shlex, sys, termios, time, subprocess

SETTLE=3
SECONDS=20
REACH=shlex.split(sys.argv[2])
def radio(words):
    cmd=' ; '.join('echo %s > /tmp/sdr.in; sleep 0.1' % w for w in words)
    subprocess.run(REACH+[cmd], check=False, capture_output=True)
def last_measured():
    r=subprocess.run(REACH+['grep "^measured:" /tmp/sdr.out | tail -1'], capture_output=True, text=True)
    return r.stdout.strip()
p=glob.glob('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*')
fd=os.open(p[0], os.O_RDWR|os.O_NOCTTY)
s=termios.tcgetattr(fd); s[0]=0; s[1]=0; s[2]=termios.CS8|termios.CREAD|termios.CLOCAL; s[3]=0; s[6][termios.VMIN]=0; s[6][termios.VTIME]=1
termios.tcsetattr(fd, termios.TCSANOW, s)
def drain(seconds):
    t=time.time()+seconds
    while time.time()<t:
        os.read(fd,4096)
def panel(letters, note):
    for l in letters:
        os.write(fd,l.encode()); drain(0.2)
    os.write(fd,('#'+note+'\n').encode())
# each: (name, panel letters, radio words)
runs=eval(open(sys.argv[1]).read())
radio(['vfo=off','bfo=off'])
for name,letters,words in runs:
    if words:
        radio(words)
    panel(letters,name)
    drain(SETTLE)
    radio(['measure=%d' % SECONDS])
    drain(SECONDS+1.5)
    print('%-22s %s' % (name, last_measured()), flush=True)
radio(['panel=on','spectrum=on','vfo=on','bfo=on'])
panel('Fa2w','')
os.write(fd,b'#\n')
drain(0.5)
print('done: painting all rows at 2 mA and 24 MHz, both oscillators on')
