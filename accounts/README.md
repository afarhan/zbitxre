# accounts: logins for sbitx and zbitx owners

The service behind `https://my.sbitx.net`. An owner registers a callsign with
an email address, confirms the address through a mailed link, and chooses a
password. Signed in, they add their radios, five at most, each by a name and
the cookie the radio shows in its settings; the radio is then `vu2ese-home`.
Beside each radio the site shows its client password, which with the radio's
name is what a client is given to reach the radio from afar.

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
port 8081 and answer its challenge, the radio with its cookie and a client
with the radio's client password. `PROTOCOL.md` has every line of it, and
`remote_client.py` is a client in a page:

    python3 accounts/remote_client.py vu2ese-home

## Not built yet

Payment; a page for customer service; reaching a radio from the GTK window
(only `remote_client.py` does it yet); sound; the whole logbook, of which the
radio's protocol gives the newest hundred entries; putting a kept copy of the
settings back into a radio.
