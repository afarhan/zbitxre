# The radio's protocol over TCP/IP

This is how a program talks to a zbitx or an sbitx running zbitxre: the window on
the radio's own screen, a program on another computer, and, when it is built, the
service at my.sbitx.net that lets an owner reach their radio from anywhere.

The first part is the radio's own protocol. `server.c` serves it and its header
is the fuller account; where the two differ, `server.c` is right and this file
is to be corrected. The second and third parts are what is added to cross the
internet: how each end proves who it is, and how a client is brought to its
radio. `accounts/remote.py` is the service's end of that,
`clients/remote/zbitx_remote.c` the radio's, and `clients/gtk/zbitx_gui.c` and
`accounts/remote_client.py` are clients; where this file and they differ, they
are right.

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
    CONTROL RADIO MODE choice USB/LSB/CW/CWR/AM/FT8/2TONE/RADE2
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
An action
with no choices takes what it is given and says itself whether that will do.

**CW keyed on a client.** `CW_KEY` is such an action, for a client with a keyer
of its own, as a telephone's screen has: it is sent each character as it is
keyed, as dots and dashes, and after them the milliseconds the key was up
before the character began, if that is known:

    CW_KEY -.-
    CW_KEY --.- 180

One to twelve dots and dashes. The radio, in CW, sends the character at `WPM`,
evenly made, beginning when its own key has been up for as long as the client's
was, and never sooner than three dots after the character before; so the
spacing is the operator's and the making is the radio's. A character that comes
too late for that is sent as soon as it comes. `CW_DO ESC` drops what is
waiting, as it does a macro's text.

**CW typed on a client.** `CW_TEXT` is another: what is typed on a client's
keyboard, a character as it is struck or several together, sent in CW after
whatever typed text is already waiting:

    CW_TEXT c
    CW_TEXT de_vu2ese

`_` is a space as well as a space is, since a space alone at a line's end is
lost on the way. A character with no code in Morse is passed over.

**A macro gives way to the operator.** While a macro is being sent or waiting
to be, `CW_TEXT` or `CW_KEY` drops it, and all that waited with it, and what
was typed or keyed is sent in its place; as a touch of the radio's own paddle
drops it.

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

# Authentication

## What it is for

The protocol is carried across the internet, between an owner's client and
their radio by way of my.sbitx.net. Each end must then prove who it is, and the
proof must not be something that a listener on the way can use again. A
password or a key sent down the connection would be exactly that. So the proof
is an answer to a challenge.

## A radio's cookie and its client password

An owner has up to five radios at my.sbitx.net. Each has a name the owner gave
it, and is known by the callsign with that after it: `vu2ese-home`,
`vu2ese-zbitx`. Each radio has two secrets, both twelve letters written in
fours, `abcd-jklm-wxyz`.

**The cookie** is the radio's own. The radio makes it, the first time remote
access is turned on, and shows it among its settings. The owner, signed in at
the website, adds a radio by giving it a name and typing the cookie. Nothing is
typed on the radio, and no file on it is edited: the author's design, a zbitx
having no keyboard. The radio answers the service's challenge with the cookie
from then on, and no longer shows it.

**The client password** is made by the website and shown there beside the
radio. It is what the owner types into a client, with the radio's name, to
reach the radio from afar. It is apart from the cookie so that it can be
changed at the website, when a laptop is lost, with nothing done at the radio,
and so that whoever has it cannot pass for the radio.

The account's own password is typed into the website and nowhere else.

my.sbitx.net keeps both secrets as they are and not as hashes, since it has to
work out the same answer to its challenge, and has to show the owner the
client password. Whoever read the service's database would have them.

## Stretching

Twelve letters are few enough to find by trying them all, for someone who
overheard a challenge and its answer and can check each guess against them. So
the letters are not the key of the proof. The key is what PBKDF2, with
HMAC-SHA256 and 50,000 rounds, makes of the twelve letters, written small and
without the strokes, with the salt `zbitx-1 ` followed by, in small letters,
the callsign for a cookie and the radio's name for a client password; the
result is used as the 64 hex digits it is written in. Each guess then costs
50,000 times what it would.

## The handshake

The service speaks first, with a challenge: 32 hex digits it has never used
before.

    CHALLENGE 5f1c0a...                             the service
    AUTH radio VU2ESE <nonce> <proof>               a radio's daemon, or
    AUTH client vu2ese-home <nonce> <proof>         a client
    WELCOME vu2ese-home <proof>                     the service

