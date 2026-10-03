// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// ft8_callsign_hash.c — the callsigns heard, by their hashes.
//
// A callsign that does not fit FT8's pattern, PJ4/K1ABC, VU2ESE/QRP, is sent
// in full in some messages and as a hash in others, and the hash can be
// turned back into the callsign only by one who has seen the callsign;
// the design at the top of modem_ft8.c says more. ft8_lib asks the program
// for that, through ftx_callsign_hash_interface_t: save_hash() with every
// callsign it unpacks or packs in full, and lookup_hash() with a hash of
// 22, 12 or 10 bits that it has to show as a callsign. This is that table.
//
// It is the table of the library's example, decode_ft8.c, with its names:
// 256 callsigns, each with its 22 bit hash, the shorter hashes being its
// leading bits; and an age, the slots since it was last heard, in the top
// byte. The example forgot a callsign after ten slots unheard; here it is
// kept for MAX_AGE, an hour of slots, since a station worked a while ago
// may come back.
//
// The table is in memory and starts empty. Keeping it in the logbook's
// database, with the callsigns of the logbook and of ARRL's LoTW list, is
// the next step of the design, and is not here yet.
//
// The decoder's thread looks up and saves as it unpacks, and the thread
// that owns the controls saves as a message is packed to be sent, so a
// lock is held for each.

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ft8_lib/message.h"
#include "ft8_callsign_hash.h"

#define CALLSIGN_HASHTABLE_SIZE 256
#define CALLSIGN_ROOM 12
#define HASH22_MASK 0x3FFFFFu
#define HASH10_MASK 0x3FFu
#define AGE_SHIFT 24
#define HASH_SPREAD 23
#define SHIFT_FOR_10 12
#define SHIFT_FOR_12 10
#define MAX_AGE 240

static struct
{
	char callsign[CALLSIGN_ROOM]; ///> Up to 11 symbols of callsign + trailing zeros (always filled)
	uint32_t hash;     ///> 8 MSBs contain the age of callsign; 22 LSBs contain hash value
} callsign_hashtable[CALLSIGN_HASHTABLE_SIZE];

static int callsign_hashtable_size;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void hashtable_add(const char* callsign, uint32_t hash)
{
	uint16_t hash10 = (hash >> SHIFT_FOR_10) & HASH10_MASK;
	int idx_hash = (hash10 * HASH_SPREAD) % CALLSIGN_HASHTABLE_SIZE;

	pthread_mutex_lock(&lock);
	while (callsign_hashtable[idx_hash].callsign[0] != '\0')
	{
		if (((callsign_hashtable[idx_hash].hash & HASH22_MASK) == hash) && (0 == strcmp(callsign_hashtable[idx_hash].callsign, callsign)))
		{
			// reset age
			callsign_hashtable[idx_hash].hash &= HASH22_MASK;
			pthread_mutex_unlock(&lock);
			return;
		}
		// Move on to check the next entry in hash table
		idx_hash = (idx_hash + 1) % CALLSIGN_HASHTABLE_SIZE;
	}
	// a full table takes nothing more until callsigns are forgotten
	if (callsign_hashtable_size < CALLSIGN_HASHTABLE_SIZE - 1)
	{
		callsign_hashtable_size++;
		strncpy(callsign_hashtable[idx_hash].callsign, callsign, CALLSIGN_ROOM - 1);
		callsign_hashtable[idx_hash].callsign[CALLSIGN_ROOM - 1] = '\0';
		callsign_hashtable[idx_hash].hash = hash;
	}
	pthread_mutex_unlock(&lock);
}

static bool hashtable_lookup(ftx_callsign_hash_type_t hash_type, uint32_t hash, char* callsign)
{
	uint8_t hash_shift = 0;
	uint16_t hash10;
	int idx_hash;

	if (hash_type == FTX_CALLSIGN_HASH_10_BITS)
		hash_shift = SHIFT_FOR_10;
	if (hash_type == FTX_CALLSIGN_HASH_12_BITS)
		hash_shift = SHIFT_FOR_12;
	hash10 = (hash >> (SHIFT_FOR_10 - hash_shift)) & HASH10_MASK;
	idx_hash = (hash10 * HASH_SPREAD) % CALLSIGN_HASHTABLE_SIZE;
	pthread_mutex_lock(&lock);
	while (callsign_hashtable[idx_hash].callsign[0] != '\0')
	{
		if (((callsign_hashtable[idx_hash].hash & HASH22_MASK) >> hash_shift) == hash)
		{
			strcpy(callsign, callsign_hashtable[idx_hash].callsign);
			pthread_mutex_unlock(&lock);
			return true;
		}
		// Move on to check the next entry in hash table
		idx_hash = (idx_hash + 1) % CALLSIGN_HASHTABLE_SIZE;
	}
	pthread_mutex_unlock(&lock);
	callsign[0] = '\0';
	return false;
}

static ftx_callsign_hash_interface_t hash_if = {
	.lookup_hash = hashtable_lookup,
	.save_hash = hashtable_add,
};

ftx_callsign_hash_interface_t *ft8_callsign_hashes(void) {
	return &hash_if;
}

// a callsign is put in the table, with its hash, as the library would put
// it there on hearing it: ours, so that a message to us sent with our hash
// is known to be to us
void ft8_callsign_remember(const char *callsign) {
	ftx_message_t message;
	char text[FTX_MAX_MESSAGE_LENGTH];

	// packing a message with the callsign in full puts it in the table
	snprintf(text, sizeof(text), "CQ %s", callsign);
	ftx_message_encode(&message, &hash_if, text);
}

// after each slot: every callsign is a slot older, and one not heard for
// MAX_AGE slots is forgotten. This is the example's hashtable_cleanup()
void ft8_callsign_age(void) {
	uint8_t age;

	pthread_mutex_lock(&lock);
	for (int idx_hash = 0; idx_hash < CALLSIGN_HASHTABLE_SIZE; ++idx_hash)
	{
		if (callsign_hashtable[idx_hash].callsign[0] == '\0')
			continue;
		age = (uint8_t)(callsign_hashtable[idx_hash].hash >> AGE_SHIFT);
		if (age > MAX_AGE)
		{
			// free the hash entry
			callsign_hashtable[idx_hash].callsign[0] = '\0';
			callsign_hashtable[idx_hash].hash = 0;
			callsign_hashtable_size--;
		}
		else
		{
			// increase callsign age
			callsign_hashtable[idx_hash].hash = (((uint32_t)age + 1u) << AGE_SHIFT) | (callsign_hashtable[idx_hash].hash & HASH22_MASK);
		}
	}
	pthread_mutex_unlock(&lock);
}
