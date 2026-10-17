# The radio's protocol, for clients

This chapter is for anyone writing a program or a script that works the
radio. It covers the protocol that the radio program, `sdr`, speaks to its
clients on TCP port 8081, and the RTP sound streams on the radio itself. It
does not cover the internet side, my.sbitx.net. That is in `PROTOCOL.md`
(see the end of section 1).

Everything here was taken from the source at the time of writing. Where this
chapter and the source differ, the source is right. These are the files to
read:

| File | What it holds |
|---|---|
| `server.c` | The TCP server. Its header is the fullest account of the protocol. |
| `control.c`, `control.h` | The table of controls, their kinds, checking and who is told |
| `radio.c` | The radio's own controls: `FREQ`, `BAND`, `MODE` and the rest |
| `tools/sdr.c` | The program that opens the port, sends the spectrum and owns the program's own controls |
| `rtp_audio.c`, `rtp_microphone.c`, `rtp_voip.c` | The sound streams |
| `clients/gtk/zbitx_gui.c` | The desktop window, a complete client |
| `clients/remote/zbitx_remote.c` | The gateway: one more client of `sdr` on 127.0.0.1 |

---

## 1. Overview

- **Port.** TCP 8081 (`SERVER_PORT` in `server.h`). The server binds to every
  interface of the radio (`INADDR_ANY`).
- **Transport.** Lines of text, each ending in a newline (`\n`). The radio
  sends `\n` only. A `\r` from a client is ignored, so `\r\n` works too. There
  is no binary framing. The spectrum is sent as hex digits on a text line too.
- **Who may connect.** Anyone who can reach port 8081. There is no password and
  no login on this port (`PROTOCOL.md`: "There is no authentication.").
- **How many.** At most `SERVER_CLIENTS_MAX`, which is 8, at once. A ninth
  connection is accepted and then closed at once, with nothing said. The window
  on the radio's screen, the gateway `zbitx_remote`, and each visitor the
  gateway carries in from my.sbitx.net each take one of the eight places.
- **Everyone sees everyone's changes.** A change made by any client, by the
  front panel, or by typing on `sdr`'s terminal is sent to every other
  connected client. That is how the desktop window follows the panel's knob.
  The radio does not tell a client about its own change. Section 2 gives the
  rules.
- **Nothing is polled.** The radio sends a line when something changes, and
  nothing when nothing changes. A client never has to ask "what is the
  frequency now?".
- **Several programs talk this protocol.** The desktop window
  (`clients/gtk/zbitx_gui.c`) and the gateway (`clients/remote/zbitx_remote.c`)
  connect to it directly. So can `nc <radio> 8081` typed by hand. The web page
  (`accounts/static/operate.js`) speaks the same lines, carried over a WebSocket
  through my.sbitx.net and the gateway.

**The internet side.** A client can also reach a radio from anywhere through
my.sbitx.net. The client first proves who it is to the service (`CHALLENGE`,
`AUTH`, `WELCOME`, `CONNECTED`). After that, every line is this same protocol,
as if the client had connected to the radio itself. How that works is in
[`PROTOCOL.md`](../../PROTOCOL.md) at the top of the repository: the handshake,
the gateway, the sound across the internet, VoIP and the radio's cookie. It is
not repeated here.

---

## 2. Lines

### The form of a line

A line is a name, one space, and a value:

    NAME value

The server takes everything before the first space as the name, and
everything after it, as it stands, as the value. A value may contain spaces.
`CW_TEXT cq de vu2ese` sets `CW_TEXT` to `cq de vu2ese`.

- **Case matters.** Names are compared exactly (`strcmp`). Control names are
  in capitals: `FREQ`, `MODE`. Choices are compared exactly too: `MODE usb` is
  refused. The few requests that are not controls (section 3) are in lower
  case: `schema`, `spectrum on`.
- **An empty value.** `MYCALLSIGN ` (with the space) sets the text to
  nothing. `MYCALLSIGN` with no space is an error (see below). The radio sends
  an empty value the same way, as the name followed by one space.
- **Empty lines** are ignored.

### Setting a control

Send its name and the new value:

    FREQ 7074000
    MODE CW
    AUDIO 80
    MYCALLSIGN VU2ESE

`control_set()` in `control.c` checks the value against the control's kind:

| Kind | Accepted |
|---|---|
| number | A whole decimal number from min to max. It is snapped down onto the steps: `min + (n - min) / step * step`. |
| choice | Exactly one of the choices |
| text | Anything |
| action | One of its choices. An action with no choices takes any value, and its own code decides. |

### What the radio sends back

There is no "OK". These rules come from `control.c` and `server.c`, and
`tests/test_server.c` checks them:

1. **A value that is taken as sent** is told to every *other* client as
   `NAME value`. The sender is told nothing. It already knows.
2. **A value that the control already has** changes nothing, and nobody is
   told, not even with an error.
3. **A value that was not taken as sent** is told to everyone, the sender
   included. This happens when a number is snapped onto its steps, or when text
   is cut to fit. For example, on a control with steps of 5, `STEPPED 17` comes
   back to everyone as `STEPPED 15`.
