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
TICKED = {name: "on" for name, _ in accounts.TICKS}
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

    def register(self, callsign, email, invite=INVITE, radio=None):
        """radio, a dict of what the form says of it: suffix, cookie, and the
        ticks"""
        _, text = self.get("/register")
        challenge = self.hidden(text, "pow_challenge")
        form = {
            "callsign": callsign, "email": email, "invite": invite,
            "pow_challenge": challenge,
            "pow_signature": self.hidden(text, "pow_signature"),
            "pow_answer": solve(challenge, 8),
        }
        form.update(radio or {})
        return self.post("/register", form)


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

    def add_radio(self, browser, suffix="home", cookie="abcd-efgh-jkmn", ticked=True):
        _, text = browser.get("/")
        form = {"guard": browser.hidden(text, "guard"), "suffix": suffix, "cookie": cookie}
        form.update(TICKED if ticked else {})
        return browser.post("/radios/add", form)[1]

    def follow_link(self, browser):
        return browser.post("/password", {"t": self.mail.last_link(),
                                          "password": PASSWORD, "again": PASSWORD})[1]

    # registering with the radio, on one form

    def test_the_registration_form_says_why_and_adds_the_radio(self):
        _, text = self.browser.get("/register")
        self.assertIn("Put your radio on line", text)
        self.assertIn("on the map", text)
        self.assertIn("QSOs with others", text)
        for name, _ in accounts.TICKS:
            self.assertRegex(text, r'name="%s" required' % name)
        self.assertIn("Callsign is set to your callsign entered above", text)
        radio = dict(TICKED, suffix="Home", cookie="abcd-efgh-jkmn")
        _, text = self.browser.register("VU2ESE", "farhan@example.org", radio=radio)
        self.assertIn("your radio is added", text)
        # nothing is added until the address is confirmed
        self.assertEqual(self.service.radios_of("VU2ESE"), [])
        text = self.follow_link(self.browser)
        self.assertIn("Signed in as <strong>VU2ESE</strong>", text)
        self.assertIn("vu2ese-home", text)
        self.assertEqual(self.service.radio_called("vu2ese-home")["cookie"], "abcdefghjkmn")

    def test_a_radio_just_added_is_waited_for(self):
        browser = self.make_account()
        text = self.add_radio(browser)
        self.assertIn("Waiting for vu2ese-home to come on line", text)
        self.assertIn('http-equiv="refresh" content="5; url=/"', text)
        self.assertNotIn("None of your radios is on line", text)
        # once it is on line nothing is waited for
        with self.service.store.open() as db:
            db.execute("update radios set online = 1")
        _, text = browser.get("/")
        self.assertNotIn("Waiting for", text)
        self.assertNotIn("http-equiv", text)
        # and one that has not come in two minutes is shown what to check
        with self.service.store.open() as db:
            db.execute("update radios set online = 0, created = created - 600")
        _, text = browser.get("/")
        self.assertIn("None of your radios is on line", text)
        self.assertIn("MYCALLSIGN should be VU2ESE", text)
        self.assertNotIn("http-equiv", text)

    def test_the_boxes_must_be_ticked(self):
        radio = dict(suffix="home", cookie="abcd-efgh-jkmn", tick_remote="on")
        _, text = self.browser.register("VU2ESE", "farhan@example.org", radio=radio)
        self.assertIn(accounts.NOT_TICKED, text)
        self.assertIn("value=\"VU2ESE\"", text)
        self.assertEqual(self.mail.sent, [])
        browser = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        _, text = browser.get("/radios/add")
        self.assertIn("Callsign is set to <strong>VU2XYZ</strong>", text)
        self.assertIn(accounts.NOT_TICKED, self.add_radio(browser, ticked=False))
        self.assertEqual(self.service.radios_of("VU2XYZ"), [])

    def test_a_key_in_use_is_refused_and_a_new_one_asked_for(self):
        first = self.make_account()
        self.add_radio(first, "home", "abcd-efgh-jkmn")
        # at Add Radio, by another account
        other = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        text = self.add_radio(other, "home", "ABCD EFGH JKMN")
        self.assertIn("already in use", text)
        self.assertIn("NEW KEY", text)
        self.assertNotIn("VU2ESE", text.split("</nav>")[1])
        # by the same account, under another name
        self.assertIn("Your radio vu2ese-home has that key already",
                      self.add_radio(first, "shack", "abcd-efgh-jkmn"))
        # as a new key on the Manage page
        self.add_radio(other, "home", "pqrs-tuvw-xyza")
        radio = self.service.radio_called("vu2xyz-home")
        _, page = other.get("/radios/manage?radio=%d" % radio["id"])
        _, text = other.post("/radios/cookie", {"guard": other.hidden(page, "guard"),
                                                "radio": radio["id"],
                                                "cookie": "abcd-efgh-jkmn"})
        self.assertIn("already in use", text)
        self.assertEqual(self.service.radio_called("vu2xyz-home")["cookie"], "pqrstuvwxyza")
        # on the registration form, before anything is mailed
        sent = len(self.mail.sent)
        radio = dict(TICKED, suffix="home", cookie="abcd-efgh-jkmn")
        _, text = Browser(self.port).register("VU3AAA", "a@example.org", radio=radio)
        self.assertIn("already in use", text)
        self.assertEqual(len(self.mail.sent), sent)

    def test_a_key_taken_before_the_link_is_followed_is_not_added(self):
        browser = Browser(self.port)
        radio = dict(TICKED, suffix="home", cookie="abcd-efgh-jkmn")
        browser.register("VU3AAA", "a@example.org", radio=radio)
        link = self.mail.last_link()
        self.add_radio(self.make_account(), "home", "abcd-efgh-jkmn")
        _, text = browser.post("/password", {"t": link, "password": PASSWORD,
                                             "again": PASSWORD})
        self.assertIn("Signed in as <strong>VU3AAA</strong>", text)
        self.assertIn("already in use", text)
        self.assertIn("href=\"/radios/add\"", text)
        self.assertEqual(self.service.radios_of("VU3AAA"), [])

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
        for path in ("/", "/map", "/help", "/upgrade", "/signin", "/register", "/no-such-page"):
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

    def test_upgrade_page_gives_the_whole_line_and_the_panel_firmware(self):
        # what went wrong for the first tester, on 2026-10-17: the line typed
        # in part, a radio off the Wi-Fi, a clock at 2000 that refused the
        # certificate, and the panel's firmware wanted by the USB cable
        status, text = Browser(self.port).get("/upgrade")
        self.assertEqual(status, 200)
        self.assertIn("curl -fsSL http://127.0.0.1:%d/install.sh | bash" % self.port, text)
        self.assertIn("capital S", text)
        self.assertIn("ping -c 3 my.sbitx.net", text)
        self.assertNotIn("ssh", text.split("</nav>")[1])
        # a clock that says 2000 refuses the certificate: today, to type
        self.assertIn("sudo date -u -s \"%s" % time.strftime("%Y-%m-%d", time.gmtime()), text)
        self.assertIn("href=\"https://raw.githubusercontent.com/afarhan/zbitxre/main/"
                      "release/panel/front_panel.uf2\"", text)
        # the short line's script only fetches the installer from GitHub and
        # runs it, and no copies of the installer or the firmware are here
        status, pointer = Browser(self.port).get("/install.sh")
        self.assertEqual(status, 200)
        self.assertTrue(pointer.startswith("#!/bin/bash"))
        self.assertIn("https://raw.githubusercontent.com/afarhan/zbitxre/main/setup/install.sh",
                      pointer)
        self.assertIn("exec bash /tmp/zbitxre_install.sh", pointer)
        for path in ("/install", "/zbitx/front_panel.uf2"):
            self.assertEqual(Browser(self.port).get(path)[0], 404)
        # the knob held as the radio is switched on, and the cable after
        self.assertLess(text.index("switch the radio <strong>on</strong>"),
                        text.index("Connect the panel's USB socket"))
        self.assertIn("/static/upgrade.js?v=", text)
        self.assertIn("href=\"/upgrade\">Upgrade</a>", text)
        # the script it names is served, from this site, as a script must be
        address = text.split("src=\"")[-1].split("\"")[0]
        status, script = Browser(self.port).get(address)
        self.assertEqual(status, 200)
        self.assertIn("clipboard", script)

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

    def test_a_radio_is_added_with_its_cookie(self):
        browser = self.make_account()
        text = self.add_radio(browser, "Home", "ABCD EFGH JKMN")
        self.assertIn("vu2ese-home is added", text)
        radio = self.service.radio_called("VU2ESE-home")
        self.assertEqual(radio["cookie"], "abcdefghjkmn")
        # there is no client password any more: the owner's programs sign
        # in with the account's password, which the Keys page says
        self.assertEqual(radio["client_password"], "")
        self.assertIn("class=\"card\"", text)
        # its Manage page has the new cookie and Remove, and nothing of
        # passwords; the old Keys address still reaches it
        _, keys = browser.get("/radios/manage?radio=%d" % radio["id"])
        self.assertIn("Manage vu2ese-home", keys)
        self.assertIn("action=\"/radios/cookie\"", keys)
        self.assertIn("action=\"/radios/remove\"", keys)
        self.assertNotIn("password", keys.split("</nav>")[1].lower())
        self.assertIn("Manage vu2ese-home", browser.get("/radios/keys?radio=%d" % radio["id"])[1])
        # how the window on another computer signs in is on Help
        _, helped = Browser(self.port).get("/help")
        self.assertIn("zbitx_gui remote vu2ese-home", helped)
        # keys are shown in small letters, as the radio shows them
        self.assertNotIn("uppercase", accounts.STYLE)
        # and a cookie typed in small letters, as the radio shows it, is taken
        self.assertIn("is added", self.add_radio(browser, "shack", "pqrs-tuvw-xyza"))
        for page in (text, keys):
            self.assertNotIn("abcd-efgh-jkmn", page)

    def test_a_new_owner_is_asked_to_add_a_radio_and_shown_how(self):
        browser = self.make_account()
        _, text = browser.get("/")
        self.assertIn("Three steps to operate your radio", text)
        self.assertIn("<a class=\"action\" href=\"/radios/add\">Add Radio</a>", text)
        _, text = browser.get("/radios/add")
        self.assertIn("Callsign is set to <strong>VU2ESE</strong>", text)
        self.assertIn("action=\"/radios/add\"", text)
        # a mistake is shown on the same page, with the name kept
        text = self.add_radio(browser, "shack", "abcd")
        self.assertIn("That is not a radio key", text)
        self.assertIn("value=\"shack\"", text)

    def test_keys_are_only_the_owners(self):
        first = self.make_account()
        self.add_radio(first)
        radio = self.service.radio_called("vu2ese-home")
        second = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        status, text = second.get("/radios/keys?radio=%d" % radio["id"])
        self.assertEqual(status, 404)
        self.assertNotIn("Manage vu2ese-home", text)
        status, text = Browser(self.port).get("/radios/keys?radio=%d" % radio["id"])
        self.assertNotIn("Manage vu2ese-home", text)

    def test_a_sign_in_is_remembered_only_when_asked(self):
        self.make_account()
        for form, kept in (({}, False), ({"remember": "on"}, True)):
            form.update({"callsign": "VU2ESE", "password": PASSWORD})
            connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20)
            connection.request("POST", "/signin", body=urllib.parse.urlencode(form), headers={
                "Content-Type": "application/x-www-form-urlencoded",
                "Origin": self.browser.origin})
            answer = connection.getresponse()
            answer.read()
            connection.close()
            self.assertEqual(answer.status, 303)
            self.assertEqual("Max-Age=" in answer.getheader("Set-Cookie"), kept, form)

    def test_what_is_not_a_cookie_or_a_name_is_refused(self):
        browser = self.make_account()
        self.assertIn("That is not a radio key", self.add_radio(browser, "home", "abcd-efgh"))
        self.assertIn("That is not a radio key", self.add_radio(browser, "home", "1234-5678-9012"))
        self.assertIn("up to ten letters and figures",
                      self.add_radio(browser, "my home", "abcd-efgh-jkmn"))
        self.assertEqual(self.service.radios_of("VU2ESE"), [])

    def test_a_name_is_used_once_and_there_is_room_for_five(self):
        browser = self.make_account()
        # each a radio of its own, with a key of its own
        for suffix in ("a", "b", "c", "d", "e"):
            self.assertIn("is added", self.add_radio(browser, suffix, "abcd-efgh-jkm" + suffix))
        self.assertIn("room for 5 radios", self.add_radio(browser, "f", "abcd-efgh-jkmf"))
        self.assertEqual(len(self.service.radios_of("VU2ESE")), 5)
        other = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        self.add_radio(other, "home", "pqrs-tuvw-xyza")
        self.assertIn("called home already", self.add_radio(other, "home", "pqrs-tuvw-xyzb"))

    def test_the_accounts_client_key_follows_its_password(self):
        """what the owner's programs sign in with is the account's password
        stretched, kept when the password is chosen, and when it is signed
        in with, for an account from before there was one"""
        self.make_account()
        with self.service.store.open() as db:
            kept = db.execute("select client_key, password from accounts").fetchone()
        self.assertEqual(kept["client_key"], accounts.client_key_of(PASSWORD, "VU2ESE"))
        self.assertNotIn(PASSWORD, kept["client_key"])
        with self.service.store.open() as db:
            db.execute("update accounts set client_key = ''")
        Browser(self.port).post("/signin", {"callsign": "VU2ESE", "password": PASSWORD})
        with self.service.store.open() as db:
            self.assertEqual(db.execute("select client_key from accounts").fetchone()[0],
                             accounts.client_key_of(PASSWORD, "VU2ESE"))
        # and there is no page that makes a client password
        browser = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        _, text = browser.get("/")
        status, _ = browser.post("/radios/password", {"guard": browser.hidden(text, "guard"),
                                                      "radio": "1"})
        self.assertEqual(status, 404)

    def test_a_new_cookie_replaces_the_old_and_keeps_the_name_and_password(self):
        browser = self.make_account()
        self.add_radio(browser, "home", "abcd-efgh-jkmn")
        before = self.service.radio_called("vu2ese-home")
        _, text = browser.get("/")
        guard = browser.hidden(text, "guard")
        _, text = browser.post("/radios/cookie", {"guard": guard, "radio": str(before["id"]),
                                                  "cookie": "PQRS-TUVW-XYZA"})
        self.assertIn("has its new radio key", text)
        after = self.service.radio_called("vu2ese-home")
        self.assertEqual(after["cookie"], "pqrstuvwxyza")
        self.assertEqual(after["id"], before["id"])
        _, text = browser.post("/radios/cookie", {"guard": guard, "radio": str(before["id"]),
                                                  "cookie": "pqrs-tuvw-xyza"})
        self.assertIn("That is the key it has", text)
        _, text = browser.post("/radios/cookie", {"guard": guard, "radio": str(before["id"]),
                                                  "cookie": "pqrs"})
        self.assertIn("That is not a radio key", text)
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
        # and the page's UPDATE is told the same files, with the same marks
        status, listed = first.get("/operate.json")
        self.assertEqual(status, 200)
        self.assertEqual(sorted(json.loads(listed)), sorted(marked))
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
        for hidden in ("home", "fj", "abcd", home["cookie"]):
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