The one connecting makes a nonce of its own, 32 hex digits. Its proof is

    HMAC-SHA256(key, "zbitx-auth-1 radio VU2ESE <challenge> <nonce>")
    HMAC-SHA256(key, "zbitx-auth-1 client vu2ese-home <challenge> <nonce>")

in hex, the key being the stretched cookie or the stretched client password,
and the name in it being the name exactly as it was sent. The service's proof
is the same with `server` in place of `radio` or `client`, and shows that the
service has the secret too, so that nobody is talking to an impostor.

A radio says only whose it is, by the callsign set on it. It does not know the
name it was given on the website. The service tries the cookie of each of that
callsign's radios, five at most, and the one that made the proof is the radio;
`WELCOME` says its name.

A wrong proof, and the connection is closed with `ERROR AUTH` and a reason. A
radio that has not been added at the website is refused in just this way, and
goes on showing its cookie and asking, every quarter of a minute, until it is
added. An address that sends a hundred wrong proofs in a quarter of an hour is
turned away for the rest of it.

## Where it is asked for

Only on the internet side. The author's ruling: the home network stays as it
is, and what is guarded is what crosses the internet.

| Connection | Who challenges | What answers |
|---|---|---|
| the radio's side to my.sbitx.net | the service | the radio's cookie |
| a client to my.sbitx.net | the service | the radio's client password |
| a client straight to a radio on the home network | nobody | nothing |
| a program on the radio itself, to the radio | nobody | nothing |

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

The website is another matter and is served over HTTPS: the account's password
is typed there, and so is a radio's cookie when the radio is added.

---

# Reaching a radio through my.sbitx.net

The service listens on TCP port 8081, as a radio does.

A radio at home is behind a router and cannot be connected to from outside. So
something on the radio connects out to my.sbitx.net, and nothing is changed on
the owner's router.

## The daemon on the radio

That something is not the radio program. `sdr` is a server and stays one. A
small program of its own, written in C, runs beside it on the radio:
`clients/remote/zbitx_remote.c`. To `sdr` it is one more client on 127.0.0.1;
to my.sbitx.net it is the radio. It makes and keeps the radio's cookie and
answers the challenge. Whatever goes wrong with it or with the internet, the
radio program is not touched.

## It is the owner's to switch on and off

The author's ruling. The radio has two controls in a group of their own,
`CONNECTIONS`, each `OFF` or `ON`, and off until the owner turns it on:
`REMOTE`, whether the radio may be worked from afar, and `STREAMING`, whether
anyone may listen to it and see what it copies in FT8. They are set at the
radio and nowhere else: a client that reaches the radio through my.sbitx.net
does not offer them. With both off the daemon holds no connection to anything
outside the radio. With either on it is on line, and tells the service which
on the connection it keeps, when it comes and whenever one changes:

    ALLOWS remote streaming
    ALLOWS streaming
    ALLOWS remote

A radio that does not allow remote is worked by nobody: its owner's page is
told `! closed`, a client `ERROR connect`, and whoever was working it is let
go. One that does not allow streaming is listened to by its owner alone,
signed in, who has to hear it to work it; anyone else is told `closed`. It is
shown on the globe, grey, and what it copies in FT8 is not sent.

To shut somebody out who has got in, the owner changes the password that let
them in: the radio's client password, on the website, or the account's own.
Whoever is in by it is let go within half a minute.

Three more switches are in `CONNECTIONS`, which have nothing to do with
my.sbitx.net and work with the first two off. The daemon sees to them as well,
and says what came of each in a control of its own:

    BLUETOOTH    BLUETOOTH_STATUS     the radio's sound in a paired earphone
    SOUND_CARD   SOUND_CARD_STATUS    two sound devices for another program on
                                      the radio, to hear the receiver by and
                                      to send by
    CAT          CAT_STATUS           other programs set the frequency and the
                                      mode and key the radio, through Hamlib's
                                      "NET rigctl"; the status is the address
                                      and port to give them, 192.168.1.82:4532

`clients/remote/zbitx_remote.c` and `clients/remote/cat.c` say how each is done.

What the daemon has to say it says in another control, `REMOTE_STATUS`:

    OFF
    SET MYCALLSIGN FIRST          the radio has no callsign to say whose it is
    CONNECTING
    COOKIE abcd-jklm-wxyz         the website does not know this radio: add it
                                  there, with this cookie
    ONLINE vu2ese-home
    NO ANSWER FROM SERVER

`sdr` keeps the three controls and does nothing with them.