4. **Other controls that a change moves** are told to everyone, the sender
   included. `BAND 20M` moves `FREQ` and `MODE`. The sender is sent `FREQ
   14200000` and `MODE USB`, but not `BAND`. Tuning out of every amateur band
   sends `BAND OUT` to everyone.
5. **An action** (kind `action`) is done each time it is set, and nobody is
   told. Actions have no value and are never sent as `NAME value` lines.
6. **A refused line** is answered to the sender alone with `ERROR`.

Changes made on the front panel, on `sdr`'s terminal, or by the radio itself
(the PTT setting `TX ON`, `CLOCK` each minute, the meters) are sent to every
client.

**A client must never send a change because it was told of one.** It shows
the new value and does nothing else. This rule, together with rule 2, keeps a
change from going round in a loop (`server.c`, header).

### Errors

The form is `ERROR`, the name, and the reason:

    ERROR <name> <reason>

These are the reasons the source gives:

| Cause | Line sent |
|---|---|
| No space in the line, and not a request from section 3 | `ERROR <the whole line> a label and a value` |
| No such control | `ERROR FOO no such control` |
| Number out of range or not a whole number | `ERROR FREQ a number from 500000 to 30000000` |
| Not one of the choices | `ERROR MODE one of USB/LSB/CW/CWR/AM/FT8/2TONE/RADE1/RADE2/VOIP` |
| The control's own code refused it | `ERROR <name> <its reason>`. Examples from the source: `ERROR TX this mode does not transmit`, `ERROR CLOCK the clock is set with TIME_SET`, `ERROR STACK the radio is not on a band`, `ERROR MACRO macros are sent in CW`, `ERROR MACRO no such macro`, `ERROR CW_TEXT text is sent in CW`, `ERROR CW_KEY one to twelve dots and dashes, and the gap before them in ms` |

A misspelt request is treated as a control. For example, `spectrum on ` with
a trailing space does not match `spectrum on`, so it is taken as the control
`spectrum` with the value `on `, and is answered `ERROR spectrum no such
control`.

### Limits

| Limit | Value | Where | Effect |
|---|---|---|---|
| Line from a client | 128 bytes (`LINE_MAX_LENGTH`), so 127 characters | `server.c` | Characters past 127 are dropped, up to the newline. The line is not refused. |
| A control's value | 64 bytes (`CONTROL_VALUE_MAX`), so 63 characters | `control.h` | A longer value is cut to 63 characters. Rule 3 then applies, so the sender is told the value that was kept. |
| A control's name | 16 bytes (`CONTROL_NAME_MAX`) | `control.h` | The clients keep names and groups in this much room. The server does not check it. |
| Reason in an `ERROR` | 80 bytes (`CONTROL_WHY_MAX`) | `control.h` | |
| Number of controls | 96 (`CONTROLS_MAX`) | `control.h` | |
| Replies queued for one client | 4096 bytes (`OUT_MAX`) | `server.c` | See "A slow client" below |
| Kernel send buffer per client | 8192 bytes (`SEND_BUFFER_BYTES`) | `server.c` | Kept small on purpose, so that what waits in it cannot be long out of date |
| Text of `SAY`, `WARN`, `QSO`, `CW` | 127 characters | `server.c` | Cut to fit |

The source does not specify a character encoding. The server passes bytes
through as they are. Every name, choice and message in the source is ASCII.

### A slow client

The server never waits for a client, and one slow client never holds up the
radio or the others (`server.c`, "A CLIENT THAT IS SLOW").

- **Control changes are not queued.** Each client has one flag per control.
  When the client's socket can take more, it is sent the *current* value of
  each flagged control, in the order of the table. A control that changed a
  hundred times while the client stalled is sent once, with its last value. The
  order of the lines is therefore the table's order, not the order in which the
  changes were made.
- **The spectrum** is sent to a client only when nothing sent to it earlier is
  still waiting, either in the server or in the kernel. A slow client misses
  lines, and the line it does get is always the newest.
- **`SAY`, `WARN`, `QSO` and `CW` lines** are dropped for a client whose queue
  of replies is too full to take one.
- **Disconnection.** Replies (the schema, `ERROR`, `TIME`, log entries) are
  queued in `OUT_MAX` bytes. A client that keeps asking without reading, so
  that the queue overflows, is disconnected. Otherwise a client is
  disconnected only when its connection fails. The server never times out a
  silent client.

---

## 3. Requests that are not controls

These words are matched exactly, in lower case, as the whole line (`answer`,
`log` and `logdel` are followed by more). They are checked before a line is
taken as `NAME value`. Each one affects only the client that sent it, except
`answer` and `logdel`, whose effects reach the radio.

The examples below show what the client sends (`>`) and what the radio sends
back (`<`).

### On connecting (nothing asked)

As soon as the connection is accepted, the radio sends every control's value,
one line each, in the table's order. Actions are left out. There is no line
to mark the end of this list.

    < FREQ 7088000
    < BAND 40M
    < MODE LSB
    < AUDIO 60
    ...

