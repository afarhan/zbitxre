# The controls

Everything you can set on the radio is a control: a name and a value, such
as **AUDIO** at 60 or **MODE** at LSB. The radio keeps one table of them, and
every screen shows the same ones: the zBitx front panel, the window on the
radio's own screen or a desktop, and the web page at my.sbitx.net. A change
made on one is seen on all the others at once.

How a value is changed depends on where you are.

- On the zBitx front panel, a tap selects a control, turning the knob steps
  through the rows of a list, and a press of the knob acts on the row that
  is selected: a choice moves to its next value, a number starts being
  changed by the knob until the next press. A long press of the knob goes
  back to the main screen from anywhere.
- In the window, a tap or a click selects. The mouse wheel or the arrow keys
  step through the rows, and a press acts: the middle mouse button, Enter,
  or a second tap on the same row.
- On an sBitx, the right-hand control knob on the main screen changes the
  selected control (the volume, while the frequency is selected), and its
  press moves to the next control. In SET and CONNECTIONS it steps through
  the rows and its press acts.

Most controls are remembered when the radio is switched off, and come back
as they were at the next start. The **Kept** column in each table says which.
Actions, which do something rather than hold a value, are never kept.

In the tables below, a value written as "min–max, step N" is a number; a
list such as OFF / ON is a choice; "text" is something typed in. Where the
panel shows a control under a shorter name, it is given in brackets.

## The main screen: always shown

These are the radio's own controls, group RADIO. They are on the main screen
whatever the mode. Some have a place of their own on the top two rows, and
the rest are in the list on the right.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **FREQ** | 500000–30000000 Hz, step 1 | 7088000 | Yes | The frequency the radio is tuned to. It is the large figure on the main screen, and the knob, the mouse wheel or the tuning wheel changes it. |
| **BAND** | 80M / 60M / 40M / 30M / 20M / 17M / 15M / 12M / 10M | 40M | No (follows from FREQ) | The amateur band the radio is on, shown as the row of band buttons; it reads OUT when the frequency is in no band. Choosing a band goes back to the frequency and mode last used on it. Touching the button of the band already in use goes to the next of its four remembered places. |
| **MODE** | USB / LSB / CW / CWR / AM / FT8 / 2TONE / RADE1 / RADE2 / VOIP | LSB | Yes | The mode. Changing it takes the radio back to receive first, and changes which controls the list shows. |
| **AUDIO** (VOL) | 0–100, step 1 | 60 | Yes | The volume. 0 is silence, and 1 to 100 are spread over the 40 dB of the sound card's useful range. |
| **IF** | 0–100, step 1 | 60 | Yes | The gain of the sound card's input, where the second IF comes in. |
| **DRIVE** | 0–100, step 1 | 40 | Yes | The transmitter's drive. The radio scales it for the band in use. |
| **AGC** | OFF / SLOW / MED / FAST | SLOW | Yes | How fast the receiver's gain follows the signal. |
| **TX** | OFF / ON | OFF | No | Whether the radio is transmitting. It is not a row of the list: in the voice modes, RADE and 2TONE it is worked by the TX and RX buttons on the bottom row, and by the microphone's PTT. Modes that cannot transmit, AM for now and VOIP always, refuse it. The radio goes back to receive by itself after two minutes of continuous transmission. |

Beside them on the top row are **STEP** and **VFO**, which belong to the
group PANEL and are described under "Behind the scenes", and **OFFSET**,
which is not one of the radio's controls at all. **OFFSET** (NORM / RIT /
SPLIT) is kept by each screen for itself and starts at NORM each time, so
that the radio never comes up with RIT on. At RIT, tuning moves the receiver
by up to 25 kHz either way while the dial stays; at SPLIT, the radio
receives on VFO A and transmits on VFO B. It is shown in red while it is not
NORM.

## Voice: USB, LSB and AM

