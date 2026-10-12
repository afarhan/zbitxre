# The radio's protocol over TCP/IP

This is how a program talks to a zbitx or an sbitx running zbitxre: the window on
the radio's own screen, a program on another computer, and, when it is built, the
service at my.sbitx.net that lets an owner reach their radio from anywhere.

The first part describes the protocol **as it is today**. `server.c` serves it and
its header is the fuller account; where the two differ, `server.c` is right and
this file is to be corrected. The last two parts, on authentication and on
reaching a radio through my.sbitx.net, are **proposed and not built**. They are
written here to be argued with before any of it is code.

## The connection

The radio listens on TCP port 8081. A client connects and the two talk in lines
of text, each ending in a newline. It can be tried by hand:

    nc sbitx.local 8081

At most eight clients are connected at once; one more is turned away. Nothing is
polled: a client that says nothing is sent only what changes.

Today anyone who can reach the port is a client. There is no authentication.

## Controls

Everything the radio can be set to is a control with a name: `FREQ`, `MODE`,
`AUDIO`, `WPM`, `MYCALLSIGN` and the rest. `control.c` keeps the table.

**On connecting**, a client is sent every control's value, a line each:

    FREQ 7074000
    MODE USB
    AUDIO 80

A client that sends `?` alone on a line is sent them all again.

**A client sets** a control by its name and the new value:

    FREQ 7074000
    BAND 20M
    MODE CW

**The radio tells.** When a control changes, whoever changed it, the radio sends
the new value to every client except the one that changed it, which knows
already. The one that asked is told only if the radio took a value other than
the one asked for, a number snapped to its step. A change that moves other
controls, a band moving the frequency and the mode, is told to everyone.

Two rules keep a change from going round in a loop. A value the control already
has changes nothing and is told to nobody. And a client never sends a change
because it was told one: it shows it, and that is all.

**A line the radio will not take** is answered, to that client alone:

    ERROR FREQ a number from 500000 to 30000000

**The schema.** A client that sends `schema` is sent the table of controls, so
that it can build itself without knowing beforehand what this radio has:

    CONTROL RADIO FREQ number 500000 30000000 1
    CONTROL RADIO MODE choice USB/LSB/CW/CWR/AM/FT8/2TONE
    CONTROL CW WPM number 5 50 1
    CONTROL CW MACRO action CQ/RST/End
    CONTROL STATION MYCALLSIGN text
    CONTROL END

Each line is the control's group, its name and its kind, and then for a number
its least, its greatest and its step, and for a choice or an action its choices
with a stroke between them. A control's group says where it belongs: `RADIO` for
the radio as a whole, a mode's name for that mode's own, `STATION` for the
callsign and grid, `METER` for what is measured. An **action** has no value: it
is something done each time it is set, as `MACRO CQ` sends that macro.

## What the radio sends besides controls

**The spectrum.** After `spectrum on`, ten times a second, until `spectrum off`:

    SPECTRUM 25031 1c1d1f2a...

The number is how many Hz the line spans, the tuned frequency in its middle.
Then a strength for each point from the lowest frequency up, two hex digits
each, in dB with 30 added. There are 534 points.

**The modulation.** While the radio transmits there is no spectrum, and in its
place, as often, comes the envelope of what is being sent:

    MODULATION 00030a1f...

400 points, two hex digits each, a fifth of a second.

**Messages copied off the air.** After `messages on`, each FT8 message as it is
copied, beginning with what the radio still has from before, until
`messages off`:

    MESSAGE 043015|-12|1797|CQ VU2ESE MK97

The fields are the time it was sent in UTC, its strength in dB or `TX` for one
of ours, its pitch in Hz and its text. A client answers a message by sending
its line back after the word `answer`.

**CW read off the air**, a few characters at a time:

    CW CQ CQ DE VU

**Remarks.** `SAY` is a remark, `WARN` one the operator has to act on, and
`QSO` how the FT8 contact under way stands, empty when it ends:

    SAY that message cannot be answered
    WARN MYGRID is not set: enter it in SET
    QSO VU2XZ > -19 > R-14 > RR73

None of these is kept: a client that connects later is not told what was said
before.

## The logbook

`log` asks for the newest entries, a hundred at most, the newest first; with
the beginning of a callsign after it, only the stations whose callsign begins
so. First a line that says how many are coming and what was searched for, then
an entry to a line:

    log VU2
    LOG_LIST 2 VU2
    LOG 972|2026-04-08|04:45|7130|LSB|VU2XZ|59|59
    LOG 961|2026-04-02|14:10|14074|FT8|VU2ESE|-14 MK97|-09 MK81

The fields are the entry's number, the date and time in UTC, the frequency in
kHz, the mode, the station, and what was sent and received. From then on the
client is sent the list of its search afresh whenever the logbook changes.

An entry is deleted by its number, with the search it is to answer with:

    logdel 972 VU2

An entry is written with the controls of the group `LOGGER`: `CALL`, `SENT`,
`RECV`, `EXCH` and `NR` are set as any text is, `LOGGER SAVE` logs the entry
and `LOGGER WIPE` clears them.

## The time

`time` is answered with the radio's time in UTC as seconds since 1970, or 0
while the radio does not know the time:

    TIME 1791397105

## A client that is slow

The radio sends a client what is, not what happened. A control that changed a
hundred times while a client stalled is sent once, with its last value. A line
of the spectrum goes to a client only when nothing sent before is still
waiting, so a slow client misses lines and the one it gets is the newest.
Messages and log entries are not state and none is left out.

