#ifndef JTTY_H
#define JTTY_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {

#endif
#define JTTY_MAX_FRAMES    16
#define JTTY_SYMBOLS       59
#define JTTY_SAMPLE_RATE   12000
#define JTTY_SPS           384
#define JTTY_FRAME_SAMPLES (JTTY_SYMBOLS * JTTY_SPS)

/* All routines return 0 on success, negative on error unless documented.
 * Payload is a 34-bit integer, transmitted MSB first; bits 1:0 are reserved/EOM.
 * No global mutable state. Strings are ASCII, NUL terminated. */

enum { JTTY_CALL,
       JTTY_NUM,
       JTTY_LOC,
       JTTY_PAIR,
       JTTY_TIME,
       JTTY_CONTROL,
       JTTY_GRID,
       JTTY_TEXT5
};

enum { JTTY_UNKNOWN,
       JTTY_FIELD_DAY,
       JTTY_RTTY_ROUNDUP
};

/* Wire subtype assignments for the typed atom interface. */

enum { JTTY_CALL_CQ,
       JTTY_CALL_BARE,
       JTTY_CALL_TU_CQ,
       JTTY_CALL_TU,
       JTTY_CALL_AGN,
       JTTY_CALL_TU_NOW
};

enum { JTTY_FIELD_ONLY,
       JTTY_FULL_EXCHANGE
};

enum { JTTY_SERIAL,
       JTTY_CQ_ZONE,
       JTTY_ITU_ZONE,
       JTTY_AGE,
       JTTY_POWER,
       JTTY_CHECK,
       JTTY_LICENSE_YEAR,
       JTTY_GENERIC_NUMBER
};

enum { JTTY_STATE_PROVINCE,
       JTTY_SECTION,
       JTTY_COUNTRY_PREFIX,
       JTTY_GENERIC_QTH,
       JTTY_ADMIN_CODE
};

enum { JTTY_ZONE_LOC,
       JTTY_CLASS_SECTION
};

enum { JTTY_AGN,
       JTTY_CALL_QUERY,
       JTTY_AGN_CALL,
       JTTY_NR_QUERY,
       JTTY_AGN_NR,
       JTTY_EXCH_QUERY,
       JTTY_STATE_QUERY,
       JTTY_SECTION_QUERY,
       JTTY_ZONE_QUERY,
       JTTY_GRID_QUERY,
       JTTY_RPRT_QUERY,
       JTTY_QSL_TU,
       JTTY_TU,
       JTTY_QRZ_QUERY,
       JTTY_QSO_B4,
       JTTY_WAIT,
       JTTY_NIL_QUERY,
       JTTY_OK_QUERY
};

/* Zero-initialize atoms; fields not used by a kind are ignored. See README
 * for the mapping of kind/subtype/role/value/value2/text. */

typedef struct {
    int  kind, subtype, role, value, value2;
    char text[14];
} jtty_atom;

int jtty_atom_pack(const jtty_atom *a, int eom, uint64_t *payload);
int jtty_atom_unpack(uint64_t payload, jtty_atom *a, int *eom);
int jtty_atom_render(const jtty_atom *a, char text[81]);

/* Literal packing, minimum frames. Input longer than 80 bytes is truncated
 * before normalization, as in WSJT-X. Returns frame count or negative error. */

int jtty_pack(const char *text, int profile, uint64_t frames[16], char normalized[81]);
int jtty_unpack(const uint64_t *frames, size_t count, char text[81]);

/* Encode includes 13 sync tones; FEC decoder accepts 46x4 scores, larger=better.
 * Decode verifies CRC, tail biting, reserved bit and source grammar. */

int jtty_encode(uint64_t payload, uint8_t tones[59]);
int jtty_decode(const float scores[46][4], uint64_t *payload);

/* 12 kHz real audio, BT=2, lowest tone f0; capacity >= count*384. */

int jtty_modulate(const uint8_t *tones, size_t count, double f0, float *audio, size_t capacity);

typedef struct {
    uint64_t payload;
    double   time, frequency, sync;
    char     text[81];
    int      eom;
} jtty_frame;

typedef void (*jtty_frame_callback)(const jtty_frame *, void *);

/* Stateful streaming receiver. Real mono float audio at 12 kHz.
 * create performs one allocation; process/flush/reset do not allocate.
 * Each context is single-threaded and callbacks must not re-enter it. */

typedef struct jtty_rx jtty_rx;

typedef struct {
    double fmin, fmax;         /* Lowest-tone search range. */
    size_t candidate_capacity; /* Maximum pending acquisition hypotheses. */
    size_t decode_budget;      /* Attempts per 3072 input samples (256 ms). */
} jtty_rx_config;

typedef struct {
    uint64_t samples, candidates_dropped, budget_dropped, decode_attempts, frames;
    size_t   pending_candidates, memory_bytes;
} jtty_rx_stats;

/* Both capacities must be positive, decode_budget <= candidate_capacity. */

jtty_rx *jtty_rx_create(const jtty_rx_config *config);

/* Return delivered frame count, or -1 on invalid input/state. Invalid input
 * leaves state unchanged. NULL audio is allowed only when count == 0.
 * Input is consumed synchronously; count may be 0 through INT_MAX. */

int jtty_rx_process(jtty_rx *rx, const float *audio, size_t count, jtty_frame_callback callback, void *user);

/* Finish complete frames, discard incomplete ones; no zero padding.
 * Idempotent. Call reset before supplying another stream after flush. */

int  jtty_rx_flush(jtty_rx *rx, jtty_frame_callback callback, void *user);
void jtty_rx_reset(jtty_rx *rx);
void jtty_rx_destroy(jtty_rx *rx);
int  jtty_rx_get_stats(const jtty_rx *rx, jtty_rx_stats *stats);

const char *jtty_section(int index); /* 1..86, NULL for invalid index */

#ifdef __cplusplus
}
#endif
#endif
