# Setting up a radio for zbitxre

What a zbitx needs on its Raspberry Pi before zbitxre will run, why each
thing is there, and how to check it. Everything here was read from a working
radio on 28 September 2026.

| | |
|---|---|
| Computer | Raspberry Pi Zero 2 W |
| System | Raspbian GNU/Linux 10 (buster), 32 bit |
| Kernel | 5.10.63-v7+ |
| Board | zbitx, first board |

## The boot configuration

`config.txt` in this folder is the radio's whole `/boot/config.txt`, as it
stands with zbitxre working. The lines that matter to the radio are these.

| Line | What it does |
|---|---|
| `dtparam=i2c_arm=on` | Turns on the Pi's own I2C bus, which carries the controls of the sound chip. |
| `dtparam=i2s=on` | Turns on the Pi's I2S port, which carries the sound itself. |
| `dtoverlay=audioinjector-wm8731-audio` | Loads the kernel's driver for the WM8731 sound chip and makes it a sound card. |
| `dtoverlay=i2s-mmap` | Lets programs reach the sound card's buffer directly. |
| `gpio=4,5,9,10,11,17,22,27=ip,pu` | Makes these pins inputs with pull-ups from the moment the Pi starts. Pins 4 and 5 are the PTT and the key. |
| `gpio=24,23=op,pu` | Makes these pins outputs from the moment the Pi starts. Pin 23 is the transmitter's line. |
| `dtoverlay=i2c-rtc-gpio,ds3231,i2c_gpio_sda=13,i2c_gpio_scl=6` | Added for zbitxre. See below. |
| `dtoverlay=dwc2,dr_mode=host` | Added for a keyboard and mouse on the Pi's USB. The radio itself does not need it. See below. |

Pin numbers are BCM GPIO numbers throughout.

### The line zbitxre adds

    dtoverlay=i2c-rtc-gpio,ds3231,i2c_gpio_sda=13,i2c_gpio_scl=6

The Si5351 and the DS3231 clock chip are wired to GPIO 13 and GPIO 6, which
are header pins 33 and 31, and not to the Pi's own I2C pins. zbitxv2 drove
those two pins from the program. This line gives them to the kernel instead,
and does two things.

It makes an I2C bus of the two pins. On the test radio it came up as
`/dev/i2c-11`. The number is the kernel's to choose, so zbitxre never names
it: it asks each bus in turn for the Si5351 and uses the one that answers.

It loads the kernel's clock driver, `rtc-ds1307`, for the DS3231. The kernel
then sets the system's time from the chip when it starts, before any network
is up, and writes the time back to the chip while the network keeps the
system's clock right. zbitxre has no clock code of its own.

`add-overlay.sh` adds the line to a boot configuration that lacks it, and
keeps a copy of the file as it was:

    sudo setup/add-overlay.sh
    sudo reboot

### The line for a keyboard and mouse

    dtoverlay=dwc2,dr_mode=host

The radio does not use the Pi's USB. This is for working on the Pi with a
keyboard and a mouse.

The Pi's older USB driver, `dwc_otg`, does badly with a full-speed device
such as the receiver of a wireless keyboard. Plugged straight into the Pi,
the receiver was not found on 10 starts out of 16. Through a USB 2.0 hub it
was found on five starts running, and then on one start the hub reported
nothing plugged into it, though the receiver was there: releasing the hub
and taking it up again found it at once. On a start that ran overnight the
kernel had to reset the receiver 114 times.

The line replaces that driver with the newer one, `dwc2`. It was tried on 29
September 2026 and the receiver was found on the next start. One start
proves little about a fault that comes and goes. To see how each start of
the radio has done:

    sudo grep -a "Booting Linux\|idVendor=046d\|reset full-speed" /var/log/kern.log

A copy of the boot configuration from before the line was added is kept on
the radio as `/boot/config.txt.before-dwc2`.

If the keyboard is lost while the radio is running, this finds it again
without a restart:

    echo 1-1 | sudo tee /sys/bus/usb/drivers/usb/unbind
    echo 1-1 | sudo tee /sys/bus/usb/drivers/usb/bind

## Finding the radio on the network

The radio's name is `sbitx`, and its mDNS service, avahi, announces it as
`sbitx.local`. That is how it is reached, by `ssh` and by anything that
controls it from afar:

    ssh pi@sbitx.local

