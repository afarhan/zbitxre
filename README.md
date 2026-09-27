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
| Sound, and the DSP of `sbitx.c` as `dsp/sdr.c` | next |
| Controls, keyer, clients | after that |

## Setting up the radio

The Si5351 and the DS3231 clock chip sit on two GPIO pins, not on the Pi's own
I2C pins. Add this line to `/boot/config.txt` and reboot, and the kernel
presents them as `/dev/i2c-3`:

    dtoverlay=i2c-gpio,bus=3,i2c_gpio_sda=13,i2c_gpio_scl=6

The numbers are BCM GPIO numbers: header pins 33 and 31, which wiringPi calls
23 and 22.

## Building and testing

    make          build everything into build/
    make check    run the tests that need no radio; works on a PC too
    make radio    run the tests that need the radio

Stop the radio program before `make radio`. That test takes over the Si5351
while it runs, and leaves its clocks switched off.

## What was measured

On a zbitx (Pi Zero 2W, kernel 5.10.63), retuning the receiver's oscillator
300 times in 10 Hz steps:

| Driver | One retune |
|---|---|
| original, pins toggled from the program | 3.3 ms |
| this one, through the kernel | 1.5 ms |

The register values are the original's. Over 4,966,055 retunes across the
range, the two drivers wrote the same bytes to the same registers.

## Thanks

To JJ (W9JES) and his team, whose version of the zbitx software at
[drexjj/zbitx](https://github.com/drexjj/zbitx) found and fixed problems that
this one learns from. Every file that borrows their code or their technique
says so at its top.

## Layout

    hw/       the radio's hardware: buses and chips
    tests/    one program per piece, each with its own main()

## Conventions

Plain C. Tabs. No `?:`. One statement per line, and the body of an `if` on the
line below it. A file's header comment explains how the file works; functions
carry no comments, because their names say what they do. Headers use
`#pragma once`. Every number that means something has a name. The code builds
clean under `-Wall -Wextra`.