## Nothing is sent that nobody asked for

While remote access is on and nobody is using it, the daemon holds one
connection to my.sbitx.net and says nothing on it, beyond a word a minute to
show that it is still there. That connection is how the service knows the
radio can be reached, and how it tells the daemon that somebody wants it. No
spectrum, no controls and no log go down it.

When the owner's client asks my.sbitx.net for the radio, the service tells the
daemon, and the daemon then opens two connections for that client alone: one
to my.sbitx.net and one to `sdr`, and passes each byte from the one to the
other. The client is then talking the protocol above to its radio, and is sent
what any client is sent and no more: the spectrum only if it asks for it. When
the client goes, both connections are closed.

The backup of the settings and the copy of the logbook are taken the same way
and are not kept up continuously: the service asks the daemon for a
connection, reads the controls' values and the log's entries as a client
would, and closes it. That is done when the radio comes on line.

## What is said, after the handshake

**The daemon, on the connection it keeps.** It says what the connection is, and
then nothing but a word a minute:

    radio                      the daemon
    OK                         the service
    GRID MK97fk                the daemon: the grid locator set on the radio
    PING                       the daemon, each minute
    PONG                       the service

`GRID` is said once the connection is made and again whenever the radio's
`MYGRID` is changed; with no grid set it is said with nothing after it. The
service keeps it with the radio. It is the one thing of the radio's that is
told unasked, the author having asked that the grid reach the service along
with the callsign.

When somebody wants the radio, the service says so on that connection:

    SESSION 9c41e0...          the service

**The daemon, for that visitor.** It connects again, answers a challenge again,
and names the session:

    session 9c41e0...          the daemon
    OK                         the service

Every byte after that `OK` is passed between this connection and the visitor,
and the daemon passes it between this connection and one it has made to the
radio program. A session nobody is waiting for is refused.

**A client.** It named its radio in the handshake, and has nothing more to
say. When the radio's daemon has answered, the service says

    CONNECTED vu2ese-home

and the client is from then on talking to its radio in the radio's own
protocol, the first part of this file, beginning with every control's value,
as if it had just connected to the radio itself. A radio that is not on line,
or does not answer within ten seconds, is an `ERROR connect` with the reason.

One client works a radio at a time. A second that asks for a radio while
another is on it is sent `ERROR connect that radio is being worked by another
client`. Any number may listen to it.

## The radio's sound

**On the radio.** The radio program sends what its speaker plays out on the
radio itself, always, as RTP: 16-bit samples, the more significant byte first,
48000 a second, one channel, 960 samples and twenty milliseconds to a packet,
fifty packets a second. RTP has no number of its own for that, and the payload
type is 96. It goes to the multicast group 239.255.73.88, port 5004, on the
loopback interface with a time to live of 0, so it never leaves the radio.
Any program on the radio may listen; the samples begin after RTP's 12 bytes.
The sound is taken before the sound card's volume, so its level does not
follow the radio's volume control; whoever listens sets their own.
`rtp_audio.c` is this.

It is the sound whole, for programs on the radio that need it so, a digital
mode's among them. Across the internet the sound is the telephone's, mu-law at
8000 samples a second, a twelfth of the bits, and the daemon makes the one of
the other, both ways: `clients/remote/telephone.c`. The radio program sent
mu-law itself until 2026-10-13.

**From the radio to the service.** Only while somebody is listening. The
service says so on the connection the daemon keeps:

    AUDIO ON 3f9a1c2b4d5e6f70 8d2e...       the service: the stream's id, 16 hex
                                            digits, and a nonce, 32
    AUDIO OFF                               the service, when the last listener goes

While the sound is on, the daemon also says where the radio is tuned, in Hz,
and its mode, when the sound begins and when either changes, no oftener than
once a second:

    TUNED 7074000 USB                       the daemon

After `AUDIO ON` the daemon sends each RTP packet it hears to the service by
UDP, to port 8081:

| Bytes | |
|---|---|
| 8 | the stream's id |
| 172 | the RTP packet as it was heard: 12 of header, 160 of sound |
| 8 | a tag |