### `?`: every value again

    > ?
    < FREQ 7074000
    < BAND 40M
    ...

Only the client that asked is sent the values.

### `schema`: the table of controls

    > schema
    < CONTROL RADIO FREQ number 500000 30000000 1
    < CONTROL RADIO BAND choice 80M/60M/40M/30M/20M/17M/15M/12M/10M
    ...
    < CONTROL END

Section 4 gives the format.

### `spectrum on` and `spectrum off`

    > spectrum on
    < SPECTRUM 25031 1c1d1f2a...
    < SPECTRUM 25031 1c1e1f29...
    ...
    > spectrum off

After `spectrum on`, the radio sends a line ten times a second (every
`CLIENT_SPECTRUM_EVERY_MS`, which is 100 ms). While the radio transmits, it
sends `MODULATION` lines in place of `SPECTRUM` lines. Section 5 gives the
format. The radio works out the spectrum for clients only while at least one
client has asked for it. A new connection starts with the spectrum off.

### `messages on` and `messages off`

These carry what the radio copies off the air, FT8's messages:

    > messages on
    < MESSAGE 043015|-12|1797|CQ VU2ESE MK97
    < MESSAGE 043030|TX|1500|VU2ESE G0XYZ R-07
    ...
    > messages off

`messages on` first sends what the radio still holds of earlier messages (the
newest `MESSAGES_KEPT`, which is 128). After that, each message is sent as it
is copied. The fields of a message are separated by `|` (`message_line()` in
`messages.c`):

| Field | Example | Meaning |
|---|---|---|
| time | `043015` | The time the message was sent, in UTC, as `HHMMSS`. For FT8 this is the start of its slot. |
| strength | `-12` or `TX` | Signal strength in dB. `TX` marks a message the radio sent itself. |
| pitch | `1797` | Its pitch in the passband, in Hz |
| text | `CQ VU2ESE MK97` | The message itself. At most 31 characters (`MESSAGE_TEXT_ROOM` is 32). |

Messages are a stream, not the radio's state. Each client has its own place
in the stream. A client that falls more than 128 messages behind misses the
oldest of them.

### `answer <message line>`

This answers a message, as a tap on it does on the front panel. Send the
message's line back, without the word `MESSAGE`:

    > answer 043015|-12|1797|CQ VU2ESE MK97

The server passes the line to `ft8_qso_answer()` (in `tools/sdr.c`, through
`server_on_answer()`). The server itself sends no reply. What follows comes
from the FT8 contact as it goes: `QSO` lines, or a `SAY` line such as `SAY
that message cannot be answered`.

### `time`

    > time
    < TIME 1791397105

The answer is the radio's time in UTC, as seconds since 1970, or `0` while
the radio does not know the time. The time comes from `hw/radio_clock.c`, and
it may differ from the client computer's clock. The control `CLOCK` changes at
the start of each minute. A client that shows seconds can ask `time` once,
count on by its own clock, and correct itself each time `CLOCK` changes. This
is what the desktop window does.

### `log` and `log <beginning of a callsign>`

This lists the logbook, newest entry first, up to 100 entries
(`LOG_ENTRIES_MAX`):

    > log VU2
    < LOG_LIST 2 VU2
    < LOG 972|2026-04-08|04:45|7130|LSB|VU2XZ|59|59
    < LOG 961|2026-04-02|14:10|14074|FT8|VU2ESE|-14 MK97|-09 MK81

`LOG_LIST <count> <what was searched for>` comes first. With a plain `log`,
what was searched for is empty, so the line is `LOG_LIST <count> ` with a
trailing space. Then come the entries, one per line. The fields of an entry
are separated by `|` (`logbook_line()` in `logbook.c`):

| # | Field | Example |
|---|---|---|
| 1 | Entry number, used by `logdel` | `972` |
| 2 | Date, UTC | `2026-04-08` |
| 3 | Time, UTC, `HH:MM` | `04:45` |
| 4 | Frequency in kHz | `7130` |
| 5 | Mode | `LSB` |
| 6 | The other station's callsign | `VU2XZ` |
| 7 | Report sent, then a space and the exchange sent, if there was one | `-14 MK97` |
| 8 | Report received, then the exchange received, if there was one | `-09 MK81` |

The search is the SQL `like ? || '%'` on the callsign (`logbook_find()` in
`logbook.c`). It matches callsigns that begin with the given text.

After a `log` request, the client is sent the whole list of its search again
whenever an entry is added or deleted, by anyone. There is no request to stop
this. It lasts until the client sends another `log` (a new search) or
disconnects. If the radio has no logbook, the client is sent
`WARN log: the radio has no logbook`.

### `logdel <number> [<search>]`

This deletes an entry by its number. Repeat the search with it, so that the
list sent back is still the list of that search:

    > logdel 972 VU2
    < LOG_LIST 1 VU2
    < LOG 961|...

If the deletion fails, the client is sent
`WARN log: the entry could not be deleted`.

To write a log entry, set the `LOGGER` group's text controls (`CALL`, `SENT`,
`RECV`, `EXCH`, `NR`), then send `LOGGER SAVE`. `LOGGER WIPE` clears them.
`logger.c` says what an entry needs. If something is missing, the radio sends
an `ERROR LOGGER ...` line.

