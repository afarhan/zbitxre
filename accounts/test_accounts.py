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
import json
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
        text = answer.read().decode()
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

    def radio_ask(self, path, asked):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20)
        connection.request("POST", path, body=json.dumps(asked),
                           headers={"Content-Type": "application/json"})
        answer = connection.getresponse()
        got = json.loads(answer.read().decode())
        connection.close()
        return answer.status, got

    def whoami(self, key):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20)
        connection.request("GET", "/api/radio/whoami",
                           headers={"Authorization": "Bearer " + key})
        answer = connection.getresponse()
        got = json.loads(answer.read().decode())
        connection.close()
        return answer.status, got

    def link_radio(self, browser, name="shack sbitx"):
        _, started = self.radio_ask("/api/radio/link", {"name": name})
        _, text = browser.get("/radios/link")
        status, text = browser.post("/radios/link", {
            "guard": browser.hidden(text, "guard"), "code": started["user_code"]})
        self.assertIn("is linked", text)
        _, polled = self.radio_ask("/api/radio/link/poll",
                                   {"device_code": started["device_code"]})
        return polled

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

    def test_a_radio_is_linked_and_its_key_says_whose_it_is(self):
        browser = self.make_account()
        _, started = self.radio_ask("/api/radio/link", {"name": "shack sbitx"})
        self.assertRegex(started["user_code"], r"^[A-Z]{4}-[A-Z]{4}$")
        _, polled = self.radio_ask("/api/radio/link/poll",
                                   {"device_code": started["device_code"]})
        self.assertEqual(polled, {"status": "pending"})
        _, text = browser.get("/radios/link")
        _, text = browser.post("/radios/link", {
            "guard": browser.hidden(text, "guard"),
            "code": started["user_code"].lower().replace("-", " ")})
        self.assertIn("shack sbitx", text)
        _, polled = self.radio_ask("/api/radio/link/poll",
                                   {"device_code": started["device_code"]})
        self.assertEqual(polled["status"], "linked")
        self.assertEqual(polled["callsign"], "VU2ESE")
        status, who = self.whoami(polled["key"])
        self.assertEqual((status, who), (200, {"callsign": "VU2ESE", "radio": "shack sbitx"}))
        _, again = self.radio_ask("/api/radio/link/poll",
                                  {"device_code": started["device_code"]})
        self.assertEqual(again, {"status": "expired"})

    def test_the_key_is_not_kept(self):
        key = self.link_radio(self.make_account())["key"]
        with open(os.path.join(self.folder, "accounts.db"), "rb") as file:
            self.assertNotIn(key.encode(), file.read())

    def test_a_wrong_code_links_nothing(self):
        browser = self.make_account()
        _, text = browser.get("/radios/link")
        _, text = browser.post("/radios/link", {"guard": browser.hidden(text, "guard"),
                                                "code": "BCDF-GHJK"})
        self.assertIn("No radio is showing that code", text)

    def test_linking_needs_a_sign_in(self):
        _, started = self.radio_ask("/api/radio/link", {"name": "x"})
        _, text = self.browser.post("/radios/link", {"guard": "x",
                                                     "code": started["user_code"]})
        self.assertIn("Sign in first", text)
        _, polled = self.radio_ask("/api/radio/link/poll",
                                   {"device_code": started["device_code"]})
        self.assertEqual(polled, {"status": "pending"})

    def test_a_removed_radios_key_is_refused(self):
        browser = self.make_account()
        key = self.link_radio(browser)["key"]
        _, text = browser.get("/")
        browser.post("/radios/remove", {"guard": browser.hidden(text, "guard"),
                                        "radio": browser.hidden(text, "radio")})
        status, _ = self.whoami(key)
        self.assertEqual(status, 401)

    def test_one_owner_cannot_remove_anothers_radio(self):
        first = self.make_account()
        key = self.link_radio(first)["key"]
        _, text = first.get("/")
        radio = first.hidden(text, "radio")
        second = self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        _, text = second.get("/")
        second.post("/radios/remove", {"guard": second.hidden(text, "guard"), "radio": radio})
        self.assertEqual(self.whoami(key)[0], 200)

    def test_a_key_of_no_radio_is_refused(self):
        self.assertEqual(self.whoami("sbx_" + "a" * 43)[0], 401)
        self.assertEqual(self.whoami("nonsense")[0], 401)

    # customer service

    def admin(self, *words):
        parser_args = type("Args", (), {})()
        parser_args.command = words[0]
        parser_args.callsign = words[1] if len(words) > 1 else None
        parser_args.value = words[2] if len(words) > 2 else None
        return accounts.admin(self.service.settings, parser_args)

    def test_a_suspended_account_cannot_sign_in_and_its_radio_is_refused(self):
        browser = self.make_account()
        key = self.link_radio(browser)["key"]
        self.assertEqual(self.admin("suspend", "VU2ESE"), 0)
        _, text = browser.get("/")
        self.assertNotIn("Signed in as", text)
        _, text = Browser(self.port).post("/signin", {"callsign": "VU2ESE",
                                                      "password": PASSWORD})
        self.assertIn("suspended", text)
        self.assertEqual(self.whoami(key)[0], 401)
        self.assertEqual(self.admin("restore", "VU2ESE"), 0)
        self.assertEqual(self.whoami(key)[0], 200)

    def test_a_callsign_is_given_to_another_name_and_the_radio_follows(self):
        browser = self.make_account()
        key = self.link_radio(browser)["key"]
        self.assertEqual(self.admin("rename", "VU2ESE", "VU2ZZZ"), 0)
        self.assertEqual(self.whoami(key)[1]["callsign"], "VU2ZZZ")
        self.make_account("VU2XYZ", "xyz@example.org", Browser(self.port))
        self.assertEqual(self.admin("rename", "VU2ZZZ", "VU2XYZ"), 1)

    def test_a_deleted_account_takes_its_radios_with_it(self):
        browser = self.make_account()
        key = self.link_radio(browser)["key"]
        self.assertEqual(self.admin("delete", "VU2ESE"), 0)
        self.assertEqual(self.whoami(key)[0], 401)
        _, text = Browser(self.port).register("VU2ESE", "new@example.org")
        self.assertIn("We have mailed a link", text)


if __name__ == "__main__":
    unittest.main(verbosity=1)
