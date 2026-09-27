# zbitxre

The zbitx radio's software, rebuilt one piece at a time so that it is small
enough to read and each piece can be tested on its own.

The radio it comes from is [zbitxv2](https://github.com/afarhan/zbitxv2). That
program works, and it stays the radio's daily software until this one can take
over. Nothing here changes it.

## Where it stands

| Piece | State |
|---|---|
| I2C through the kernel (`hw/i2c.c`) | done, tested on a zbitx |
| Si5351 clock generator (`hw/si5351.c`) | done, tested on a zbitx |
| GPIO pins (`hw/gpio.c`) | done, tested on a zbitx |
| What each pin does on a board (`hw/board.c`) | done for the zbitx's first board |
| Clock chip | the kernel's own driver keeps it; no code here |
| Sound card (`hw/audio.c`) | done, tested on a zbitx |
| The receiver of `sbitx.c` as `dsp/sdr.c` | runs on a zbitx; not yet compared with zbitxv2 sample for sample |
| The transmitter and the T/R sequence | next |
| Controls, keyer, clients | after that |

## How it relates to hermes-radio-daemon

[hermes-radio-daemon](https://github.com/Rhizomatica/hermes-radio-daemon) by
Rafael Diniz of Rhizomatica runs the same radios, headless, for the HERMES
network. zbitxre is a separate program with a different aim: an operator's
radio, with a keyer, in code small enough to read in an evening.

The hardware layer here is written to follow theirs. Where the two programs
meet the outside world they are meant to agree: the same rigctld behaviour,
and the same names for the controls wherever one name will do. A client
written for one should work with the other.

## Boards

Boards differ, so a board is a table in `hw/board.c`: a line for the
transmitter's power, one for the receiver's if the board has it, the low-pass
filters with the frequency each serves below, and the two inputs from the
jacks. Nothing else in the code knows how many lines a board has.

| Board | State |
|---|---|
| zbitx, first board | described and tested |
| zbitx, second board, which has one more filter | to be added |
| original sbitx, which has no receiver line | to be added |

The zbitx's first board has no encoder or switch on the Pi's pins. A board
that has them, as the sbitx does, will need them added to its table.

## Setting up the radio

The Si5351 and the DS3231 clock chip sit on two GPIO pins, not on the Pi's own
I2C pins. Add this line to `/boot/config.txt` and reboot:

    dtoverlay=i2c-rtc-gpio,ds3231,i2c_gpio_sda=13,i2c_gpio_scl=6

The numbers are BCM GPIO numbers: header pins 33 and 31, which wiringPi calls
23 and 22.

That one line does two things. It makes a kernel I2C bus of the two pins, on
which the radio finds the Si5351 by asking each bus in turn, so no bus number
is written anywhere. And it gives the clock chip to the kernel's driver: the
kernel sets the system's time from the chip when it starts, and writes the
time back to the chip while the network keeps it right. The radio has no clock
code of its own. To see the chip's time:

    sudo hwclock -r

## Building and testing

    make          build everything into build/
    make check    run the tests that need no radio; works on a PC too
    make radio    run the tests that need the radio

Stop the radio program before `make radio`. Those tests take over the Si5351,
the pins and the sound card while they run. They leave the clocks switched off
and the radio receiving, and they never turn the transmitter on.

To watch the key and PTT lines while you press them:

    build/test_board watch 20

To hear the sound card, a one second tone in the speaker:

    build/test_audio tone

## Running the radio

    build/sdr 7040000 LSB 75

opens the receiver on 7040 kHz, lower sideband, at a volume of 75, and then
takes one request to a line:

    r1:freq=7030000
    r1:mode=CW
    r1:low=400
    r1:high=900
    r1:agc=FAST
    smeter=0
    report
    quit

These are the requests of zbitxv2's `sdr_request()`, with the same names. A
request it does not know is answered `error unknown`.

## What was measured

On a zbitx (Pi Zero 2W, kernel 5.10.63), retuning the receiver's oscillator
300 times in 10 Hz steps:

| How the retune is sent | One retune |
|---|---|
| zbitxv2: pins toggled from the program, a transfer for each register | 3.3 ms |
| through the kernel, a transfer for each register | 3.7 ms |
| zbitxre: through the kernel, a transfer for each group of registers | 1.5 ms |

The register values are zbitxv2's. Over 4,966,055 retunes across the range,
the two drivers wrote the same bytes to the same registers.

Reading a pin takes about 80 ns and writing one about 20 ns.

The sound card, run with all four processors kept busy:

| | |
|---|---|
| blocks delivered in 10 s | 936 of 937 |
| longest wait for a block | 10.74 ms, a block being 10.67 ms |
| queued to play after a write | 2028 frames, a block being 1024 |
| times the card overflowed or ran dry | 0 |
| writing one level | 0.32 ms |

Then one block was held back for 25 ms, longer than anything queued. The card
carried on, the gap was counted once, the blocks that had piled up were
dropped, and 2032 frames were queued afterwards: the delay through the radio
came back to what it was.

The four levels, speaker, transmitter, receiver and microphone, are separate
channels of the chip. Each was moved alone and the other three stayed put, so
no level changes when the radio goes between receive and transmit.

## Thanks

To Rafael Diniz and Rhizomatica, whose
[hermes-radio-daemon](https://github.com/Rhizomatica/hermes-radio-daemon) the
hardware layer follows: the kernel's I2C bus, the way the pins are set up, the
sound card's periods and levels, and playing that carries on through a late
block. Every file that follows theirs says so at its top.

To Raspberry Pi Ltd, whose gpiolib gives the order in which the pin registers
are written, and to Gordon Henderson, whose wiringPi the radio used for its
pins until now.

To Phil Karn, KA9Q, whose ka9q-radio is where the filter comes from, and
whose method of filtering by fast convolution the receiver follows.

To JJ (W9JES), whose version of the zbitx software at
[drexjj/zbitx](https://github.com/drexjj/zbitx) gave the form of the I2C
request used in `hw/i2c.c`.

## Licence

GPL version 3 or later. See `LICENSE`.

## Layout

    hw/       the radio's hardware: buses, chips and boards
    dsp/      the radio itself: tuning, modes, filters, the receiver
    tools/    programs to run the radio by hand
    tests/    one program per piece, each with its own main()

## Conventions

Plain C. Tabs. No `?:`. One statement per line, and the body of an `if` on the
line below it. A file's header comment explains how the file works; functions
carry no comments, because their names say what they do. Headers use
`#pragma once`. Every number that means something has a name. The code builds
clean under `-Wall -Wextra`.
