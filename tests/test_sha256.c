// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// test_sha256.c — clients/remote/sha256.c against the published vectors:
// SHA-256's from FIPS 180-4 and NIST's examples, HMAC's from RFC 4231.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"

#define LONG_KEY_BYTES 131
#define MILLION 1000000

static int checks_failed;

static void expect(int ok, const char *what) {
	if (ok) {
		printf("  ok      %s\n", what);
		return;
	}
	printf("  FAILED  %s\n", what);
	checks_failed++;
}

static void to_hex(const uint8_t hash[SHA256_BYTES], char hex[SHA256_HEX_ROOM]) {
	int i;

	for (i = 0; i < SHA256_BYTES; i++)
		snprintf(hex + 2 * i, 3, "%02x", hash[i]);
}

static int hashes_to(const char *text, const char *expected) {
	uint8_t hash[SHA256_BYTES];
	char hex[SHA256_HEX_ROOM];

	sha256((const uint8_t *)text, strlen(text), hash);
	to_hex(hash, hex);
	return !strcmp(hex, expected);
}

int main(void) {
	static char a_million[MILLION + 1];
	uint8_t key[LONG_KEY_BYTES];
	uint8_t hash[SHA256_BYTES];
	char hex[SHA256_HEX_ROOM];
	const char *long_key_text = "Test Using Larger Than Block-Size Key - Hash Key First";

	printf("sha256:\n");
	expect(hashes_to("",
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
		"nothing at all");
	expect(hashes_to("abc",
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
		"abc");
	expect(hashes_to("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
		"56 bytes, whose padding takes a second block");
	memset(a_million, 'a', MILLION);
	expect(hashes_to(a_million,
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
		"a million times the letter a");

	memset(key, 0x0b, 20);
	hmac_sha256(key, 20, (const uint8_t *)"Hi There", 8, hash);
	to_hex(hash, hex);
	expect(!strcmp(hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
		"HMAC, RFC 4231's first case");
	hmac_sha256_hex("Jefe", "what do ya want for nothing?", hex);
	expect(!strcmp(hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
		"HMAC with a key and a message that are text, its second case");
	memset(key, 0xaa, LONG_KEY_BYTES);
	hmac_sha256(key, LONG_KEY_BYTES, (const uint8_t *)long_key_text, strlen(long_key_text),
		hash);
	to_hex(hash, hex);
	expect(!strcmp(hex, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"),
		"HMAC with a key longer than a block, its sixth case");

	if (checks_failed) {
		printf("sha256: %d checks FAILED\n", checks_failed);
		return EXIT_FAILURE;
	}
	printf("sha256: all checks passed\n");
	return EXIT_SUCCESS;
}