The group VOICE. Shown in USB, LSB and AM.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **MIC** | 0–100, step 1 | 40 | Yes | The gain of the microphone input. It sits on the bottom row, beside TX and RX, and is also shown there in RADE1, RADE2 and VOIP. |
| **BW_VOICE** (BW) | 300–3000 Hz, step 50 | 2200 | Yes | The width of the receiver's passband in the voice modes. The low edge stays at 300 Hz and this moves the high edge. AM, 2TONE and VOIP use the same bandwidth. |

The bottom row also has the TX and RX buttons, which set **TX** ON and OFF.

## CW: CW and CWR

The group CW. Shown in CW and CWR. The window lists them in this order:
BW, then **WPM**, **PITCH**, **SIDETONE**, **CW_DELAY** and **CW_INPUT**.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **WPM** | 5–50 WPM, step 1 | 20 | Yes | The keyer's speed, in words a minute. Macros and typed text are sent at this speed too, and the decoder starts from it. |
| **PITCH** | 100–3000 Hz, step 10 | 600 | Yes | The tone a CW signal is heard at, and the sidetone's pitch. The receiver's passband is centred on it. |
| **BW_CW** (BW) | 50–3000 Hz, step 50 | 400 | Yes | The width of the passband in CW, from PITCH less half of it to PITCH plus half. A signal further off the pitch than half the bandwidth is not heard. CW keeps this apart from the voice modes' bandwidth. |
| **SIDETONE** | 0–100, step 1 | 30 | Yes | How loud the sidetone is in the speaker while you send. |
| **CW_DELAY** (DELAY) | 0–1000 ms, step 50 | 300 | Yes | How long the radio stays in transmit after the last element before it goes back to receive. |
| **CW_INPUT** (INPUT) | STRAIGHT / IAMBIC / IAMBICB / BUG / ULTIMATIC | IAMBIC | Yes | What is plugged into the key jack and how the keyer works it. STRAIGHT: a straight key on the dot contact. IAMBIC: iambic A. IAMBICB: iambic B, which sends one more opposite element after a squeeze let go during an element. ULTIMATIC: with both paddles closed, the last one closed is repeated. BUG: the dot paddle makes dots and the dash paddle is a straight key. |
| **MACRO** | the labels of the macros in data/cw.mc, such as CQ / RST / End | — | No (action) | Sends a macro, with your callsign and the logger's fields put in. The first six are the buttons on the bottom row in CW. Refused in other modes. There is no such control when the macro file is missing. A touch of the paddle, or anything typed, stops a macro and drops what was waiting. |
| **CW_TEXT** | text | — | No (action) | Text typed on a keyboard, sent in CW as it is typed. In the window the KBD button brings up a keyboard on the screen for this, and a keyboard plugged into the radio sends too. Refused in other modes. |
| **CW_KEY** | dots and dashes, and the gap before them in ms | — | No (action) | A character keyed on another device's own keyer, such as a telephone's screen. The radio sends it evenly at WPM and keeps the spacing as it was keyed. Refused in other modes. |
| **CW_DO** | ESC | — | No (action) | ESC stops the text or macro being sent and drops whatever was waiting. In the window it is the ESC key on the KBD screen, and the Escape key on a keyboard. |

## FT8

The group FT8. Shown in FT8. In this mode the list of copied messages takes
the place of the spectrum and waterfall. The panel shows these controls
without the FT8_ at the front of their names.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **TX_PITCH** | 200–3000 Hz, step 10 | 1500 | Yes | Where in the passband your own signal is sent. In the window a tap on the graph in FT8 sets it, and a red line marks it. |
| **FT8_SIDETONE** (SIDETONE) | 0–100, step 1 | 30 | Yes | How loud your own transmission is in the speaker; 0 is silence. It is kept apart from the CW sidetone. |
| **FT8_AUTO** (AUTO) | OFF / ON | OFF | Yes | ON: while no contact is under way, a station that calls you is answered by itself. OFF: such a call is only shown in the list. It starts OFF so that the radio never transmits by itself until you have said it may. |
| **FT8_REPEAT** (REPEAT) | 1–5, step 1 | 5 | Yes | How many times a message is sent without an answer before the contact is given up. |
| **FT8_TX1ST** (TX1ST) | OFF / ON | ON | Yes | Which slots your CQ goes in. ON: the first and third of the minute, at :00 and :30. OFF: at :15 and :45. An answer to another station always goes in the slots it did not use. |
| **FT8_DO** | CQ / HALT | — | No (action) | CQ starts calling CQ. HALT stops whatever is being sent and ends the contact without logging it. They are the CQ and HALT buttons in the left-hand column. |

