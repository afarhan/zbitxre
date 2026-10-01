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
| The receiver of `sbitx.c` as `core_sdr.c` | runs on a zbitx and is heard in the earphones; gives the same samples as zbitxv2 |
| The transmitter and the T/R sequence | next |
| Front panel firmware (`front_panel/`) | begun: the frequency, the volume, the spectrum and its waterfall; the hash it made in the receiver is cured |
| The link between the radio and the panel, over I2C | the panel's knob tunes the receiver, and the radio sends the panel the spectrum; it makes no noise in the receiver |
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

A second line, `dtoverlay=dwc2,dr_mode=host`, is for a keyboard and mouse on
the Pi, which the older USB driver loses. `setup/README.md` also says how to
find the radio on a network where its name does not resolve.

## Building and testing

    make          build the radio program here, and the tests into build/
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

## Working the radio from the desktop

The radio program, `sdr`, serves its controls on port 8081. `server.c`
says what is said there: lines of a label and a value, `FREQ 7074000`,
`BAND 20M`, `MODE CW`, `AUDIO 70`, sent by a client to change a setting and
sent by the radio to every client whenever a setting changes. It can be
tried by hand with `nc <radio> 8081`.

The settings themselves, and what each change does to the radio, are kept
by `radio.c`. Changes from the front panel and the terminal go through it
too, so every client, the panel and the desktop see the same radio.

`clients/gtk/` is a window for the desktop that fits an 800 by 600 screen.
On the left are the frequency, a line of the main settings, and the
spectrum and waterfall; on the right, a scrolling list of the controls,
made from the radio's schema. The mouse wheel over any of them moves it a
step, as the radio's knob would:

    make gui
    ./zbitx_gui 192.168.1.76

## The front panel

The zbitx's front panel is a Raspberry Pi Pico W with a touch screen and a
knob. Its firmware is the Arduino sketch in `front_panel/`, which draws with
LVGL. `front_panel/front_panel.ino` says what each file is.

    make panel          build it
    make panel-flash    put it on a Pico that is in its loader

Building needs `arduino-cli` with the `rp2040` core and the `lvgl` library,
version 9.3.0:

    arduino-cli core install rp2040:rp2040
    arduino-cli lib install lvgl@9.3.0

Once this firmware is on a panel, new firmware goes on over the cable with
nothing to press:

    arduino-cli upload -p /dev/ttyACM0 --fqbn rp2040:rp2040:rpipicow \
        --input-dir build/front_panel front_panel

To see what the panel has on its screen without looking at it:

    tools/panel_screen.py build/panel.png

The panel shows colour bars for four seconds as it starts: red, green, blue
and white, in two rows that must look the same, with a small white square in
the top left corner.

To put a Pico in its loader, hold the panel's knob down while the panel
powers up. It then shows up on USB as a drive called `RPI-RP2`. Use a cable
that is known to carry data.

Before putting new firmware on a panel, keep what it has:

    picotool save -a panel.bin

`picotool` comes with the `rp2040` core. `picotool load panel.bin` puts it
back, access points and touch calibration included.

## Running the radio

    ./sdr 7088000 LSB 75

opens the receiver on 7088 kHz, lower sideband, at a volume of 75, with a
passband of 300 to 3000 Hz. Those are also what it opens with when given
nothing. From then on the front panel's knob tunes it, and it takes one
request to a line from the terminal:

    r1:freq=7030000
    r1:mode=CW
    r1:low=400
    r1:high=900
    r1:agc=FAST
    smeter=0
    report
    panel=off
    panel=on
    spectrum=off
    spectrum=on
    vfo=off
    vfo=on
    bfo=off
    bfo=on
    measure=20
    quit

These are the requests of zbitxv2's `sdr_request()`, with the same names. A
request it does not know is answered `error unknown`.

As in zbitxv2, the passband does not follow the mode. After changing the
mode, ask for `r1:low` and `r1:high` again.

## The link to the front panel

The panel is on the same I2C bus as the Si5351, as a slave at 0x0a. What
passes between it and the radio is heard in the receiver, so the link says as
little as it can: four bytes on the bus when there is nothing to say, nine to
bring a new frequency. `front_panel/link.c` says how, and both the radio and
the panel's firmware are built with that one file. `hw/panel.c` says when the
radio polls.

Four times a second the radio sends the panel a line of the spectrum, 240
points across 25 kHz, which the panel shows as a graph and a waterfall. A
line is 245 bytes on the bus, about 22 ms.

Three things can be stopped and started one at a time, so that the noise each
makes in the receiver can be told by ear from the rest:

