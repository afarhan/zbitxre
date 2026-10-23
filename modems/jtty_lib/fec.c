#include "internal.h"
#include <math.h>
#include <string.h>

const uint8_t jt_sync[13] = { 0, 2, 2, 3, 0, 0, 3, 2, 1, 3, 1, 2, 0 };

static unsigned parity(unsigned x) {
    x ^= x >> 8;
    x ^= x >> 4;
    return (0x6996u >> (x & 15)) & 1;
}

unsigned jt_trellis_tone(unsigned reg) {
    unsigned              pair = (parity(reg & 01167) << 1) | parity(reg & 01545);
    static const unsigned gray[4] = { 0, 1, 3, 2 };

    return gray[pair];
}

int jtty_encode(uint64_t payload, uint8_t tones[59]) {
    if (!tones || payload >> 34)
        return -1;

    uint64_t info = (payload << 12) | jt_crc(payload, 34);
    unsigned state = (unsigned) info & 511;

    memcpy(tones, jt_sync, 13);

    for (int i = 0; i < 46; i++) {
        unsigned reg = (state << 1) | ((unsigned) (info >> (45 - i)) & 1);
        tones[13 + i] = (uint8_t) jt_trellis_tone(reg);
        state = reg & 511;
    }
    return 0;
}

/* Wrap-around Viterbi, then CRC and grammar constrained circular traceback.
 * Each pass extends the same metrics across the block boundary. Each candidate
 * must return to its starting state, so accepting an open path is impossible.
 * Unlike the WSJT-X coherent list decoder this uses noncoherent tone scores. */

void jt_fec_init(jt_fec *work) {
    for (unsigned s = 0; s < 512; s++)
        for (unsigned d = 0; d < 2; d++)
            work->tone[d][s] = (uint8_t) jt_trellis_tone(s | (d << 9));
}

int jtty_decode(const float scores[46][4], uint64_t *payload) {
    jt_fec work;
    jt_fec_init(&work);
    return jt_decode(&work, scores, payload);
}

int jt_decode(jt_fec *work, const float scores[46][4], uint64_t *payload) {
    if (!scores || !payload)
        return -1;

    *payload = 0;

    float(*obs)[4] = work->obs;
    float *high = work->high;
    double scale = 0;

    for (int t = 0; t < 46; t++) {
        float hi = -INFINITY, lo = INFINITY;

        for (int k = 0; k < 4; k++) {
            if (!isfinite(scores[t][k]))
                return -1;

            if (scores[t][k] > hi)
                hi = scores[t][k];

            if (scores[t][k] < lo)
                lo = scores[t][k];
        }

        high[t] = hi;
        double range = (double) hi - (double) lo;

        if (range > scale)
            scale = range;
    }

    if (scale == 0)
        return -1;

    /* One scale for the whole block preserves all relative reliabilities. */

    for (int t = 0; t < 46; t++)
        for (int k = 0; k < 4; k++)
            obs[t][k] = (float) (((double) scores[t][k] - high[t]) / scale);

    float *prev = work->prev, *next = work->next;
    uint8_t(*trace)[512] = work->trace, (*tone)[512] = work->tone;
    memset(prev, 0, sizeof(work->prev));

    for (int pass = 0; pass < 4; pass++) {
        for (int t = 0; t < 46; t++) {
            float peak = -INFINITY;
            for (int s = 0; s < 512; s++) {
                float a = prev[s >> 1] + obs[t][tone[0][s]];
                float b = prev[(s >> 1) | 256] + obs[t][tone[1][s]];
                trace[t][s] = (uint8_t) (b > a);
                next[s] = b > a ? b : a;
                if (next[s] > peak)
                    peak = next[s];
            }
            for (int s = 0; s < 512; s++)
                prev[s] = next[s] - peak;
        }

        if (pass < 1)
            continue;

        float    best = -INFINITY;
        uint64_t selected = 0;
        int      found = 0;

        for (unsigned end = 0; end < 512; end++) {
            unsigned state = end;
            uint64_t info = 0;

            for (int t = 45; t >= 0; t--) {
                info |= (uint64_t) (state & 1) << (45 - t);
                state = (state >> 1) | ((unsigned) trace[t][state] << 8);
            }

            if (state != end || jt_crc(info, 46) != 0)
                continue;

            uint64_t  p = info >> 12;
            jtty_atom a;
            int       eom;

            if (jtty_atom_unpack(p, &a, &eom))
                continue;

            /* Score this block alone: cumulative WAVA metrics include history. */

            uint8_t encoded[59];
            jtty_encode(p, encoded);

            float metric = 0;

            for (int t = 0; t < 46; t++)
                metric += obs[t][encoded[t + 13]];

            if (metric > best) {
                best = metric;
                selected = p;
                found = 1;
            }
        }

        if (found) {
            *payload = selected;
            return 0;
        }
    }
    return -1;
}