There is no connection in UDP on which a challenge could have been answered,
so every packet carries its proof. The tag is the first 8 bytes of
HMAC-SHA256 of the 180 bytes before it. Its key is good for this stream alone
and is never sent: it is the 64 hex digits of

    HMAC-SHA256(the radio's stretched cookie, "zbitx-audio-1 <nonce>")

The service drops a packet whose id is no stream's, whose tag is wrong, or
whose RTP sequence number is not ahead of the last it took. Nothing is looked
up anywhere for a packet but in the service's own memory.

**To those listening.** The service passes each packet's 160 bytes of sound to
everyone listening to that radio. A listener that falls behind misses packets;
none is queued for it.

A web browser listens over a WebSocket, a browser having no UDP:
`wss://my.sbitx.net/listen?radio=<the radio's number>`, from the website's
globe, `https://my.sbitx.net/map`, which shows the radios that are on line and
gives each one's number. Anyone may listen, signed in or not: the author's
ruling, a radio's transmissions and receptions being public anyway. Each binary
message is one packet's 160 bytes of mu-law; a text message, `online` or
`offline`, says whether the radio is there, `tuned 7074000 USB` where it is
tuned and in what mode, whenever that changes, and `full` that a hundred are
listening to it already, or six from the one address. This stretch is inside
the website's HTTPS.

A client that takes the sound by UDP, as the window is to, is not built yet:
both it and the radio are behind routers, and it has to send to the service
first for there to be a way back to it.

A text message `lost 2` says that so many packets did not come before the
next that did, so that a player can cover the gap.

## What a radio copies in FT8

The author's ruling: every message a radio copies in FT8 is sent to the
service and kept there, all of them, apart from the accounts, and is public,
as what the radio plays is. The daemon asks the radio program for its
messages, `messages on`, as a client that shows them does, and for as long as
it is on line sends each one on the connection it keeps, with where the dial
is before it:

    DECODE 14074000 043015|-12|1797|CQ VU2ESE MK97

The service keeps the radio's number, when it was told, the dial, and the
message's own time, strength, pitch and text; a message a radio has said
already is not kept again. Anyone may read what a radio that is on the globe
has copied in the last quarter of an hour, or since a message they have:

    https://my.sbitx.net/decodes.json?radio=1
    https://my.sbitx.net/decodes.json?radio=1&after=5012

which answers a list, oldest first, of `id`, `heard` (seconds since 1970),
`hz`, `sent`, `strength` (null for one the radio sent itself), `pitch` and
`text`. The globe shows it in a box beside the radio that is chosen.

## Speech to transmit

The same path, the other way: the author's design. Whoever works the radio
from afar speaks, and the radio sends it.

**Into the radio program.** Speech to transmit is sent to the radio program as
RTP, by UDP, to port 5006 on the radio itself, `127.0.0.1`, and nowhere else:
the same RTP the radio's sound is, payload type 96, 960 16-bit samples to a
packet, with a label of eleven bytes before it: one for the kind of sound, 1
for speech from the owner working the radio from afar, 2 for sound from
another program on the radio, 3 for someone speaking over the internet, which
is to be heard and not transmitted and is not taken yet; and ten for the
callsign of whoever is speaking, with noughts after it. Any program on the radio may send it; the daemon does. While the radio
is sending, speech that comes takes the microphone's place, from its first
sample until that transmission ends, and a gap in it is silence, not the
microphone. Speech that comes while the radio is receiving is thrown away.
`MIC` sets its level, as it does the microphone's. Nothing in the speech says
when to send: that is `TX`, as ever.

**From the service to the daemon.** By UDP, to the address the radio's sound
is coming from, which is the one way in through the owner's router, and so
only while there is a stream. A packet is made as the sound's are:

    8 bytes     the stream's id
    172 bytes   an RTP packet, 160 bytes of mu-law after 12 of header
    8 bytes     the tag

The tag is made the same way with a key of its own,

    HMAC-SHA256(the radio's stretched cookie, "zbitx-speech-1 <nonce>")

the nonce being the stream's, from `AUDIO ON`. A packet of the radio's sound
therefore cannot be sent back as speech. The service numbers the packets one
after another for as long as the stream lasts, and the daemon drops one whose
id or tag is wrong or whose number is not ahead of the last it took, which
stops a packet that somebody kept from being sent again.

**From the owner to the service.** The owner's page sends each 160 bytes of
mu-law as a binary message on the WebSocket it works the radio by,
`wss://my.sbitx.net/work?radio=<number>`, which is the owner's alone. A binary
message of any other length is dropped.

## Not done yet

The window neither plays the radio's sound nor sends speech yet; both are for
browsers only. The copy of the
logbook is
the newest hundred entries, which is all `log` gives. Files that are not
controls, the radio's calibration in `hw_settings.ini` and the CW macros, are
not backed up; that needs a request for sending a file. And a kept copy of the
settings cannot yet be put back into a radio.
