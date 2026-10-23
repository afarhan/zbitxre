#ifndef JTTY_INTERNAL_H
#define JTTY_INTERNAL_H
#include "jtty.h"
#define JT_PI 3.1415926535897932384626433832795

extern const char        jt_alphabet[];
extern const char *const jt_controls[18];
extern const uint8_t     jt_sync[13];

int      jt_letter(char c);
int      jt_digit(char c);
int      jt_index(const char *s, char c);
int      jt_call_pack(const char *s, uint32_t *v);
int      jt_call_unpack(uint32_t v, char s[14]);
unsigned jt_crc(uint64_t word, int bits);
unsigned jt_trellis_tone(unsigned reg);

/* Scores/correlations for rectangular one-symbol matched filters. */

void   jt_correlate(const float *audio, size_t start, double f0, float scores[4]);
double jt_sync_score(const float *audio, size_t start, double f0);

typedef struct {
    size_t sample;
    double frequency, score;
} jt_candidate;

typedef struct {
    float   obs[46][4], high[46], prev[512], next[512];
    uint8_t trace[46][512], tone[2][512];
} jt_fec;

void jt_fec_init(jt_fec *work);
int  jt_decode(jt_fec *work, const float scores[46][4], uint64_t *payload);

#define JT_HOP           48
#define JT_DF            7.8125
#define JT_ENERGY_ROWS   97
#define JT_EPOCH         3072
#define JT_REFINE_MARGIN 60
#define JT_AUDIO_SIZE    (JTTY_FRAME_SAMPLES + JT_EPOCH + 2 * JT_REFINE_MARGIN)

typedef struct {
    double wr, wi, dr, di, zr, zi, re, im;
} jt_bin;

typedef struct {
    uint64_t sample;
    double   frequency, score;
} jt_pending;

struct jtty_rx {
    jtty_rx_config config;
    jtty_rx_stats  stats;
    uint64_t       epoch;
    size_t         bases, bins, pending_count, recent_count, recent_next, output_count;
    int            finished, busy;
    jt_fec         fec;
    float          audio[JT_AUDIO_SIZE], scratch[JTTY_FRAME_SAMPLES + 2 * JT_REFINE_MARGIN];
    jt_bin        *bank;
    float         *energy;
    jt_pending    *pending;
    jtty_frame    *recent, *output;
};

void jt_sync_init(jtty_rx *rx);
void jt_sync_push(jtty_rx *rx, float sample);
void jt_refine(const float *audio, size_t count, jt_candidate *candidate);

#endif