Whether the name resolves depends on the access point. mDNS is asked and
answered by multicast, and an access point that does not pass multicast
from one wireless device to another breaks it for every device on it.

This was met on 29 September 2026. With the radio and a computer both on
one access point, a home router from an internet provider, the name did not
resolve. The radio's avahi answered when asked directly and its WiFi had
joined the mDNS groups, but nothing sent to 224.0.0.251 or to
239.255.255.250 passed between the two in either direction, while ordinary
traffic, broadcasts and IPv6 neighbour discovery did. Both were moved to
another access point and the name resolved at once, with nothing changed
on the radio or the computer.

None of these made any difference on the first access point, so none of
them is a cure: stopping the radio's own access point, turning off the
WiFi's power saving, letting the WiFi take all multicast, and having avahi
leave the groups and join again.

To tell whether an access point passes mDNS, ask the radio directly, which
does not need multicast. If this answers and the name still does not
resolve, the access point is the cause:

    dig +short @<the radio's address> -p 5353 sbitx.local

### The radio's own access point

The radio can be an access point itself, called `zbitx`, at 192.168.4.1.
It was there for the front panel when the panel was on WiFi. The panel is
on I2C now, and on the test radio the three services that make the access
point are disabled. To have it back:

    sudo systemctl enable hostapd uap0.service dnsmasq
    sudo reboot

## Kernel modules

`/etc/modules` holds one line, `i2c-dev`, which gives programs the
`/dev/i2c-*` devices.

The overlays load the rest: `snd_soc_wm8731` and
`snd_soc_audioinjector_pi_soundcard` for the sound card, `i2c_gpio` and
`i2c_algo_bit` for the bus, `rtc_ds1307` for the clock.

## What the program's user needs

The user that runs zbitxre must belong to three groups.

| Group | For |
|---|---|
| `audio` | the sound card, and real-time priority |
| `i2c` | `/dev/i2c-*` |
| `gpio` | `/dev/gpiomem`, the pins |

The user `pi` belongs to all three on a standard system.

Real-time priority comes from `/etc/security/limits.d/audio.conf`, which
allows the `audio` group a priority of up to 95:

    @audio   -  rtprio     95
    @audio   -  memlock    unlimited

zbitxre's audio thread asks for 90. A request above the limit is refused
outright, and the thread then runs as an ordinary one.

## What it takes to build

    sudo apt-get install gcc make libasound2-dev libfftw3-dev

FFTW is needed in both its double and its single precision forms. On the test
radio both the packaged FFTW 3.3.8 and one built from source in `/usr/local`
are present, and either will do.

zbitxre does not need wiringPi, GTK, sqlite, or the `snd-aloop` loopback
cards that zbitxv2 sets up.

## Checking a radio

| Command | What a working radio shows |
|---|---|
| `cat /proc/asound/cards` | a card named `audioinjectorpi` |
| `ls /dev/i2c-*` | `/dev/i2c-1` and one more |
| `cat /sys/class/i2c-adapter/i2c-*/name` | one named `i2c-gpio-rtc@0` |
| `sudo hwclock -r` | the time, from the clock chip |
| `timedatectl` | a line `RTC time:` with the time in it |
| `dmesg \| grep rtc` | `rtc-ds1307 ... registered as rtc0` |
| `ulimit -r` | `95` |
| `ls -l /dev/gpiomem` | group `gpio`, readable and writable by it |

Then, with the old radio program stopped:

    make
    make check
    make radio

`make radio` tests the Si5351, the board's pins and the sound card on the
radio itself. It never turns the transmitter on.

## The old program

zbitxv2 and zbitxre cannot run together: both want the sound card and the
Si5351. On the test radio zbitxv2 no longer starts by itself, because its
desktop entry was renamed:

    ~/.config/autostart/sbitx.desktop.disabled

Rename it back to `sbitx.desktop` to have zbitxv2 start at login again.

zbitxv2 still works with the overlay line in place.

## What zbitxre keeps on the radio

`data/sbitx_wisdom.wis` and `data/sbitx_wisdom_f.wis`, made on the first run. They record the
fastest way this processor does the radio's transforms, so that later runs
start at once. They belong to the machine they were made on and are not kept
in the repository.
