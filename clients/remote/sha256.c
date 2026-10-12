// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// sha256.c — SHA-256, and the keyed hash made of it, HMAC-SHA256.
//
// The radio proves who it is to my.sbitx.net by answering a challenge with
// a keyed hash of it; PROTOCOL.md says how. This is the hash. It is here,
// in a hundred lines, so that the program that answers needs no library
// for it: the author asked for as few dependencies as could be.
//
// SHA-256 is as FIPS 180-4 gives it: the message is padded with a one bit,
// zeros and its length in bits to a whole number of 64-byte blocks, and
// each block is worked into eight 32-bit words, sixty-four rounds a block.
// The eight words it starts from and the sixty-four constants of the rounds
// are the standard's, the first 32 bits of the fractional parts of the
// square roots and the cube roots of the first primes.
//
// HMAC is as RFC 2104 gives it: the hash of the key xored with one pad and
// then the hash of the message, all hashed again with the key xored with
// another. A key longer than a block is hashed first.
//
// The message is hashed in one go, from memory, which is all that is needed
// of it here. tests/test_sha256.c checks both against the published test
// vectors.

#include <stdio.h>
#include <string.h>

#include "sha256.h"

#define BLOCK_BYTES 64
#define LENGTH_BYTES 8
#define WORDS 8
#define ROUNDS 64
#define BITS_A_BYTE 8
#define INNER_PAD 0x36
#define OUTER_PAD 0x5c
#define HMAC_MESSAGE_MAX 512

static const uint32_t round_constants[ROUNDS] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
	0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
	0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
	0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
	0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
	0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
	0xc67178f2,
};

static const uint32_t first_words[WORDS] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
	0x5be0cd19,
};

static uint32_t rotate_right(uint32_t word, int bits) {
	return (word >> bits) | (word << (32 - bits));
}

static void hash_a_block(uint32_t words[WORDS], const uint8_t block[BLOCK_BYTES]) {
	uint32_t w[ROUNDS];
	uint32_t v[WORDS];
	uint32_t choose;
	uint32_t majority;
	uint32_t first;
	uint32_t second;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16)
			| ((uint32_t)block[4 * i + 2] << 8) | block[4 * i + 3];
	for (i = 16; i < ROUNDS; i++)
		w[i] = w[i - 16] + w[i - 7]
			+ (rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3))
			+ (rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10));
	memcpy(v, words, sizeof(v));
	for (i = 0; i < ROUNDS; i++) {
		choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
		majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
		first = v[7] + (rotate_right(v[4], 6) ^ rotate_right(v[4], 11)
			^ rotate_right(v[4], 25)) + choose + round_constants[i] + w[i];
		second = (rotate_right(v[0], 2) ^ rotate_right(v[0], 13) ^ rotate_right(v[0], 22))
			+ majority;
		v[7] = v[6];
		v[6] = v[5];
		v[5] = v[4];
		v[4] = v[3] + first;
		v[3] = v[2];
		v[2] = v[1];
		v[1] = v[0];
		v[0] = first + second;
	}
	for (i = 0; i < WORDS; i++)
		words[i] += v[i];
}

void sha256(const uint8_t *data, size_t length, uint8_t hash[SHA256_BYTES]) {
	uint32_t words[WORDS];
	uint8_t last[2 * BLOCK_BYTES];
	uint64_t bits = (uint64_t)length * BITS_A_BYTE;
	size_t whole = length - length % BLOCK_BYTES;
	size_t left = length - whole;
	size_t padded = BLOCK_BYTES;
	size_t at;
	int i;

	memcpy(words, first_words, sizeof(words));
	for (at = 0; at < whole; at += BLOCK_BYTES)
		hash_a_block(words, data + at);

	// what is left over, the one bit, zeros and the length make the last
	// block, or the last two if the length does not fit in the first
	memset(last, 0, sizeof(last));
	if (left > 0)
		memcpy(last, data + whole, left);
	last[left] = 0x80;
	if (left + 1 + LENGTH_BYTES > BLOCK_BYTES)
		padded = 2 * BLOCK_BYTES;
	for (i = 0; i < LENGTH_BYTES; i++)
		last[padded - 1 - i] = (uint8_t)(bits >> (BITS_A_BYTE * i));
	for (at = 0; at < padded; at += BLOCK_BYTES)
		hash_a_block(words, last + at);

	for (i = 0; i < WORDS; i++) {
		hash[4 * i] = (uint8_t)(words[i] >> 24);
		hash[4 * i + 1] = (uint8_t)(words[i] >> 16);
		hash[4 * i + 2] = (uint8_t)(words[i] >> 8);
		hash[4 * i + 3] = (uint8_t)words[i];
	}
}

// a message longer than HMAC_MESSAGE_MAX is cut short there; what is hashed
// here is a line of the protocol, a quarter of that
void hmac_sha256(const uint8_t *key, size_t key_length, const uint8_t *data,
		size_t length, uint8_t hash[SHA256_BYTES]) {
	uint8_t block_key[BLOCK_BYTES];
	uint8_t inner[BLOCK_BYTES + HMAC_MESSAGE_MAX];
	uint8_t outer[BLOCK_BYTES + SHA256_BYTES];
	int i;

	memset(block_key, 0, sizeof(block_key));
	if (key_length > BLOCK_BYTES)
		sha256(key, key_length, block_key);
	else
		memcpy(block_key, key, key_length);
	if (length > HMAC_MESSAGE_MAX)
		length = HMAC_MESSAGE_MAX;
	for (i = 0; i < BLOCK_BYTES; i++) {
		inner[i] = block_key[i] ^ INNER_PAD;
		outer[i] = block_key[i] ^ OUTER_PAD;
	}
	memcpy(inner + BLOCK_BYTES, data, length);
	sha256(inner, BLOCK_BYTES + length, outer + BLOCK_BYTES);
	sha256(outer, sizeof(outer), hash);
}

// the keyed hash of a text with a key that is text, written in hex
void hmac_sha256_hex(const char *key, const char *text, char hex[SHA256_HEX_ROOM]) {
	uint8_t hash[SHA256_BYTES];
	int i;

	hmac_sha256((const uint8_t *)key, strlen(key), (const uint8_t *)text, strlen(text), hash);
	for (i = 0; i < SHA256_BYTES; i++)
		snprintf(hex + 2 * i, 3, "%02x", hash[i]);
}