**TX** is not used by hand in FT8: the radio keys itself in the slot a
message is due. A message is answered by marking it in the list and pressing
the knob, or by tapping it twice in the window. How FT8 is worked, and what
is sent at the end of a contact, are set in SET with **FT8_OPS** and
**FT8_REPLY**.

## RADE: RADE1 and RADE2

The group RADE, shared by both RADE modes. The bottom row has **MIC** and the
TX and RX buttons, as in the voice modes.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **SQUELCH** | OFF / ON | ON | Yes | ON: the speaker is silent while no RADE signal is being received. OFF: the plain SSB audio is heard while there is no signal, which can help when tuning a station in by ear. A station can still be found with it ON. |
| **RADE_STATUS** | text | -- | Yes (see note) | What the decoder says: "--" with no signal, and with one its signal to noise ratio and how far off tune it is, such as "SYNC 12 dB +3 Hz". Neither the panel nor the window lists it, since it is a text. |

Note: **RADE_STATUS** is not among the controls that settings.c leaves out,
so it is written to the settings file with the rest, though it is a report
and not a setting.

## VOIP

The group VOIP. Shown in the mode VOIP. In this mode the transmitter is never
keyed: the speaker is silent but for others speaking over VoIP, and the TX
and RX buttons on the bottom row send your speech to my.sbitx.net, to the
radios tuned to the same kilohertz. **TX** shows blue rather than red while
you are speaking over VoIP.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **VOIP** | OFFLINE / ONLINE | OFFLINE | Yes | Whether the radio is on VoIP at my.sbitx.net, where the dial frequency is the room. While ONLINE, others' speech on your frequency is heard. |
| **BREAK_IN** | OFF / ON | ON | Yes | ON: speech over VoIP breaks in on the other modes. OFF: it is heard only in the mode VOIP. |
| **VOIP_TX** | OFF / ON | OFF | No | The push-to-talk of VoIP: ON while you are speaking over the internet. It is the TX and RX buttons in this mode, and the PTT sets it too. It is refused unless the mode is VOIP and **VOIP** is ONLINE. |

**VOIP_STATUS** and **VOIP_SPEAKER** are under "Status texts" below.

## 2TONE

2TONE has no controls of its own: the list shows only the RADIO group. It
sends two steady tones of equal strength, 700 Hz and 1900 Hz above the
frequency, as an upper sideband signal, for testing and measuring the
transmitter. The bottom row has the TX and RX buttons. It receives as USB
does, with **BW_VOICE**.

## SET

SET on the main screen opens the settings: the group STATION. The panel
shows them under friendlier names, given in brackets, and also has rows for
the date and time, WiFi, the panel's firmware and BOOTSEL, which are
described under "Behind the scenes" where they are controls. The last row,
CONNECTIONS, opens the next screen.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **MYCALLSIGN** (Callsign) | text | empty | Yes | Your callsign. The macros put it in, it is logged with each entry, and without it the radio cannot call CQ or answer anyone in FT8. |
| **MYGRID** (Grid) | text | empty | Yes | Your grid locator. The macros and FT8 put it in. |
| **FT8_OPS** (FT8 Operations) | NORMAL / RUNNING / POUNCING | NORMAL | Yes | How FT8 is worked. RUNNING: CQ is called until someone answers, and called again after each contact. POUNCING: the radio never calls CQ; you answer the CQs you mark. NORMAL: a CQ is called no more than three times unanswered, and a marked message is answered as in POUNCING. |
| **FT8_REPLY** (FT8 Reply) | RR73 / RRR | RR73 | Yes | What you send, having called CQ, once the other station's R and report have come. |
| **UPDATE** (Update) | - / UPDATING / CANCEL | - | No | Brings the radio up to the newest zbitxre, the panel's firmware with it. A press asks first, in a box; yes sets it to UPDATING and the progress is shown in a view of its own. CANCEL stops it. When something new was built the radio restarts by itself, once it is not transmitting. |

