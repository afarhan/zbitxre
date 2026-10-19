# Working on zbitxre with Claude

zbitxre is written by its author, Ashhar Farhan VU2ESE, with Claude, an AI
model by Anthropic, working in Claude Code, a program that runs in a
terminal, reads and changes the code, builds it, runs its tests, and talks
to the radio over the network. The author decides what the radio should do
and how it should feel to use; Claude reads, writes, measures and tests;
the author reads what was done and says yes or no. This chapter is for
anyone who wants to change the software the same way.

## What you need

- A Linux computer, with the build tools: `gcc`, `make`, `libasound2-dev`,
  `libsqlite3-dev`, and `libgtk-3-dev` for the GTK client.
- **Claude Code**, from [claude.com/claude-code](https://claude.com/claude-code),
  and an account to use it with.
- The repository:
  `git clone https://github.com/afarhan/zbitxre.git`
- A radio on your network is a great help, but much can be done without one
  (see "Without a radio" below).

Start Claude Code in the repository's folder, `claude`, and talk to it in
plain words.

## How a change is made here

The way the author works has settled into a few habits. They are worth
keeping, whoever is at the keyboard.

**Say what you want, and why.** "The knob on the sBitx should change the
selected control again, as it used to" is a good request. The reason goes
into the code: every file opens with a comment that says how it works and
why it is the way it is, often in the words of the person who asked. When
you later ask "why does this wait for TX OFF?", the answer is in the file.

**Design first, in prose.** For anything larger than a fix, ask Claude to
lay out the whole design in a few paragraphs before it writes code, and
agree it. The registration form that adds the radio, and the update that
the radio runs itself, were both agreed this way.

**Prefer the direct way.** When something goes wrong because of a
roundabout design, make the design direct rather than patching around it.
The update was once run by a separate script that stopped and restarted the
radio; it is now a child of the radio, read through a pipe, and the radio
restarts itself.

**Keep it simple for hams.** Plain ON and OFF switches, names that say what
they do, and no layers of options nobody asked for.

**Build and test before saying it is done.**

```
make            # the radio, the tests and the tools
make check      # every test
make gui        # the GTK client
make remote     # the gateway to my.sbitx.net
make panel      # the front panel's firmware (needs arduino-cli)
python3 accounts/test_accounts.py    # the website
```

Ask Claude to say plainly what was verified and what was not. "Builds and
passes its tests; not yet tried on a radio" is a perfectly good answer, and
a much better one than a confident "done".

**Commits** have a subject of the form `area: a plain sentence`, then a
paragraph or two saying why and what was found, then what was verified and
what was not. Ask Claude to write them so.

## The conventions of the code

For code that is new:

- plain C, with tabs;
- no `?:`, and one statement per line, the body of an `if` on the line
  below it;
- a file's header comment explains how the file works; functions carry no
  comments, because their names say what they do;
- names in plain technical terms that say what a thing is; `static` unless a
  function is part of a module's interface;
- headers use `#pragma once`; every number that means something has a name;
- it builds clean under `-Wall -Wextra`.

What is carried over from the older zbitxv2 software keeps its old names
(`sbitx.c` became `core_sdr.c`, but `rx_linear()` and `fft_in` are still
called that), so that anyone who knows the old code finds their way.

`CLAUDE.md`, at the top of the repository, is read by Claude at the start
of every session. It holds the standing decisions, such as leaving the CW
latency at 25 ms. Add to it when you make a decision Claude must not undo.

## Without a radio

Most of the radio can be run on a desktop:

- every piece has a test in `tests/`, a program of its own run by
  `make check`;
- `tests/hw_stand_ins.c` stands in for the radio's hardware: the pins, the
  I2C bus and the sound card. The radio program built against it runs on a
  laptop, and the GTK client connects to it as to a radio. The pictures of
  the GTK client in this manual were taken that way, with a made-up band played into
  the stand-in;
- the website and the service run on a laptop too: `accounts/README.md`
  says how.

Ask Claude to try a change against a stand-in before it goes near a radio.

## With a radio

- **Let Claude reach the radio over ssh**, with a key, as user `pi`. It can
  then read the radio program's output, look at the panel's screen, and
  measure.
- **Say which radios it may change.** A radio you use every day can be
  marked "look, do not touch": Claude will then only read logs and state.
- **Transmitting is yours.** Claude should not key a transmitter unless you
  ask, and should check the radio is not transmitting before restarting it.
- **The front panel's screen** can be fetched over its USB cable,
  `tools/panel_screen.py`, and Claude can look at the picture.

## How a change reaches the radios

Changes go to GitHub, and **radios update from GitHub**, never by copying
files onto them. On each radio, SET then **UPDATE** fetches, builds and
restarts (chapter 3). Update every radio you test on together, so that none
is left behind on an old version.

The website, my.sbitx.net, is deployed by copying its files to the server,
with a backup taken first; `accounts/README.md` says where things live.

## Good requests to start with

- "Read `server.c` and tell me how a client is told of a change."
- "The macro buttons should show eight macros on a wide screen. Design it
  first."
- "Add a test that the logbook keeps a search when an entry is deleted."
- "Run the GTK client against the stand-in and take a picture of SET."
- "Write the commit for this, and say what was not tried on a radio."
