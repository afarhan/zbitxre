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

It works only where the access point carries mDNS. A home router tried on
29 September 2026 did not. On it, and for the first few minutes after a
device joined a phone's hotspot, the name resolved; after that it did not,
on two different zbitx radios. The radio was not at fault.

Where the name does not resolve, the radio has to be found by its address.

Every radio made from the same system image is called `sbitx`. Two of them
on one network will not both be `sbitx.local`: avahi renames the second
`sbitx-2.local`, and says so only in its log.

### No access point of its own

A zbitx is a WiFi client only, the author's ruling of 16 October 2026. Many
were shipped as an access point too, called `zbitx`, at 192.168.4.1, made by
three services, `hostapd`, `uap0` and `dnsmasq`, for the front panel when the
panel was on WiFi. The panel is on I2C now. The access point was on another
channel from the client's, and the one WiFi chip hopped between the two: on
the test radio more than half of what it sent out was lost, and VoIP and its
sound to my.sbitx.net came through broken. With the three turned off, none
was lost. `install.sh` turns them off, and `undo_install.sh` turns them back
on for the old program, whose panel may want them. By hand:

    sudo systemctl disable --now hostapd uap0.service dnsmasq

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

    sudo apt-get install gcc make libasound2-dev

The transforms are kiss_fft's, whose source is in `dsp/kiss_fft`, so there is
no library to install for them.

zbitxre does not need FFTW, wiringPi, GTK, sqlite, or the `snd-aloop` loopback
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

## Starting zbitxre with the desktop

On a radio with a screen of its own, as the sbitx has, zbitxre is started
each time the desktop comes up: the radio program in a terminal, where what
it prints can be read, and the window, maximised. Two files here do it.

`start.sh` starts the window, `zbitx_gui`, and then runs the radio program,
`sdr`, in the terminal it was started in. Neither is started if it is
already running. The window tries the radio program every three seconds
until it answers.

`zbitxre.desktop` is the desktop's entry that runs `start.sh` in a terminal.
It is put where the desktop looks at login, once, after `make` and
`make gui` in `~/zbitxre`:

    cp setup/zbitxre.desktop ~/.config/autostart/

The entry names `/home/pi/zbitxre`; change it if the repository is kept
elsewhere. The old program's entry must be out of the way first, as above.
To stop zbitxre starting by itself, rename the entry to
`zbitxre.desktop.disabled`.

On the sbitx it was put in on 8 October 2026.

### The icon

The **zbitxre** icon, on the desktop and in the menu among the Internet
programs, opens the window again once it has been closed: `open.sh` starts
the window if it is not there, and when the radio program is not running
either, runs `start.sh` in a terminal as at login. `desktop_icon.sh` puts
the icon where it is missing; `install.sh` runs it, and so does every
update, so a radio set up before there was an icon has one after its next
update. The picture is `zbitxre.png`, the website's.

## Updating a radio from GitHub

A radio is brought up to GitHub's main from SET, by Update: the radio runs
`setup/update.sh --if-new`, shows each step in a view of the update, and,
when something new was built, waits until it is not transmitting, restarts
itself with it in the same terminal and gives the front panel its firmware.
CANCEL in the view stops it and leaves the radio as it was.

The same can be done by hand, in a terminal on the radio:

    ~/zbitxre/setup/update.sh

which pulls main, fetches RADE's weights if any are missing, and builds the
radio program, the window and zbitx_remote; the radio uses them when it is
next started. The script's header says what it will not do: it does not
overwrite work of the radio's own, a failed build leaves the programs that
were there, and nothing in `data/` is reached.

The repository is private, so each such radio reads it with a key of its
own, which can read this repository and nothing else, and cannot push.
Once, on the radio:

    ssh-keygen -t ed25519 -N "" -C <name> -f ~/.ssh/github_zbitxre
    printf 'Host github.com\n\tIdentityFile ~/.ssh/github_zbitxre\n\tIdentitiesOnly yes\n' >> ~/.ssh/config
    cat ~/.ssh/github_zbitxre.pub

and the line printed is added on GitHub, in the repository's Settings,
Deploy keys, under the tester's name, with "Allow write access" left
unticked. GitHub asks the owner to confirm it. Then:

    git clone git@github.com:afarhan/zbitxre.git ~/zbitxre

If the radio is lost, its key is deleted on GitHub and nothing else need
change.

The first was the sbitx of Sasi, key "sasi", on 14 October 2026.

## What zbitxre keeps on the radio

`data/user_settings.ini`, the radio's settings, a line `NAME=value` for
each, which the radio writes as they change. One of them is changed by
editing the file and nowhere else:

    SERVER=my.sbitx.net

the service the radio is put on line with; with no such line it is
my.sbitx.net. Anyone may run the service, `accounts/`, on a server of
their own, and put its name here; the radio takes it at its next start.
The gateway, `zbitx_remote`, connects there, the clock is set from it, and
the panel's firmware is fetched from it. No client may change it, since a
wrong one set from afar would leave the radio unreachable.

`data/snapshot.raw`, when "snapshot" has been asked for in the radio's
terminal: the samples of sixteen blocks as the sound card gave them. It is
not kept in the repository.
