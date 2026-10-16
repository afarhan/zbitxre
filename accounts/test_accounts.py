#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ashhar Farhan
"""
test_accounts.py — the accounts service, tried over HTTP as a browser and a
radio use it.

    python3 accounts/test_accounts.py

The service is started on a port of its own with a database in a temporary
folder, mail kept in a list here instead of sent, and the proof of work set
to 8 bits so that the tests do it quickly. Each test has a service of its
own. If node is on the computer, the browser's script is also run, to see
that the answer it finds is one the service takes.
"""

import hashlib
import http.client
import decodes
import json
import time
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
import urllib.parse
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import accounts

PASSWORD = "correct horse battery"
INVITE = "test-group"


class Mail:
    def __init__(self):
        self.sent = []

    def send(self, to, subject, text):
        self.sent.append((to, subject, text))

    def last_link(self):
        return re.search(r"/password\?t=(\S+)", self.sent[-1][2]).group(1)


def solve(challenge, bits):
    n = 0
    while accounts.zero_bits(hashlib.sha256(("%s:%d" % (challenge, n)).encode()).digest()) < bits:
        n += 1
    return str(n)


class Browser:
    """a browser: it keeps the cookie and says where its forms come from"""

    def __init__(self, port):
        self.port = port
        self.cookie = ""
        self.origin = "http://127.0.0.1:%d" % port

    def ask(self, method, path, form=None, headers=None, body=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20)
        sent = {"Origin": self.origin}
        if self.cookie:
            sent["Cookie"] = self.cookie
        if form is not None:
            body = urllib.parse.urlencode(form)
            sent["Content-Type"] = "application/x-www-form-urlencoded"
        sent.update(headers or {})
        connection.request(method, path, body=body, headers=sent)
        answer = connection.getresponse()
        text = answer.read().decode("utf-8", "replace")
        cookie = answer.getheader("Set-Cookie")
        if cookie:
            self.cookie = cookie.split(";")[0]
        location = answer.getheader("Location")
        connection.close()
        if answer.status == 303:
            return self.ask("GET", location)
        return answer.status, text

    def get(self, path):
        return self.ask("GET", path)

    def post(self, path, form, **more):
        return self.ask("POST", path, form=form, **more)

    def hidden(self, text, name):
        return re.search(r'name="%s" value="([^"]*)"' % name, text).group(1)

    def register(self, callsign, email, invite=INVITE):
        _, text = self.get("/register")
        challenge = self.hidden(text, "pow_challenge")
        return self.post("/register", {
            "callsign": callsign, "email": email, "invite": invite,
            "pow_challenge": challenge,
            "pow_signature": self.hidden(text, "pow_signature"),
            "pow_answer": solve(challenge, 8),
        })


class AccountsTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        settings = accounts.Settings()
        settings.database = os.path.join(self.folder, "accounts.db")
        settings.decodes = os.path.join(self.folder, "decodes.db")
        settings.registration = "invite"
        settings.invite_code = INVITE
        settings.support = "support@example.org"
        settings.pow_bits = 8
        self.mail = Mail()
        self.service = accounts.Service(settings, self.mail)
        handler = type("TestHandler", (accounts.Handler,), {"service": self.service,
                                                           "log_message": lambda *a: None})
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.server.daemon_threads = True
        self.port = self.server.server_address[1]
        settings.base_url = "http://127.0.0.1:%d" % self.port
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.browser = Browser(self.port)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        shutil.rmtree(self.folder)

    def make_account(self, callsign="VU2ESE", email="farhan@example.org", browser=None):
        browser = browser or self.browser
        status, text = browser.register(callsign, email)
        self.assertIn("We have mailed a link", text)
        _, text = browser.get("/password?t=" + self.mail.last_link())
        self.assertIn("A password for", text)
        status, text = browser.post("/password", {"t": self.mail.last_link(),
                                                  "password": PASSWORD, "again": PASSWORD})
        self.assertIn("Signed in as <strong>%s</strong>" % callsign, text)
        return browser

    def add_radio(self, browser, suffix="home", cookie="abcd-efgh-jkmn"):
        _, text = browser.get("/")
        return browser.post("/radios/add", {"guard": browser.hidden(text, "guard"),
                                            "suffix": suffix, "cookie": cookie})[1]

    # registering

    def test_register_verify_and_sign_in(self):
        self.make_account()
        self.assertEqual(self.mail.sent[0][0], "farhan@example.org")
        fresh = Browser(self.port)
        _, text = fresh.post("/signin", {"callsign": "vu2ese", "password": PASSWORD})
        self.assertIn("Signed in as <strong>VU2ESE</strong>", text)

    def test_no_account_until_the_link_is_followed(self):
        self.browser.register("VU2ESE", "farhan@example.org")
        _, text = self.browser.post("/signin", {"callsign": "VU2ESE", "password": PASSWORD})
        self.assertIn("The callsign or the password is wrong", text)

    def test_a_link_works_once(self):
        self.make_account()
        link = self.mail.last_link()
        _, text = Browser(self.port).get("/password?t=" + link)
        self.assertIn("has been used or has run out", text)

    def test_a_registered_callsign_cannot_be_registered_again(self):
        self.make_account()
        _, text = Browser(self.port).register("VU2ESE", "someone@example.com")
        self.assertIn("VU2ESE is already registered", text)
        self.assertEqual(len(self.mail.sent), 1)

    def test_a_pending_callsign_is_not_taken_over_by_another_address(self):
        self.browser.register("VU2ESE", "farhan@example.org")
        _, text = Browser(self.port).register("VU2ESE", "someone@example.com")
        self.assertIn("is being registered with another address", text)
        self.assertEqual(len(self.mail.sent), 1)

    def test_what_is_not_a_callsign_or_an_address_is_refused(self):
        for callsign in ("VU2ESE/P", "HELLO", "12345", "A1", "VU2 ESE"):
            _, text = Browser(self.port).register(callsign, "a@example.org")
            self.assertIn("does not look like a callsign", text, callsign)
        _, text = Browser(self.port).register("VU2XYZ", "not-an-address")
        self.assertIn("does not look like an email address", text)
        self.assertEqual(self.mail.sent, [])

    def test_the_invitation_code_is_needed(self):
        _, text = self.browser.register("VU2ESE", "farhan@example.org", invite="guess")
        self.assertIn("not the invitation code", text)
        self.assertEqual(self.mail.sent, [])

    def test_closed_registration_registers_nobody(self):
        self.service.settings.registration = "closed"
        _, text = self.browser.get("/register")
        self.assertIn("Registration is not open yet", text)
        ok, _ = self.service.register("VU2ESE", "farhan@example.org", INVITE, "1.2.3.4")
        self.assertFalse(ok)

    def test_passwords_must_match_and_be_long_enough(self):
        self.browser.register("VU2ESE", "farhan@example.org")
        link = self.mail.last_link()
        _, text = self.browser.post("/password", {"t": link, "password": "short",
                                                  "again": "short"})
        self.assertIn("at least 10 characters", text)
        _, text = self.browser.post("/password", {"t": link, "password": PASSWORD,
                                                  "again": PASSWORD + "x"})
        self.assertIn("not the same", text)

    def test_the_password_is_not_kept_as_text(self):
        self.make_account()
        with open(os.path.join(self.folder, "accounts.db"), "rb") as file:
            self.assertNotIn(PASSWORD.encode(), file.read())

    # proof of work

    def test_registration_without_the_work_is_refused(self):
        _, text = self.browser.get("/register")
        _, text = self.browser.post("/register", {
            "callsign": "VU2ESE", "email": "farhan@example.org", "invite": INVITE,
            "pow_challenge": self.browser.hidden(text, "pow_challenge"),
            "pow_signature": self.browser.hidden(text, "pow_signature"),
            "pow_answer": ""})
        self.assertIn("before the browser had finished its work", text)
        self.assertEqual(self.mail.sent, [])

    def test_an_answer_is_taken_once(self):
        _, text = self.browser.get("/register")
        challenge = self.browser.hidden(text, "pow_challenge")
        form = {"callsign": "VU2ESE", "email": "farhan@example.org", "invite": INVITE,
                "pow_challenge": challenge,
                "pow_signature": self.browser.hidden(text, "pow_signature"),
                "pow_answer": solve(challenge, 8)}
        _, text = self.browser.post("/register", form)
        self.assertIn("We have mailed a link", text)
        form["callsign"] = "VU2XYZ"
        _, text = self.browser.post("/register", form)
        self.assertIn("before the browser had finished its work", text)

    def test_a_challenge_of_our_own_making_is_refused(self):
        challenge = "9999999999.0011223344556677"
        self.assertFalse(self.service.is_pow_done(challenge, "0" * 64, solve(challenge, 8)))

    def test_a_challenge_that_has_run_out_is_refused(self):
        challenge = "1000000000.0011223344556677"
        self.assertFalse(self.service.is_pow_done(
            challenge, self.service.pow_sign(challenge), solve(challenge, 8)))

    @unittest.skipUnless(shutil.which("node"), "node is not on this computer")
    def test_the_browsers_script_finds_an_answer_the_service_takes(self):
        challenge, signature = self.service.pow_challenge()
        page = """
            var form = { elements: { pow_challenge: { value: %s }, pow_answer: { value: "" } },
                getAttribute: function () { return "12"; },
                querySelector: function () { return { textContent: "Register" }; } };
            global.document = { querySelectorAll: function () { return [form]; } };
            %s
            (function wait() {
                if (form.elements.pow_answer.value === "") { setTimeout(wait, 5); return; }
                console.log(form.elements.pow_answer.value);
            })();
        """ % (json.dumps(challenge), accounts.POW_SCRIPT)
        answer = subprocess.run(["node", "-e", page], capture_output=True, text=True,
                                timeout=60).stdout.strip()
        self.service.settings.pow_bits = 12
        self.assertTrue(self.service.is_pow_done(challenge, signature, answer), answer)

    # signing in

    def test_a_wrong_password_is_refused_and_five_lock_the_callsign(self):
        self.make_account()
        fresh = Browser(self.port)
        for _ in range(5):
            _, text = fresh.post("/signin", {"callsign": "VU2ESE", "password": "wrong wrong"})
            self.assertIn("The callsign or the password is wrong", text)
        _, text = fresh.post("/signin", {"callsign": "VU2ESE", "password": PASSWORD})
        self.assertIn("Too many wrong tries", text)

    def test_a_form_from_another_site_is_refused(self):
        self.make_account()
        status, _ = Browser(self.port).post(
            "/signin", {"callsign": "VU2ESE", "password": PASSWORD},
            headers={"Origin": "https://elsewhere.example"})
        self.assertEqual(status, 403)

    def test_a_signed_in_form_without_its_guard_is_refused(self):
        browser = self.make_account()
        status, _ = browser.post("/signout", {"guard": "0" * 64})
        self.assertEqual(status, 403)
        _, text = browser.get("/")
        self.assertIn("Signed in as", text)

    def test_sign_out(self):
        browser = self.make_account()
        _, text = browser.get("/")
        _, text = browser.post("/signout", {"guard": browser.hidden(text, "guard")})
        _, text = browser.get("/")
        self.assertNotIn("Signed in as", text)

    def test_every_page_has_the_bar_saying_who_is_signed_in(self):
        browser = self.make_account()
        for path in ("/", "/map", "/signin", "/register", "/no-such-page"):
            _, text = browser.get(path)
            self.assertIn("<nav class=\"bar\">", text, path)
            self.assertIn("Signed in as <strong>VU2ESE</strong>", text, path)
            self.assertIn("action=\"/signout\"", text, path)
        _, text = browser.get("/map")
        _, text = browser.post("/signout", {"guard": browser.hidden(text, "guard")})
        for path in ("/", "/map"):
            _, text = Browser(self.port).get(path)
            self.assertIn("<nav class=\"bar\">", text, path)
            self.assertIn("href=\"/signin\">Sign in</a>", text, path)

    def test_forgot_mails_a_link_that_sets_a_new_password(self):
        self.make_account()
        fresh = Browser(self.port)
        _, text = fresh.post("/forgot", {"callsign": "VU2ESE"})
        self.assertIn("If that callsign has an account", text)
        self.assertEqual(len(self.mail.sent), 2)
        new = "another long password"
        fresh.post("/password", {"t": self.mail.last_link(), "password": new, "again": new})
        _, text = Browser(self.port).post("/signin", {"callsign": "VU2ESE", "password": new})
        self.assertIn("Signed in as", text)
        _, text = Browser(self.port).post("/signin", {"callsign": "VU2ESE",
                                                      "password": PASSWORD})
        self.assertIn("is wrong", text)

    def test_forgot_says_the_same_for_no_account_and_mails_nothing(self):
        _, text = self.browser.post("/forgot", {"callsign": "VU2NOBODY"})
        self.assertIn("If that callsign has an account", text)
        self.assertEqual(self.mail.sent, [])

    # radios

    def test_a_radio_is_added_with_its_cookie_and_given_a_client_password(self):
        browser = self.make_account()
        text = self.add_radio(browser, "Home", "ABCD EFGH JKMN")
        self.assertIn("vu2ese-home is added", text)
        radio = self.service.radio_called("VU2ESE-home")
        self.assertEqual(radio["cookie"], "abcdefghjkmn")
        self.assertRegex(radio["client_password"], r"^[a-z]{12}$")
        self.assertNotEqual(radio["client_password"], radio["cookie"])
        self.assertIn(accounts.show_secret(radio["client_password"]), text)
        self.assertNotIn("abcd-efgh-jkmn", text.split("Add a radio")[0])

    def test_what_is_not_a_cookie_or_a_name_is_refused(self):
        browser = self.make_account()
        self.assertIn("That is not a cookie", self.add_radio(browser, "home", "abcd-efgh"))
        self.assertIn("That is not a cookie", self.add_radio(browser, "home", "1234-5678-9012"))
        self.assertIn("up to ten letters and figures",
                      self.add_radio(browser, "my home", "abcd-efgh-jkmn"))
        self.assertEqual(self.service.radios_of("VU2ESE"), [])

    def test_a_name_is_used_once_and_there_is_room_for_five(self):
        browser = self.make_account()
        for suffix in ("a", "b", "c", "d", "e"):
            self.assertIn("is added", self.add_radio(browser, suffix))
        self.assertIn("room for 5 radios", self.add_radio(browser, "f"))
        self.assertEqual(len(self.service.radios_of("VU2ESE")), 5)
        other = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        self.add_radio(other, "home")
        self.assertIn("called home already", self.add_radio(other, "home"))

    def test_a_new_client_password_replaces_the_old(self):
        browser = self.make_account()
        self.add_radio(browser)
        before = self.service.radio_called("vu2ese-home")
        _, text = browser.get("/")
        browser.post("/radios/password", {"guard": browser.hidden(text, "guard"),
                                          "radio": str(before["id"])})
        after = self.service.radio_called("vu2ese-home")
        self.assertNotEqual(after["client_password"], before["client_password"])
        self.assertEqual(after["cookie"], before["cookie"])

    def test_a_new_cookie_replaces_the_old_and_keeps_the_name_and_password(self):
        browser = self.make_account()
        self.add_radio(browser, "home", "abcd-efgh-jkmn")
        before = self.service.radio_called("vu2ese-home")
        _, text = browser.get("/")
        guard = browser.hidden(text, "guard")
        _, text = browser.post("/radios/cookie", {"guard": guard, "radio": str(before["id"]),
                                                  "cookie": "PQRS-TUVW-XYZA"})
        self.assertIn("has its new cookie", text)
        after = self.service.radio_called("vu2ese-home")
        self.assertEqual(after["cookie"], "pqrstuvwxyza")
        self.assertEqual(after["client_password"], before["client_password"])
        self.assertEqual(after["id"], before["id"])
        _, text = browser.post("/radios/cookie", {"guard": guard, "radio": str(before["id"]),
                                                  "cookie": "pqrs-tuvw-xyza"})
        self.assertIn("That is the cookie it has", text)
        _, text = browser.post("/radios/cookie", {"guard": guard, "radio": str(before["id"]),
                                                  "cookie": "pqrs"})
        self.assertIn("That is not a cookie", text)
        # another owner cannot change it
        other = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        _, text = other.get("/")
        _, text = other.post("/radios/cookie", {"guard": other.hidden(text, "guard"),
                                                "radio": str(before["id"]),
                                                "cookie": "bbbb-cccc-dddd"})
        self.assertIn("You have no such radio", text)
        self.assertEqual(self.service.radio_called("vu2ese-home")["cookie"], "pqrstuvwxyza")

    def test_a_radio_is_removed_only_by_its_owner(self):
        first = self.make_account()
        self.add_radio(first)
        radio = self.service.radio_called("vu2ese-home")["id"]
        second = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        _, text = second.get("/")
        second.post("/radios/remove", {"guard": second.hidden(text, "guard"),
                                       "radio": str(radio)})
        self.assertIsNotNone(self.service.radio_called("vu2ese-home"))
        _, text = first.get("/")
        first.post("/radios/remove", {"guard": first.hidden(text, "guard"),
                                      "radio": str(radio)})
        self.assertIsNone(self.service.radio_called("vu2ese-home"))

    def test_adding_a_radio_needs_a_sign_in(self):
        _, text = self.browser.post("/radios/add", {"guard": "x", "suffix": "home",
                                                    "cookie": "abcd-efgh-jkmn"})
        self.assertIn("Sign in first", text)
        self.assertEqual(self.service.radios_of("VU2ESE"), [])

    def test_what_is_kept_of_a_radio_is_shown_to_its_owner_alone(self):
        first = self.make_account()
        self.add_radio(first)
        radio = self.service.radio_called("vu2ese-home")["id"]
        with self.service.store.open() as db:
            db.execute("insert into snapshots (radio, taken, controls, log) values (?, ?, ?, ?)",
                       (radio, accounts.now(), "FREQ 7074000\nMODE USB",
                        "972|2026-04-08|04:45|7130|LSB|VU2XZ|59|59"))
        _, text = first.get("/radios/settings?radio=%d" % radio)
        self.assertIn("<td>FREQ</td><td>7074000</td>", text)
        _, text = first.get("/radios/log?radio=%d" % radio)
        self.assertIn("VU2XZ", text)
        second = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        status, text = second.get("/radios/log?radio=%d" % radio)
        self.assertEqual(status, 404)
        self.assertNotIn("VU2XZ", text)

    def test_the_page_that_works_a_radio_is_for_its_owner(self):
        first = self.make_account()
        self.add_radio(first)
        radio = self.service.radio_called("vu2ese-home")["id"]
        status, text = first.get("/operate?radio=%d" % radio)
        self.assertEqual(status, 200)
        self.assertIn('data-radio="%d" data-name="vu2ese-home"' % radio, text)
        # a telephone can put the page on its home screen, to open on the
        # whole screen and on its side
        self.assertIn('rel="manifest" href="/operate.webmanifest?radio=%d"' % radio, text)
        status, manifest = Browser(self.port).get("/operate.webmanifest?radio=%d" % radio)
        self.assertEqual(status, 200)
        manifest = json.loads(manifest)
        self.assertEqual(manifest["start_url"], "/operate?radio=%d" % radio)
        self.assertEqual((manifest["display"], manifest["orientation"]),
                         ("fullscreen", "landscape"))
        # and it is an application to install, each radio its own: a
        # browser asks for an id, and for a service worker the page may load
        self.assertEqual(manifest["id"], "/operate?radio=%d" % radio)
        status, worker = Browser(self.port).get("/service-worker.js")
        self.assertEqual(status, 200)
        self.assertIn("addEventListener(\"fetch\"", worker)
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20)
        connection.request("GET", "/operate?radio=%d" % radio, headers={"Cookie": first.cookie})
        answer = connection.getresponse()
        answer.read()
        connection.close()
        self.assertIn("worker-src 'self'", answer.getheader("Content-Security-Policy"))
        for icon in manifest["icons"]:
            self.assertEqual(Browser(self.port).get(icon["src"])[0], 200, icon["src"])
        # each file is named with a mark of what is in it, so that a browser
        # holding an older one asks for this one, and the address is served
        marked = re.findall(r'(?:src|href)="(/static/[a-z_.]+\?v=[0-9a-f]{12})"', text)
        self.assertEqual(len(marked), 3, text)
        for address in marked:
            self.assertEqual(first.get(address)[0], 200, address)
        second = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        self.assertEqual(second.get("/operate?radio=%d" % radio)[0], 404)
        _, text = Browser(self.port).get("/operate?radio=%d" % radio)
        self.assertIn("Sign in first", text)
        for path, has in (("/static/operate.js", "WebSocket"), ("/static/operate.css", "#app"),
                          ("/static/sound.js", "radioSound"),
                          ("/static/sound_worklet.js", "registerProcessor")):
            status, text = Browser(self.port).get(path)
            self.assertEqual(status, 200, path)
            self.assertIn(has, text, path)

    # the globe

    def test_the_globe_shows_the_radios_on_line_and_only_their_callsign_and_square(self):
        browser = self.make_account()
        self.add_radio(browser, "home")
        self.add_radio(browser, "attic", "bbbb-cccc-dddd")
        self.add_radio(browser, "car", "cccc-dddd-eeee")
        with self.service.store.open() as db:
            db.execute("update radios set online = 1, grid = 'MK97fj' where suffix = 'home'")
            db.execute("update radios set online = 0, grid = 'JN58td' where suffix = 'attic'")
            db.execute("update radios set online = 1, grid = '' where suffix = 'car'")
        status, text = Browser(self.port).get("/map.json")
        self.assertEqual(status, 200)
        home = self.service.radio_called("vu2ese-home")
        self.assertEqual(json.loads(text), [{"radio": home["id"], "callsign": "VU2ESE",
                                             "grid": "MK97", "lat": 17.5, "lon": 79.0,
                                             "streaming": True, "listening": 0,
                                             "listeners": []}])
        for hidden in ("home", "fj", "abcd", home["client_password"]):
            self.assertNotIn(hidden, text)

    def test_what_a_radio_on_the_globe_copies_in_ft8_is_anyones_to_read(self):
        browser = self.make_account()
        self.add_radio(browser, "home")
        with self.service.store.open() as db:
            db.execute("update radios set online = 1, grid = 'MK97fj' where suffix = 'home'")
        home = self.service.radio_called("vu2ese-home")
        # the newest last, and then what has come since
        kept = decodes.Decodes(self.service.settings.decodes)
        now = int(time.time())
        kept.add([(home["id"], now - 3600, 14074000, "030000", -3, 900, "CQ OLD ONE"),
                  (home["id"], now, 14074000, "043015", -12, 1797, "CQ VU2ESE MK97"),
                  (home["id"], now, 14074000, "043015", None, 1500, "VU2ESE W1AW FN31"),
                  (home["id"] + 77, now, 7074000, "043015", -1, 800, "CQ NOT ON THE GLOBE")])
        status, text = Browser(self.port).get("/decodes.json?radio=%d" % home["id"])
        self.assertEqual(status, 200)
        copied = json.loads(text)
        self.assertEqual([row["text"] for row in copied], ["CQ VU2ESE MK97", "VU2ESE W1AW FN31"])
        self.assertEqual((copied[0]["strength"], copied[0]["pitch"], copied[0]["hz"],
                          copied[0]["sent"]), (-12, 1797, 14074000, "043015"))
        self.assertIsNone(copied[1]["strength"], "one the radio sent itself")
        _, text = Browser(self.port).get("/decodes.json?radio=%d&after=%d" % (home["id"], copied[0]["id"]))
        self.assertEqual([row["text"] for row in json.loads(text)], ["VU2ESE W1AW FN31"])
        for asked in ("/decodes.json?radio=%d" % (home["id"] + 77), "/decodes.json?radio=x",
                      "/decodes.json", "/decodes.json?radio=%d&after=x" % home["id"]):
            self.assertEqual(json.loads(Browser(self.port).get(asked)[1]), [], asked)

    def test_the_globe_and_what_draws_it_need_no_sign_in_and_nothing_from_elsewhere(self):
        visitor = Browser(self.port)
        status, text = visitor.get("/map")
        self.assertEqual(status, 200)
        self.assertIn('<canvas id="globe">', text)
        self.assertIn('<button id="view">flat map</button>', text)
        # the one address of another site on it is a link to follow, the
        # credit DB-IP's licence asks for; nothing is fetched from there
        self.assertIn('<a href="https://db-ip.com"', text)
        self.assertNotIn("http", text.replace("http-equiv", "").replace(
            '<a href="https://db-ip.com"', ""))
        for path, has in (("/map.js", "radioSound"), ("/map.css", "#globe"),
                          ("/static/land.json", "[[[")):
            status, text = visitor.get(path)
            self.assertEqual(status, 200, path)
            self.assertIn(has, text, path)
        rings = json.loads(text)
        self.assertGreater(len(rings), 100)
        self.assertTrue(all(-180 <= lon <= 180 and -90 <= lat <= 90
                            for ring in rings for lon, lat in ring))
        for path in ("/static/../accounts.py", "/static/accounts.db", "/static/",
                     "/static/leaflet.js"):
            self.assertEqual(visitor.get(path)[0], 404, path)

    def test_a_secret_is_stretched_as_the_radio_stretches_it(self):
        # what clients/remote/sha256.c makes of the same, by its own test
        self.assertEqual(accounts.stretch("abcdjklmwxyz", "VU2ESE")[:16], "450331079366c98f")

    # customer service

    def admin(self, *words):
        parser_args = type("Args", (), {})()
        parser_args.command = words[0]
        parser_args.callsign = words[1] if len(words) > 1 else None
        parser_args.value = words[2] if len(words) > 2 else None
        return accounts.admin(self.service.settings, parser_args)

    def test_a_suspended_account_cannot_sign_in_and_its_radios_are_not_found(self):
        browser = self.make_account()
        self.add_radio(browser)
        self.assertEqual(self.admin("suspend", "VU2ESE"), 0)
        _, text = browser.get("/")
        self.assertNotIn("Signed in as", text)
        _, text = Browser(self.port).post("/signin", {"callsign": "VU2ESE",
                                                      "password": PASSWORD})
        self.assertIn("suspended", text)
        self.assertIsNone(self.service.radio_called("vu2ese-home"))
        self.assertEqual(self.admin("restore", "VU2ESE"), 0)
        self.assertIsNotNone(self.service.radio_called("vu2ese-home"))

    def test_a_callsign_is_given_to_another_name_and_the_radio_follows(self):
        browser = self.make_account()
        self.add_radio(browser)
        self.assertEqual(self.admin("rename", "VU2ESE", "VU2ZZZ"), 0)
        self.assertIsNone(self.service.radio_called("vu2ese-home"))
        self.assertIsNotNone(self.service.radio_called("vu2zzz-home"))
        self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        self.assertEqual(self.admin("rename", "VU2ZZZ", "VU2XYZ"), 1)

    def test_a_deleted_account_takes_its_radios_with_it(self):
        browser = self.make_account()
        self.add_radio(browser)
        self.assertEqual(self.admin("delete", "VU2ESE"), 0)
        self.assertIsNone(self.service.radio_called("vu2ese-home"))
        _, text = Browser(self.port).register("VU2ESE", "new@example.org")
        self.assertIn("We have mailed a link", text)


if __name__ == "__main__":
    unittest.main(verbosity=1)
