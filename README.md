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
| The receiver of `sbitx.c` as `dsp/sdr.c` | runs on a zbitx and is heard in the earphones; gives the same samples as zbitxv2 |
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

`setup/README.md` says what a radio's Raspberry Pi needs before zbitxre will
run, why, and how to check it. `setup/config.txt` is the whole boot
configuration of a working radio.

zbitxre adds one line to the boot configuration:

    dtoverlay=i2c-rtc-gpio,ds3231,i2c_gpio_sda=13,i2c_gpio_scl=6

It gives the Si5351 and the clock chip to the kernel, which makes an I2C bus
of their two pins and keeps the clock itself. `setup/add-overlay.sh` adds the
line and keeps a copy of the file as it was.

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

To show that the receiver gives the same samples as zbitxv2's, with a copy of
zbitxv2's source at hand:

    tests/against_zbitxv2/run.sh ~/zbitxv2

It needs no radio. `tests/against_zbitxv2/script.c` says what is compared.

## Running the radio

    build/sdr 7040000 LSB 75

opens the receiver on 7040 kHz, lower sideband, at a volume of 75, with a
passband of 300 to 3000 Hz, and then takes one request to a line:

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

As in zbitxv2, the passband does not follow the mode. After changing the
mode, ask for `r1:low` and `r1:high` again.

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

The receiver was run beside zbitxv2's (commit 2726c57) on the same samples:
5220 blocks, through USB, LSB, CW and CWR, the four speeds of the AGC and
four passbands, quiet, usual and very loud. The samples for the speaker, the
spectrum, the S meter and the oscillator's frequency were the same throughout,
byte for byte.

AM is where zbitxre differs from zbitxv2 on purpose. The AM filter goes
around the carrier of the station tuned in, which is on the board's centre
bin: 28.125 kHz on the zbitx. zbitxv2 has it around 24 kHz on every board.
`make check` plays a station at each place and hears only the one on the
centre bin.

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

    setup/    what the radio's Pi needs, and its boot configuration
    hw/       the radio's hardware: buses, chips and boards
    dsp/      the radio itself: tuning, modes, filters, the receiver
    tools/    programs to run the radio by hand
    tests/    one program per piece, each with its own main()

## Conventions

What is carried from zbitxv2 keeps its names. `dsp/sdr.c` is `sbitx.c`,
`dsp/filter.c` is `fft_filter.c` and `dsp/vfo.c` is `vfo.c`, with the
functions, the arrays and the variables called what they have been called for
years: `rx_linear()`, `sound_process()`, `fft_in`, `fft_out`, `fft_m`,
`rx_list`, `tuned_bin`. Anyone who knows the old code, or has read what was
written about it, will find their way.

The rest is for code that is new. Plain C. Tabs. No `?:`. One statement per line, and the body of an `if` on the
line below it. A file's header comment explains how the file works; functions
carry no comments, because their names say what they do. Headers use
`#pragma once`. Every number that means something has a name. The code builds
clean under `-Wall -Wextra`.