## CONNECTIONS

CONNECTIONS is the last row of SET. It holds what the radio has to do with
the world outside itself: the group CONNECTIONS, with **RADIO_KEY** first.
The window does not offer this screen when it is itself reaching the radio
through my.sbitx.net: these are set at the radio and nowhere else.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **RADIO_KEY** | text | empty until the radio says it | No | The radio's key for my.sbitx.net, of the form abcd-jklm-wxyz, which you type at the website to add the radio to your account. A press asks whether to make a new one, for when the old one has gone where it should not; yes makes a new key, which then has to be typed in at the website. |
| **REMOTE** | OFF / ON | ON | Yes | Whether the radio may be worked from afar through my.sbitx.net. |
| **STREAMING** | OFF / ON | ON | Yes | Whether anyone may listen to the radio at my.sbitx.net and see what it copies in FT8. |
| **BLUETOOTH** | OFF / ON | OFF | Yes | Whether the radio's sound is played into a Bluetooth earphone or speaker paired with the radio's computer. |
| **CAT** | OFF / ON | OFF | Yes | Whether other programs may set the radio's frequency and mode and key it, through Hamlib's "NET rigctl". **CAT_STATUS** says the address and port to give them. |

Under the switches the screen shows the status texts of the next section,
which can be read but not changed.

## Status texts

These are what the radio, or the program that links it to the network, has
to say. None can be changed. None is kept, except as noted for
**RADE_STATUS** above.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **REMOTE_STATUS** | text | empty | No | What there is to say of the link to my.sbitx.net, such as ONLINE and the name the radio was given there. Shown on CONNECTIONS. |
| **BLUETOOTH_STATUS** | text | empty | No | What there is to say of the Bluetooth sound. Shown on CONNECTIONS. |
| **SOUND_CARD_STATUS** | text | empty | No | The names of the two sound devices the radio offers other programs, such as WSJT-X and FreeDV, to hear the receiver by and send by, or why they are not there. These devices are always offered while the radio runs. Shown on CONNECTIONS. |
| **CAT_STATUS** | text | empty | No | The address and port to give a program that is to use CAT. Shown on CONNECTIONS. |
| **VOIP_STATUS** | text | empty | No | The VoIP channel and how many radios are in it, "7074 KHZ, 3 HERE", or why your speech was not passed, "BUSY VU3XYZ". Shown on CONNECTIONS. |
| **VOIP_SPEAKER** | text | empty | No | The callsign of whoever is being heard over VoIP now, empty while nobody is. Shown on the status line above the spectrum, in any voice mode. |
| **UPDATE_STATUS** | text | empty | No | Each line the update says as it runs. The last begins DONE:, FAILED: or CANCELLED:. Shown in the update's own view. |

## Behind the scenes

These controls are not in any list. Some are worked by a button or a gesture
of their own; some are kept by the radio for the screens; some are reports.

### Tuning and the spectrum (group PANEL)

