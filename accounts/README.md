# accounts: logins for sbitx and zbitx owners

The service behind `https://my.sbitx.net`. An owner registers a callsign with
an email address, confirms the address through a mailed link, and chooses a
password. Signed in, they link their radios: each radio is given a key of its
own, which it saves and presents to the online services.

There are two programs, and each one's header says how it works:

- `accounts.py` is the website: registering, signing in, linking a radio or a
  client, and the pages that show an owner the copy kept of each radio's
  settings and logbook.
- `remote.py` is reached over plain TCP, on port 8081: a radio's daemon
  connects to it and stays, the owner's client connects to it, and it passes
  what they say from the one to the other. `PROTOCOL.md`, at the top of the
  repository, is the account of that.

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
`remote.py`. The service runs as a user of its own, `sbitx-accounts`, with the
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

## For the radio

    POST /api/radio/link        {"name": "shack sbitx", "kind": "radio"}
        -> {"device_code", "user_code", "verify_url", "expires_in", "interval"}
    POST /api/radio/link/poll   {"device_code": "..."}
        -> {"status": "pending"} | {"status": "expired"}
         | {"status": "linked", "key_id", "key", "callsign"}
    GET  /api/radio/whoami      Authorization: Bearer <key>
        -> {"callsign", "radio"}

The radio shows `user_code`; the owner types it at `verify_url`, signed in.
The key is given once. A client is linked in the same way, with
`"kind": "client"`. On the radio this is done by `clients/remote/zbitx_remote`.

## Not built yet

Payment; a page for customer service; reaching a radio from the GTK window
(only `remote_client.py` does it yet); sound; the whole logbook, of which the
radio's protocol gives the newest hundred entries; putting a kept copy of the
settings back into a radio.