### Lines the radio sends unasked

These go to every client as they happen. None of them is a control, and none
is kept. A client that connects later is not told what was said before.

| Line | Meaning |
|---|---|
| `SAY <text>` | A remark: why something was not done, or how a thing went. Example: `SAY that message cannot be answered` |
| `WARN <text>` | Something the operator must act on first. Example: `WARN MYGRID is not set: enter it in SET`. A client may show it more boldly. |
| `QSO <text>` | How the FT8 contact under way stands. Example: `QSO VU2XZ > -19 > R-14 > RR73`. An empty `QSO ` means the contact has ended. |
| `CW <text>` | CW the radio has read off the air, a few characters at a time, spaces between words included. A line can be `CW ` followed by nothing but a space. |

### Every name the radio may send

`CONTROL`, `SPECTRUM`, `MODULATION`, `MESSAGE`, `TIME`, `LOG_LIST`, `LOG`,
`SAY`, `WARN`, `QSO`, `CW`, `ERROR`, and the name of any control. Note that
`CW` the line (text read off the air) is not the CW group of controls, and no
control is called `CW`.

---

## 4. The schema

`schema` is answered with one line per control, in the order the controls
were registered, then `CONTROL END`. The four formats come from
`send_schema()` in `server.c`:

    CONTROL <group> <name> number <min> <max> <step>
    CONTROL <group> <name> choice <choice>/<choice>/...
    CONTROL <group> <name> text
    CONTROL <group> <name> action <choice>/<choice>/...
    CONTROL END

- **`number`**: a whole number from min to max, in steps of step. The value is
  sent as decimal text.
- **`choice`**: one of the choices, which are separated by `/`. Take the
  choices as the whole rest of the line, not as its next word. The desktop
  window does this, in case a choice ever contains a space.
- **`text`**: free text, up to 63 characters.
- **`action`**: something done, not something set. It has no value. Setting it
  to one of its choices does that, each time. An action with no choices is
  sent as `action ` followed by nothing (with the trailing space). It takes any
  value, as `CW_TEXT` and `CW_KEY` do.

The group says who a control belongs to. It is meant for a client to show the
controls in their groups. Some groups exist so that a client's list does
*not* show them: `PANEL` (kept for the front panel), `BAND`, `METER`, `KEY`,
`UPDATE` and `REMOTE` (status lines). Every control exists all the time,
whatever the mode, and keeps its value while another mode is in use
(`control.c`).

### Example

These lines are exactly what `tests/test_server.c` expects the server to send
for these controls:

    CONTROL RADIO FREQ number 500000 30000000 1
    CONTROL RADIO MODE choice USB/LSB/CW/CWR/AM/FT8/2TONE/RADE1/RADE2/VOIP
    CONTROL CW WPM number 5 50 1
    CONTROL RADIO IF number 0 100 1
    CONTROL RADIO DRIVE number 0 100 1
    CONTROL VOICE BW_VOICE number 300 3000 50
    CONTROL CW BW_CW number 50 3000 50
    CONTROL CW CW_INPUT choice STRAIGHT/IAMBIC/IAMBICB/BUG/ULTIMATIC
    CONTROL END

The text and action forms, from the definitions in `radio.c` and
`modems/modem_cw.c`:

    CONTROL STATION MYCALLSIGN text
    CONTROL CW CW_DO action ESC
    CONTROL CW CW_TEXT action 

### The controls at the time of writing

The schema is the authority, and a client should build itself from it. This
table is the list in the source when this chapter was written, for reference.