STEP and the VFOs are the radio's, common to every screen, so that the
panel, the window and the web page tune by the same step and share the same
two VFOs. Each screen still does its own tuning.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **STEP** | 10H / 100H / 500H / 1K / 10K | 100H | Yes | How far a notch of the knob or wheel tunes. The dial moves onto a multiple of it. It is on the top row, left of the frequency. |
| **VFO** | A / B | A | Yes | Which VFO is in use. Each has a dial of its own; choosing the other keeps the dial of the one left. It is on the top row; on the panel, a press of the knob while the frequency is selected also goes to the other VFO. |
| **VFO_A** | 0–30000000 Hz, step 1 | 0 | Yes | VFO A's dial, kept for the screens. 0 is a VFO not yet used, which opens where the other one is. |
| **VFO_B** | 0–30000000 Hz, step 1 | 0 | Yes | VFO B's dial, kept in the same way. |
| **SPAN** | 25K / 10K / 6K / 2.5K | 25K | Yes | How much of the spectrum the graph and waterfall show, the tuned frequency staying in the middle. A touch on the graph or the waterfall goes to the next span. |

### The band stack (group BAND)

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **STACK** | 0–3, step 1 | 0 | No (the places themselves are kept) | Which of the band's four remembered places, each a frequency and a mode, the radio is at. Touching the button of the band already in use steps it. Where you are is kept in the place being left. Refused when the radio is on no band. |

### Station housekeeping (group SYSTEM)

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **CLOCK** | text | NOT SET | No | The radio's time in UTC, to the minute, and where it came from: "2026-10-02 13:47 NTP", RTC or SET, or NOT SET. It cannot be set; it is shown in SET as Date/Time on the panel. |
| **TIME_SET** | 0–39445919 minutes, step 1 | 0 | No | The time you give the radio when it has none: minutes since the start of 2026, in UTC. The panel's Date/Time screen sets it with its rollers and SYNC. It is used only until the radio has the time from the network or its clock chip again. |
| **WIFI** | SCAN | — | No (action) | Asks the radio to scan for WiFi access points. The panel's WiFi screen in SET does it when opened, lists what is heard, the strongest first, and lets you join one. |

### Meters (group METER)

These are measured, not set, and are never kept. The screens show them on
their meters.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **SMETER** | 0–9999 | 0 | No | The received signal's strength: the S units times a hundred, plus the dB over the last whole unit, so 704 is S7 and 4 dB. It is 0 while transmitting. |
| **POWER** | 0–999, in tenths of a watt | 0 | No | The forward power while transmitting. On the zBitx the front panel measures it; on the sBitx the radio reads its own bridge. |
| **SWR** | 0–999, in tenths | 0 | No | The SWR while transmitting: 13 is 1.3. It is 0 with no power. On the sBitx, below 3 W the bridge cannot be trusted and it reads 1.0. |
| **BATTERY** | 0–9999, in hundredths of a volt | 0 | No | The battery voltage, as the zBitx front panel measures it. |

### The logbook entry (group LOGGER)

These are the fields of an entry written by hand for a contact in CW or on
the voice. They are on the LOG screen, not in the main list, and are never
kept: they are cleared when the entry is saved. FT8 logs its contacts by
itself.

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **CALL** | text | empty | No | The other station's callsign. It is logged in capitals. The ? button on the LOG screen searches the logbook for it. |
| **SENT** | text | empty | No | The report you sent, such as 599. |
| **RECV** | text | empty | No | The report you received. |
| **EXCH** | text | empty | No | The exchange you received: a serial number, a grid or a name. |
| **NR** | text | empty | No | The exchange you sent, most often a contest serial number. |
| **LOGGER** | SAVE / WIPE | — | No (action) | SAVE writes the entry to the logbook, in the mode and on the frequency in use, at the radio's time. It needs at least CALL, SENT and RECV, and a clock that is set; otherwise it says what is missing and keeps what was typed. WIPE clears the five fields. |

### Internal

| Control | Values | Starts at | Kept | What it does |
|---|---|---|---|---|
| **NEW_COOKIE** | - / MAKING | - | No | Set to MAKING when you answer yes to making a new radio key from **RADIO_KEY**. The new key appears in **RADIO_KEY** and this goes back to -. |
