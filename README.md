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
| GPIO pins (`hw/gpio.c`) and what each does (`hw/board.c`) | done, tested on a zbitx |
| Clock chip | the kernel's own driver keeps it; no code here |
| Sound (`hw/audio.c`) | next |
| The DSP of `sbitx.c` as `dsp/sdr.c` | after that |
| Controls, keyer, clients | after that |

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

This board is the only one supported so far: the zbitx, `hw=4` in the old
`hw_settings.ini`. It has no encoders or switches on the Pi's pins.

## Building and testing

    make          build everything into build/
    make check    run the tests that need no radio; works on a PC too
    make radio    run the tests that need the radio

Stop the radio program before `make radio`. Those tests take over the Si5351
and the pins while they run. They leave the clocks switched off and the radio
receiving, and they never turn the transmitter on.

To watch the key and PTT lines while you press them:

    build/test_board watch 20

## What was measured

On a zbitx (Pi Zero 2W, kernel 5.10.63), retuning the receiver's oscillator
300 times in 10 Hz steps:

| Driver | One retune |
|---|---|
| original, pins toggled from the program | 3.3 ms |
| this one, through the kernel | 1.5 ms |

The register values are the original's. Over 4,966,055 retunes across the
range, the two drivers wrote the same bytes to the same registers.

Reading a pin takes 81 ns and writing one 17 ns.

## Thanks

To JJ (W9JES) and his team, whose version of the zbitx software at
[drexjj/zbitx](https://github.com/drexjj/zbitx) found and fixed problems that
this one learns from. Every file that borrows their code or their technique
says so at its top.

To Gordon Henderson, whose wiringPi the radio used for its pins until now.
`hw/gpio.c` reaches the pins the way wiringPi does.

## Layout

    hw/       the radio's hardware: buses and chips
    tests/    one program per piece, each with its own main()

## Conventions

Plain C. Tabs. No `?:`. One statement per line, and the body of an `if` on the
line below it. A file's header comment explains how the file works; functions
carry no comments, because their names say what they do. Headers use
`#pragma once`. Every number that means something has a name. The code builds
clean under `-Wall -Wextra`.