| Group | Name | Kind | Range or choices | Notes |
|---|---|---|---|---|
| RADIO | `FREQ` | number | 500000–30000000, step 1 | Hz |
| RADIO | `BAND` | choice | `80M/60M/40M/30M/20M/17M/15M/12M/10M` | Setting it goes to where the operator last was on that band. The radio may report `OUT`, which cannot be set. |
| BAND | `STACK` | number | 0–3 | The place in the band's stack of four memories |
| RADIO | `MODE` | choice | `USB/LSB/CW/CWR/AM/FT8/2TONE/RADE1/RADE2/VOIP` | |
| RADIO | `AUDIO` | number | 0–100 | Volume |
| RADIO | `IF` | number | 0–100 | Sound card input gain |
| RADIO | `DRIVE` | number | 0–100 | |
| RADIO | `AGC` | choice | `OFF/SLOW/MED/FAST` | |
| RADIO | `TX` | choice | `OFF/ON` | Refused in modes that do not transmit. See "Transmitting" below. |
| VOICE | `MIC` | number | 0–100 | |
| VOICE | `BW_VOICE` | number | 300–3000, step 50 | `modems/modem_ssb.c` |
| STATION | `MYCALLSIGN`, `MYGRID` | text | | |
| STATION | `FT8_OPS` | choice | `NORMAL/RUNNING/POUNCING` | `ft8_qso.c` |
| STATION | `FT8_REPLY` | choice | `RR73/RRR` | |
| STATION | `UPDATE` | choice | `-/UPDATING/CANCEL` | Starts or cancels a software update |
| SYSTEM | `CLOCK` | text | | The time, for example `2026-10-02 13:47 NTP`, or `NOT SET`. Cannot be set. |
| SYSTEM | `TIME_SET` | number | 0–39445919 | Minutes since the start of 2026, UTC |
| SYSTEM | `WIFI` | action | `SCAN` | `panel_wifi.c` |
| METER | `POWER` | number | 0–999 | Forward power, tenths of a watt |
| METER | `SWR` | number | 0–999 | Tenths: 13 is 1.3 |
| METER | `BATTERY` | number | 0–9999 | Hundredths of a volt |
| METER | `SMETER` | number | 0–9999 | `meter.c` |
| CW | `WPM` | number | 5–50 | |
| CW | `PITCH` | number | 100–3000, step 10 | |
| CW | `BW_CW` | number | 50–3000, step 50 | |
| CW | `CW_INPUT` | choice | `STRAIGHT/IAMBIC/IAMBICB/BUG/ULTIMATIC` | |
| CW | `SIDETONE` | number | 0–100 | |
| CW | `CW_DELAY` | number | 0–1000, step 50 | ms |
| CW | `CW_DO` | action | `ESC` | Stops text being sent |
| CW | `CW_TEXT` | action | (any) | Text to send. `_` stands for a space. CW and CWR only. |
| CW | `CW_KEY` | action | (any) | One character as dots and dashes, then optionally the gap before it in ms: `CW_KEY -.- 180`. CW and CWR only. |
| CW | `MACRO` | action | labels from `data/cw.mc` | Present only if that file has macros |
| FT8 | `TX_PITCH` | number | 200–3000, step 10 | |
| FT8 | `FT8_AUTO`, `FT8_TX1ST` | choice | `OFF/ON` | |
| FT8 | `FT8_REPEAT` | number | 1–5 | |
| FT8 | `FT8_DO` | action | `CQ/HALT` | |
| FT8 | `FT8_SIDETONE` | number | 0–100 | |
| RADE | `SQUELCH` | choice | `OFF/ON` | |
| RADE | `RADE_STATUS` | text | | |
| LOGGER | `CALL`, `SENT`, `RECV`, `EXCH`, `NR` | text | | |
| LOGGER | `LOGGER` | action | `SAVE/WIPE` | |
| PANEL | `SPAN` | choice | `25K/10K/6K/2.5K` | Span of the front panel's spectrum |
| PANEL | `STEP` | choice | `10H/100H/500H/1K/10K` | Tuning step |
| PANEL | `VFO` | choice | `A/B` | |
| PANEL | `VFO_A`, `VFO_B` | number | 0–30000000 | Each VFO's dial, kept for the clients |
| CONNECTIONS | `RADIO_KEY` | text | | The radio's key for my.sbitx.net |
| CONNECTIONS | `REMOTE`, `STREAMING`, `BLUETOOTH`, `CAT` | choice | `OFF/ON` | The gateway acts on these |
| REMOTE | `REMOTE_STATUS`, `BLUETOOTH_STATUS`, `SOUND_CARD_STATUS`, `CAT_STATUS` | text | | Set by the gateway |
| KEY | `NEW_COOKIE` | choice | `-/MAKING` | |
| UPDATE | `UPDATE_STATUS` | text | | |
| VOIP | `VOIP` | choice | `OFFLINE/ONLINE` | |
| VOIP | `BREAK_IN`, `VOIP_TX` | choice | `OFF/ON` | |
| VOIP | `VOIP_STATUS`, `VOIP_SPEAKER` | text | | |

Many of these values are kept from one start of the radio to the next, in
`data/user_settings.ini`. `settings.c` says which.

### Transmitting

`TX ON` puts the radio into transmit if the mode can transmit. `TX OFF` takes
it out. The PTT, and a modem that has something to send (the CW keyer, for
example), also set `TX`, and every client is told. Changing the mode or the
band goes back to receive first. **The radio does not stay in transmit for
longer than two minutes at a stretch** (`TX_LIMIT_MS`, 120000 in
`tools/sdr.c`). After that it goes back to receive by itself, so a client that
loses its connection cannot leave the transmitter on.

---

## 5. The spectrum and the modulation

### `SPECTRUM`

    SPECTRUM <span in Hz> <points>

Example (shortened):

    SPECTRUM 25031 1c1d1f2a2b...

- **Span.** The number of Hz the line covers, with the tuned frequency in its
  middle. It is `SPECTRUM_BINS × SPECTRUM_HZ_PER_BIN`, which is
  534 × 46.875 Hz = 25031.25, sent as the whole number `25031`.
- **Points.** 534 points, one for each bin of the receiver's transform
  (46.875 Hz each). They run from the lowest frequency to the highest. Each
  point is two lower-case hex digits (`0`–`9`, `a`–`f`). There are no spaces
  between points, so the field is 1068 characters long.
