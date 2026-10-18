# accounts: logins for sbitx and zbitx owners

The service behind `https://my.sbitx.net`. It puts an owner's radio on line,
so that they can operate it from anywhere, on any computer or phone; it is on
the map, where others click on it and listen; and its owner can have QSOs
with others over the internet, VoIP, where the dial is the room.

## Putting a radio on line

One form does it, `/register`, with the radio switched on beside the owner.
It asks for:

1. the callsign and an email address, and the invitation code while
   registration is by invitation;
2. three boxes ticked, each a check on the radio. On the radio, tap **SET**:
   - **Callsign is set to your callsign entered above** (MYCALLSIGN; MYGRID
     too, to be on the map);

   then tap **CONNECTIONS** and turn on **REMOTE** and **STREAMING**:
   - **Confirmed that REMOTE is ON**
   - **Confirmed that STREAMING is ON**
3. the radio key: the twelve letters RADIO_KEY shows, at the top of
   CONNECTIONS, `abcd-jklm-wxyz`; and a name for the radio, `home` unless
   changed.

The boxes are there because people do not read steps, and a radio whose
callsign is not the account's, or whose REMOTE is off, never comes on line.
The radio signs in with the callsign set on it and its key. REMOTE and
STREAMING start ON on a radio that has never had them set.

A link is mailed to the address. Following it, the owner chooses a password,
the radio is added, and they are signed in. Their page waits for the radio,
looking again every five seconds; it comes on line within a quarter of a
minute and is then `vu2ese-home`, with **Operate** beside it. If two minutes
go by and it has not come, the page says what to check on the radio.

An owner already registered signs in and uses **Add Radio**, which asks for
the same three boxes, the key and a name. An account has room for five
radios.

**A key already in use** at my.sbitx.net is refused, on the form, when the
link is followed, at Add Radio and on a radio's Manage page: "That radio key
is already in use on my.sbitx.net. On the radio, tap RADIO_KEY, choose NEW
KEY, and type the new twelve letters here." Whose radio has it is not said.
A key seen twice is a copied card, or a radio sold and not removed.

The form asks the browser for a little work before it is sent, a proof of
work, to keep spammers out; it needs scripts turned on.

A program on another computer, the window started as `zbitx_gui remote
vu2ese-home` among them, signs in to the owner's radios with the account's
callsign and password.

## The programs

There are two programs, and each one's header says how it works:

- `accounts.py` is the website: registering, signing in, adding a radio, the
  pages that show an owner the copy kept of each radio's settings and
  logbook, and the globe at `/map`, where anyone may see the radios that are
  on line and listen to one.
- `remote.py` is reached over plain TCP, on port 8081: a radio's daemon
  connects to it and stays, the owner's client connects to it, and it passes
  what they say from the one to the other. `PROTOCOL.md`, at the top of the
  repository, is the account of that.

`remote.py` also takes a radio's sound from its daemon, by UDP on port 8081,
and gives it to the browsers listening at `/listen`, a WebSocket that nginx
hands it on a port of its own.

Both need Python 3 and nothing else, and they share one database.
`remote_client.py` is a client for a terminal, to try the service with.

## Trying it on your own computer

    python3 accounts/test_accounts.py        the website's tests
    make remote && python3 accounts/test_remote.py
                                             a radio reached from end to end,
                                             with the radio's own daemon
    python3 accounts/accounts.py serve       then open http://127.0.0.1:8100

Run that way it keeps `accounts.db` and `outbox.log` in the folder it is
started in. Mail is not sent: each one is written to `outbox.log`, where the
link to follow can be read. Registration asks for an invitation code, and
with no settings file there is none, so give it one:

    printf '[server]\nregistration = open\n' > /tmp/try.ini
    python3 accounts/accounts.py --settings /tmp/try.ini serve

## On the server

zbitxre is open, and this service is not only for my.sbitx.net: anyone may
run it on a server of their own and put their radios on line with it. A
radio is pointed at it by the line `SERVER=` in its
`data/user_settings.ini`, which the radio writes as `SERVER=my.sbitx.net`;
the owner puts their server's name there, and the radio takes it at its
next start. The window
reaching a radio from afar is given the server after the radio's name,
`zbitx_gui remote vu2ese-home radio.example.org`.

`setup/` has the files: `settings.ini`, a systemd unit for each program, and
the lines for nginx. Port 8081 has to be let through the firewall for
`remote.py`, for TCP and for UDP. The service runs as a user of its own, `sbitx-accounts`, with the
program in `/opt/sbitx-accounts`, the settings in `/etc/sbitx-accounts` and
the database in `/var/lib/sbitx-accounts`.

Mail goes through the SMTP server named in the settings. Its password is in a
file of its own, named in the settings and readable by the service's user
only; it is never in the repository.

## Customer service

On the server, as the service's user:

    sudo -u sbitx-accounts python3 /opt/sbitx-accounts/accounts.py \
        --settings /etc/sbitx-accounts/settings.ini list

and likewise `show CALLSIGN`, `suspend`, `restore`, `set-email CALLSIGN EMAIL`,
`rename OLD NEW`, `delete CALLSIGN`, `note CALLSIGN TEXT`.

## For the radio and for a client

Neither asks the website anything. The radio's daemon,
`clients/remote/zbitx_remote`, and a client both connect to `remote.py` on
port 8081 and answer its challenge, the radio with its key and a client with
the account's password. `PROTOCOL.md` has every line of it, and
`remote_client.py` is a client in a page:

    python3 accounts/remote_client.py vu2ese-home

## Not built yet

Payment; a page for customer service; the whole logbook, of which the
radio's protocol gives the newest hundred entries; putting a kept copy of the
settings back into a radio.