| What | Stopped by | Started by |
|---|---|---|
| the radio polling the panel | `panel=off` to the radio | `panel=on` |
| the radio sending the spectrum | `spectrum=off` to the radio | `spectrum=on` |
| the panel painting the waterfall | the letter `n` on the panel's serial port | the letter `w` |

What was heard on a zbitx, the antenna unplugged so that the AGC brought the
radio's own noise up: no hash from the link alone, with the knob being
turned. The hash came with the waterfall, and it went when the panel stopped
painting while the radio went on sending the spectrum. So it is the painting
of the display that makes it, and not the I2C bus. Driving the display's pins
at 2 mA with slow edges was some 3 dB quieter than 12 mA with sharp ones, and
the panel now uses 2 mA. Painting fewer rows was quieter still.

The radio can give the hash a number. With both oscillators off nothing
reaches the sound card but what the radio makes for itself, and `measure=20`
to the radio program averages that for 20 seconds, within 3 kHz of the tuned
frequency. `tools/panel_hash.py` runs through a list of settings and measures
each. On a zbitx, painting all of the waterfall four times a second:

| The display's pins | The display's port | Rise over the quiet radio | The hash, against the radio's own noise |
|---|---|---|---|
| not painting | | none | |
| 2 mA, slow edges | 24 MHz | 0.2 dB | 13 dB below |
| 2 mA, slow edges | 12 MHz | 0.1 dB | 16 dB below |
| 2 mA, slow edges | 6 MHz | too small to measure | |
| 12 mA, sharp edges | 24 MHz | 0.5 dB | 9 dB below |
| 12 mA, sharp edges | 12 MHz | 0.3 dB | 11 dB below |
| 12 mA, sharp edges | 6 MHz | 0.1 dB | 16 dB below |

Those are averages over 20 seconds, and the ear does not average. The author
found 24 MHz the least irritating, because at the slower rates each burst of
the hash lasts longer, and chose 24 MHz at 2 mA with a shallower waterfall,
which shortens the burst further. It was 160 lines deep. At 48 lines the hash
was gone, and at 96 it was barely to be heard above the radio's own static
with the antenna out. The author settled on 96. With the antenna in, on
21.074 MHz, the waterfall showed the FT8 signals and the band's noise was
above what was left of the hash. Sending each slice of the
painting as one unbroken stream, in place of stopping after every row, made
no difference that could be measured.

The four steady lines in the waterfall of a zbitx with no antenna are not
signals. They are spurs in the IF from the PLLs and clocks of the Pico and
the Raspberry Pi.

Readings of the quiet radio agree with each other within 0.1 dB. The link
itself, polling and sending the spectrum, adds nothing that can be measured.
Switching both oscillators off does not change the hash, and turning the
sound card's input gain to nothing makes it go: it gets in on the line from
the second IF to the sound card, around 28 kHz.

More letters on the panel's serial port, for finding what quietens it: `2`,
`4` and `9` drive the display's pins at 2 mA, 4 mA, and 12 mA with sharp
edges; `a`, `b` and `c` paint all of the waterfall, half of it, or the
graph alone; `F`, `H` and `Q` run the display's port at its full rate, 12 MHz
and 6 MHz. The panel shows which is chosen.

To try the link without the radio running:

    build/test_panel 20

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
four passbands, quiet, usual and very loud. With the transforms done by
FFTW in double precision, as zbitxv2 does them, the samples for the speaker,
the S meter and the oscillator's frequency were the same throughout, byte for
byte. With kiss_fft in single precision, as they are done now, three
quarters of the numbers are still the same, and where they differ the
difference is 127 dB below the sound.

The link to the panel, on a zbitx: in 20 seconds idle, 280 polls, every one
answered and none spoiled. With the receiver running and the knob turned
through 48 kHz, 840 frequencies arrived and the radio followed each; of 3158
polls none went unanswered and none was spoiled, and the sound card missed no
block.

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

To Bodmer, whose TFT_eSPI is where the display's start-up sequence and the
way the touch screen is read come from, and to the LVGL project, whose
library draws the front panel's screen.

To JJ (W9JES), whose version of the zbitx software at
[drexjj/zbitx](https://github.com/drexjj/zbitx) gave the form of the I2C
request used in `hw/i2c.c`.

## Licence

GPL version 3 or later. See `LICENSE`.

## Layout

    setup/    what the radio's Pi needs, and its boot configuration
    hw/       the radio's hardware: buses, chips and boards
    core_sdr.c   the radio itself: tuning, modes, the receiver
    dsp/      the filter and the tone generator, and sdr.h
    tools/    programs to run the radio by hand
    front_panel/   the firmware of the zbitx's front panel, an Arduino sketch
    tests/    one program per piece, each with its own main()

## Conventions

What is carried from zbitxv2 keeps its names. `core_sdr.c` is `sbitx.c`,
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