- **Value of a point.** The bin's level in dB with 30 added
  (`SPECTRUM_ADDED_DB`), held to 0–255. The 30 is there so that the noise sits
  a little above the bottom. The source does not say what the dB are relative
  to. Treat the values as relative.
- **Where the tuned frequency falls.** The source says only "in its middle".
  The constants in `tools/sdr.c` give more. The line takes bins 1269–1802 of
  the transform, reversed, and the tuned frequency is bin 1536. That puts it at
  point 266, counting from 0. Point *p* is then about tuned + (*p* − 266) ×
  46.875 Hz.
- **Rate.** Every 100 ms, to each client that sent `spectrum on`, provided
  the client has nothing still waiting to be sent to it.
- **Span is not settable here.** The radio always sends the full 534 points.
  The `SPAN` control narrows only the front panel's own line. The desktop
  window cuts its view from the full line itself.

A minimal decoder in Python:

```python
def decode_spectrum(value):
    span_text, hex_points = value.split(" ", 1)
    return int(span_text), bytes.fromhex(hex_points)
```

### `MODULATION`

While `TX` is `ON`, clients that asked for the spectrum get this line in its
place, at the same rate:

    MODULATION <points>

The line has no span field. It carries 400 points (`MOD_MAX / 2`, with
`MOD_MAX` 800 in `dsp/sdr.h`), two hex digits each. Each point is how far the
signal swung from zero in half a millisecond: the larger of that half
millisecond's least and greatest values. It is scaled so that 255 is the top
of zbitxv2's picture (`MODULATION_FULL_SCALE`, 50) or anything above it. The
two tones of 2TONE come to about 107. So the 400 points cover a fifth of a
second. They are in the order the core keeps them, which goes round in a
ring: the newest point is somewhere in the line, and the oldest follows it.
The source does not mark where the newest point is.

---

## 6. Sound

Sound does not go over port 8081. It goes as RTP over UDP, **on the radio
itself only**. A program on another computer cannot receive these streams
directly. From afar, the radio's sound is reached through my.sbitx.net (see
`PROTOCOL.md`), and on the radio, other programs reach it through the devices
`setup/sound_card.py` makes.

All three streams use the same RTP payload (`rtp_audio.h`):

| | |
|---|---|
| RTP version | 2. The first byte is `0x80`: no padding, no extension, no CSRC, marker never set. |
| Payload type | 96 (`RTP_AUDIO_PAYLOAD_TYPE`). This is a dynamic type, since RTP has no fixed number for L16 at this rate. |
| Samples | 16-bit signed, big-endian (most significant byte first), one channel. RFC 3551 calls this L16. |
| Rate | 48000 samples a second (`RTP_AUDIO_RATE_HZ`) |
| Packet | 960 samples, which is 20 ms: 1920 bytes after the 12-byte RTP header, 1932 bytes in all (`RTP_PACKET_BYTES`). 50 packets a second. |
| Timestamp | Goes up by 960 each packet |

| Stream | Direction | Address | Extra framing |
|---|---|---|---|
| The radio's sound (`rtp_audio.c`) | sdr → listeners | multicast `239.255.73.88` port `5004`, on the loopback interface, TTL 0 | none |
| Sound to transmit, or VoIP speech to hear (`rtp_microphone.c`) | a program → sdr | UDP `127.0.0.1` port `5006` | an 11-byte label before the RTP packet |
| The owner's VoIP speech (`rtp_voip.c`) | sdr → the gateway | UDP `127.0.0.1` port `5007` | none |

### Hearing the radio: 239.255.73.88:5004

The radio program always sends this stream, whether or not anyone listens. It
sends it with TTL 0 on the loopback interface, so it never leaves the radio.
To hear it, a program on the radio joins group `239.255.73.88` on interface
`127.0.0.1` and binds to port 5004. Several listeners may do this at once.
Each packet's SSRC and first sequence number are chosen at random when `sdr`
starts.

The stream carries what the speaker plays, taken before the volume control,
so `AUDIO` does not change its level. It includes the CW sidetone and the
monitor of a transmission. The speaker's 96 kHz samples are filtered,
decimated by 2, and multiplied by 8 (`STREAM_GAIN`) before being held to 16
bits.

`setup/bluetooth_audio.sh` shows the whole job in a few lines. It strips the
12-byte header from each packet and plays the samples:

```python
import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("", 5004))
s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
             socket.inet_aton("239.255.73.88") + socket.inet_aton("127.0.0.1"))
out = sys.stdout.buffer
while True:
    out.write(s.recv(4000)[12:])
    out.flush()
```

The script pipes the samples into
`pacat --raw --format=s16be --rate=48000 --channels=1`.

### Speaking: 127.0.0.1:5006

The radio program listens on 127.0.0.1 only, so nothing off the radio can
reach this port. Each datagram must be exactly 1943 bytes: an 11-byte label
followed by a 1932-byte RTP packet as above.

