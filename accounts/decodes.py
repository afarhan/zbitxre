#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
decodes.py — what the radios copy in FT8, kept.

WHAT IT IS FOR

A radio in FT8 copies, every fifteen seconds, whatever stations it can
hear. The author asked that the service be sent these and keep them, all
of them, apart from everything else it keeps; that anyone looking at the
globe be able to choose a radio and see what it is hearing; and that what
is kept be there for whatever is thought of later, who is heard where and
when among it. What a radio hears is as public as what it plays, which
anyone may already listen to.

WHERE THEY ARE KEPT, AND HOW MANY

In a SQLite file of their own, not the one that holds the accounts: the
accounts are few, precious and backed up, and these are many and could be
had again from nowhere but are no loss to anyone's account. A radio on a
busy band copies thirty stations a quarter of a minute, a hundred thousand
and more a day. The author's ruling is that all are kept and nothing is
thrown away: a row is some sixty bytes, a radio's day a few megabytes, and
if the disk fills, that is a matter to be glad of and to settle then.

WHAT IS KEPT OF EACH

    radio      the radio's number at my.sbitx.net
    heard      when the service was told, seconds since 1970: the day the
               radio's own time, which has no date, belongs to
    hz         where the radio's dial was, as its daemon said
    sent       the time the radio put on it, "043015", UTC
    strength   in dB, or nothing for a message the radio sent itself
    pitch      in Hz, above the dial
    text       the message, "CQ VU2ESE MK97"

They are numbered as they come, and the number is how a page asks for
what is new: "those after 5012".

WHO USES IT

accounts/remote.py puts them in, a batch at a time, as radios' daemons
send them. accounts/accounts.py reads them out for the globe. Each opens
the file for itself.
"""

import contextlib
import sqlite3
import time

SCHEMA = """
create table if not exists decodes (
    id integer primary key,
    radio integer not null,
    heard integer not null,
    hz integer not null,
    sent text not null,
    strength integer,
    pitch integer not null,
    text text not null
);
create index if not exists decodes_of_a_radio on decodes (radio, id);
"""

# what the globe shows of a radio: the newest so many, if they are no older
# than this
SHOWN_MOST = 80
SHOWN_FOR_S = 15 * 60


class Decodes:
    def __init__(self, path):
        self.path = path
        with self.open() as db:
            db.executescript(SCHEMA)

    @contextlib.contextmanager
    def open(self):
        db = sqlite3.connect(self.path, timeout=10)
        db.row_factory = sqlite3.Row
        db.execute("pragma journal_mode = wal")
        try:
            with db:
                yield db
        finally:
            db.close()

    def add(self, rows):
        """rows of (radio, heard, hz, sent, strength, pitch, text) are kept"""
        if not rows:
            return
        with self.open() as db:
            db.executemany(
                "insert into decodes (radio, heard, hz, sent, strength, pitch, text) "
                "values (?, ?, ?, ?, ?, ?, ?)", rows)

    def recent(self, radio, after=0):
        """what a radio has copied lately, oldest first: with no number,
        the newest SHOWN_MOST of the last quarter of an hour; with one,
        what has come since it"""
        since = time.time() - SHOWN_FOR_S
        with self.open() as db:
            rows = db.execute(
                "select * from decodes where radio = ? and id > ? and heard >= ? "
                "order by id desc limit ?", (radio, after, since, SHOWN_MOST)).fetchall()
        return [dict(row) for row in reversed(rows)]

    def count(self):
        with self.open() as db:
            return db.execute("select count(*) from decodes").fetchone()[0]