---

# Proposed: authentication

**Not built. For review.**

## What it is for

The protocol is to be carried across the internet, between an owner's client
and their radio by way of my.sbitx.net. Each end must then prove who it is, and
the proof must not be something that a listener on the way can use again. A
password or a key sent down the connection would be exactly that. So the proof
is an answer to a challenge.

## Keys

Each party that connects has a **key** of its own: 32 random bytes, written as
text, with a short **key id** that names it and is not secret. A radio has one
and each client on each computer has one. They are issued by my.sbitx.net when
the owner links the radio or the client to their account, and each can be taken
away there. The party keeps its key in a file only its user can read. The
account's password is typed into the website and nowhere else.

my.sbitx.net keeps each key as well, since it has to work out the same answer.
That is the cost of a challenge by keyed hash: whoever read the service's
database would have the keys. The other way, a key pair on each party, keeps
nothing secret at the service but needs public-key code at both ends.

## The handshake

The side that is connected to speaks first, with a challenge: 32 hex digits it
has never used before.

    CHALLENGE 5f1c0a...                         (the one connected to)
    AUTH <key id> <nonce> <proof>               (the one connecting)
    WELCOME <callsign> <proof>                  (the one connected to)

The connecting side makes a nonce of its own, 32 hex digits, and its proof is

    HMAC-SHA256(key, "zbitx-auth-1 client <challenge> <nonce>")

in hex. The answer's proof is the same with `server` in place of `client`, and
shows that the side connected to has the key too, so that a client is not
talking to an impostor. A wrong proof, a key id that is not known, or no `AUTH`
within ten seconds, and the connection is closed, with `ERROR AUTH` and a
reason. Repeated failures from one address are made to wait.

After `WELCOME` the protocol is the one above, unchanged.

## Where it is asked for

Only on the internet side. The author's ruling: the home network stays as it
is, and what is guarded is what crosses the internet.

| Connection | Who challenges | Whose key answers |
|---|---|---|
| the radio's side to my.sbitx.net | the service | the radio's |
| a client to my.sbitx.net | the service | the client's |
| a client straight to a radio on the home network | nobody | none |
| a program on the radio itself, to the radio | nobody | none |

So the radio's own server, `server.c` on port 8081, is not changed by any of
this. The challenge is spoken by my.sbitx.net and answered by whoever connects
to it.

## What the challenge does not do, and why that is accepted

It proves who is at each end when the connection opens. It does not hide what
is then sent, and it does not stop someone who sits on the path between the two
from putting a line of their own into a connection that is already open.

The author's ruling: these connections are **not inside TLS**. Amateur radio
communications are to be unencrypted by law, and what is wanted here is only
to be sure who is accessing the radio. So what passes between a client, the
service and a radio is plain text that anyone on the way can read.

The website is another matter and is served over HTTPS: a password is typed
there, and a key is handed to a radio or a client there once, when it is
linked. Neither is a radio communication.

---

# Proposed: reaching a radio through my.sbitx.net

**Not built. For review.**

A radio at home is behind a router and cannot be connected to from outside. So
something on the radio connects out to my.sbitx.net, and nothing is changed on
the owner's router.

## The daemon on the radio

That something is not the radio program. `sdr` is a server and stays one. A
small program of its own, written in C, runs beside it on the radio. To `sdr`
it is one more client on 127.0.0.1; to my.sbitx.net it is the radio. It holds
the radio's key and answers the challenge. Whatever goes wrong with it or with
the internet, the radio program is not touched.

## It is the owner's to switch on and off

The author's ruling. The owner turns remote access on and off, on the radio,
and it is off until they turn it on. While it is off the daemon holds no
connection to anything outside the radio.

## Nothing is sent that nobody asked for

While it is on and nobody is using it, the daemon holds one connection to
my.sbitx.net and says nothing on it, beyond a word a minute to show that it is
still there. That connection is how the service knows the radio can be
reached, and how it tells the daemon that somebody wants it. No spectrum, no
controls and no log go down it.

When the owner's client asks my.sbitx.net for the radio, the service tells the
daemon, and the daemon then opens two connections for that client alone: one
to my.sbitx.net and one to `sdr`, and passes each line from the one to the
other. The client is then talking the protocol above to its radio, and is sent
what any client is sent and no more: the spectrum only if it asks for it. When
the client goes, both connections are closed.

The backup of the settings and the copy of the logbook are taken the same way
and are not kept up continuously: the service asks the daemon for a
connection, reads the controls' values and the log's entries as a client
would, and closes it. That is done when the radio comes on line, and when the
owner asks for it on the website.

## The steps

1. The owner turns remote access on. The daemon connects to my.sbitx.net, is
   challenged, and proves itself with the radio's key. The service now knows
   this radio can be reached and whose it is.
2. The owner's client, anywhere, connects to my.sbitx.net, is challenged, and
   proves itself with its own key. It asks which of the owner's radios can be
   reached, and for one of them.
3. The service tells that radio's daemon. The daemon connects again, proves
   itself again, and names the session it was told of. The service joins that
   connection to the client's.

**Open:** how many clients may be on one radio at once; whether sound is carried, which the protocol does not do
today; and whether files that are not controls, the radio's calibration in
`hw_settings.ini` and the CW macros, are backed up, which needs a request for
sending a file.