| Bytes | Content |
|---|---|
| 0 | Kind (`rtp_microphone.h`): `1` speech from the owner working the radio from afar, to transmit (`RTP_MICROPHONE_TO_TRANSMIT`). `2` sound from another program on the radio, to transmit (`RTP_MICROPHONE_FROM_A_PROGRAM`). `3` someone speaking over VoIP, to be heard and never transmitted (`RTP_MICROPHONE_VOIP`). |
| 1–10 | Callsign of the speaker, padded with zero bytes. It is used for kind 3, where it becomes `VOIP_SPEAKER`. |
| 11–22 | RTP header: version 2, payload type 96 |
| 23–1942 | 960 samples, 16-bit big-endian |

The radio drops a datagram of any other size, kind, version or payload type.
It also drops one whose sequence number is at or behind the last one taken,
unless a second (`QUIET_MS`) has passed with no packets, in which case a new
stream may start at any number. The timestamp and SSRC are not checked.

For kinds 1 and 2, sending sound does not key the radio. The sound replaces
the microphone **only while the radio is transmitting** (`TX ON`, or the PTT).
It does so from the first packet that arrives in that transmission until the
transmission ends. Before any packet arrives, the radio's own microphone is
used. Once remote sound is in use, a gap in it is sent as silence, not filled
from the microphone. Sound that arrives while the radio is receiving is
thrown away. Playing starts once 120 ms (`LEAST_SAMPLES`, 5760 samples) is
waiting. If more than 400 ms (`MOST_SAMPLES`) builds up, the oldest is
dropped. `MIC` works on these samples as a plain multiplier.

So a client that wants to transmit its own sound sets `TX ON` on port 8081,
streams packets to 5006, and sets `TX OFF` when done. The two-minute limit
applies.

### The owner's VoIP speech: 127.0.0.1:5007

In the mode `VOIP`, while the owner speaks (`VOIP_TX`), the radio sends the
microphone's sound, or the sound from afar that replaces it, to 127.0.0.1
port 5007. There it is plain RTP, with no label, SSRC `0x766f6970`. The
gateway listens on this port and carries the speech to my.sbitx.net. A
client has no reason to use it. It is listed so that the port is known to be
taken.

### Other ports on the radio

| Port | Who | What |
|---|---|---|
| TCP 4532 | the gateway, while `CAT` is `ON` | Answers as Hamlib's `rigctld` does. Choose "Hamlib NET rigctl" in the other program. `CAT_STATUS` shows the address. See `clients/remote/cat.c`. |

### The front panel

The zbitx's front panel does not use any of the above. It speaks its own
binary link over I2C (address `0x0a`, `LINK_VERSION` 4), defined in
`front_panel/link.h` and `front_panel/link.c`, with the radio's end in
`hw/panel.c`. It is not meant for clients. Anything a client needs from the
radio is on port 8081.

---

## 7. A worked session with `nc`

This shows the shape of a session. The first two exchanges were captured
on 17 October 2026 from the radio program running on a desktop against the
hardware stand-ins; the rest are examples built from the source. The order
of the lines is the order the controls were registered in, which is not
the order of the screens: the modems' controls come before the radio's.

Connect. The radio sends every value at once, 65 lines at the time of
writing:

    $ nc sbitx.local 8081
    WPM 20
    PITCH 600
    BW_CW 400
    CW_INPUT IAMBIC
    SIDETONE 30
    CW_DELAY 300
    FT8_SIDETONE 30
    RADE_STATUS --
    ...

Ask for the schema, 68 controls and the END at the time of writing:

    schema
    CONTROL CW WPM number 5 50 1
    CONTROL CW PITCH number 100 3000 10
    CONTROL CW BW_CW number 50 3000 50
    CONTROL CW CW_INPUT choice STRAIGHT/IAMBIC/IAMBICB/BUG/ULTIMATIC
    CONTROL CW SIDETONE number 0 100 1
    CONTROL CW CW_DELAY number 0 1000 50
    ...
    CONTROL END

Tune to 14.074 MHz. FREQ is not echoed back. But the band changed, and the
radio reports that to everyone, us included. `STACK` comes too if 20 m's place
differs from 40 m's:

    FREQ 14074000
    BAND 20M

Change the mode. A value that is taken as sent is not echoed, so nothing
comes back:

    MODE USB

Set it again to the same value. Nothing comes back, and nobody else is told:

    MODE USB

Send something the radio will not take:

    FREQ 1
    ERROR FREQ a number from 500000 to 30000000
    MODE usb
    ERROR MODE one of USB/LSB/CW/CWR/AM/FT8/2TONE/RADE1/RADE2/VOIP
    hello
    ERROR hello a label and a value

Ask the time:

    time
    TIME 1791397105

Now leave it alone, and turn the front panel's knob, or change something in
the desktop window. Each change appears as it is made:

    FREQ 14074500
    FREQ 14075000
    AUDIO 70

The meters and `CLOCK` appear on their own as they change.

`spectrum on` fills the terminal with 1000-character lines ten times a
second. Send `spectrum off` to stop it. Close the connection with Ctrl-C.
Every request applies only to that connection, so nothing needs to be undone.

---

## 8. Writing a client

### What the desktop window does

`clients/gtk/zbitx_gui.c` is the reference client.

On connecting:

1. It opens TCP to the radio, port 8081. The default radio name is
   `sbitx.local`.
