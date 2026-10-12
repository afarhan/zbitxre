# accounts: logins for sbitx and zbitx owners

The service behind `https://my.sbitx.net`. An owner registers a callsign with
an email address, confirms the address through a mailed link, and chooses a
password. Signed in, they link their radios: each radio is given a key of its
own, which it saves and presents to the online services.

`accounts.py` is the whole of it and its header says how it works. It needs
Python 3 and nothing else.

## Trying it on your own computer

    python3 accounts/test_accounts.py        the tests
    python3 accounts/accounts.py serve       then open http://127.0.0.1:8100

Run that way it keeps `accounts.db` and `outbox.log` in the folder it is
started in. Mail is not sent: each one is written to `outbox.log`, where the
link to follow can be read. Registration asks for an invitation code, and
with no settings file there is none, so give it one:

    printf '[server]\nregistration = open\n' > /tmp/try.ini
    python3 accounts/accounts.py --settings /tmp/try.ini serve

## On the server

`setup/` has the three files: `settings.ini`, the systemd unit, and the lines
for nginx. The service runs as a user of its own, `sbitx-accounts`, with the
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

    POST /api/radio/link        {"name": "shack sbitx"}
        -> {"device_code", "user_code", "verify_url", "expires_in", "interval"}
    POST /api/radio/link/poll   {"device_code": "..."}
        -> {"status": "pending"} | {"status": "linked", "key", "callsign"} | {"status": "expired"}
    GET  /api/radio/whoami      Authorization: Bearer <key>
        -> {"callsign", "radio"}

The radio shows `user_code`; the owner types it at `verify_url`, signed in.
The key is given once.

## Not built yet

Payment; a page for customer service; the radio's side of linking; signing in
from the remote UI.