2. It sends these four lines at once:

        schema
        spectrum on
        messages on
        time

   Through my.sbitx.net, it sends the same four after the service says
   `CONNECTED`. The web page `operate.js` sends the same four.
3. It clears its list of FT8 messages, since `messages on` sends the radio's
   kept messages again.
4. It reads lines and acts on each by its first word (`handle_line()`):
   `SPECTRUM`, `MODULATION`, `CONTROL` (building its list of controls at
   `CONTROL END`), `MESSAGE`, `TIME`, `CW`, `SAY`, `WARN`, `QSO`, `ERROR`,
   `LOG` and `LOG_LIST`. Anything else is taken as a control's value. The
   values sent on connecting arrive *before* the schema. The window keeps
   values by name as they come, and fills in each control's kind when the
   schema arrives.

While connected:

- When the operator moves a control, the window shows the new value at once
  and sends `NAME value`. It does not wait for a reply, because none comes.
- When the radio reports a change, the window shows it and never sends
  anything back.

On losing the connection:

- If the connection fails or closes, the window says so and tries once more
  after 3 seconds (`RETRY_EVERY_MS`). It never has more than one connection or
  more than one retry waiting. An earlier version that kept adding connections
  used up the radio's eight places, and the radio ran out of memory.
- If the radio has said nothing for 10 seconds (`SILENT_AT_MOST_MS`, checked
  every 2 seconds), the window drops the connection and reconnects. With
  `spectrum on` the radio sends ten lines a second, so 10 seconds of silence
  means the connection is dead. A client that does not ask for the spectrum
  may hear nothing for a minute at a time, until `CLOCK` changes.
- After reconnecting, it repeats step 2. Requests such as `spectrum on`,
  `messages on` and `log` last only as long as the connection.

### Rules for any client

- Read every line, even ones you do not care about. A client that does not
  read stalls its own queue. That costs the radio nothing, but the client
  misses spectrum lines, and if it keeps asking for the schema without reading
  it is disconnected.
- Do not echo what you are told.
- Do not expect a reply to a successful change. Expect only `ERROR` for a
  failed one, and a correction for a value that was snapped onto its steps or
  cut to fit.
- Build your list of controls from the schema, not from a fixed list. A new
  mode or modem adds controls without the protocol changing.
- Skip lines whose first word you do not know. New kinds of line may be
  added.

### A minimal client in Python

This uses only the standard library. It connects, reads the values and the
schema, checks the frequency against the schema, sets `FREQ`, and then prints
every change the radio reports. It has been run against a small stand-in
server, not against a radio.

```python
#!/usr/bin/env python3
# radio_watch.py: connect to the radio, read its schema, tune it, and print
# every change the radio reports after that.
#
#     python3 radio_watch.py [radio] [hz]

import socket
import sys

PORT = 8081

radio = sys.argv[1] if len(sys.argv) > 1 else "sbitx.local"
hz = int(sys.argv[2]) if len(sys.argv) > 2 else 7074000

sock = socket.create_connection((radio, PORT), timeout=10)
sock.settimeout(None)
lines = sock.makefile("r", encoding="utf-8", errors="replace", newline="\n")


def send(line):
    sock.sendall((line + "\n").encode("utf-8"))


# The radio sends every control's value as soon as we connect. We ask for
# the schema too, and read until CONTROL END. Value lines arrive before the
# schema, so we sort them out once the schema is complete.
send("schema")
values = {}
schema = {}
for line in lines:
    name, _, value = line.rstrip("\n").partition(" ")
    if name == "CONTROL":
        if value == "END":
            break
        parts = value.split(" ", 3)
        group, control, kind = parts[0], parts[1], parts[2]
        schema[control] = (group, kind, parts[3] if len(parts) > 3 else "")
    else:
        values[name] = value

print(f"{len(schema)} controls")
for control, (group, kind, rest) in schema.items():
    print(f"  {group:12} {control:18} {kind:7} {rest:30} {values.get(control, '')}")

# Check the frequency against the schema before sending it.
low, high, step = (int(n) for n in schema["FREQ"][2].split())
if not low <= hz <= high:
    sys.exit(f"FREQ must be from {low} to {high}")
send(f"FREQ {hz}")

# The radio does not echo our own change back. From here on, every line is
# something that changed, or something the radio has to say.
for line in lines:
    name, _, value = line.rstrip("\n").partition(" ")
    if name == "ERROR":
        print("refused:", value)
    elif name in schema:
        values[name] = value
        print(f"{name} = {value}")
    else:
        print(line, end="")
print("the radio closed the connection")
```

Notes on the example:

- `partition(" ")` splits only at the first space, as the server does, so a
  value that contains spaces, or is empty, comes through whole.
- `split(" ", 3)` on a schema line keeps the choices of a `choice` or `action`
  as one string, even if they contain spaces.
- It does not ask for the spectrum. To draw one, send `spectrum on` and
  decode the lines as in section 5.
- It does not reconnect. A long-running client should do what the desktop
  window does: on any failure, close, wait 3 seconds, connect again, and
  re-send its requests.
