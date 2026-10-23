#include "internal.h"
#include <math.h>

/* zbitxre: each tone's energy over the symbol by Goertzel's recursion, one
 * multiply a sample, in place of a rotating phasor, six; the four tones
 * are the four lanes of one vector, so that NEON on a Pi works them
 * together; it is the same energy, and this was most of what a Pi Zero 2 W
 * spent receiving. */
typedef float jt_four __attribute__((vector_size(16)));

void jt_correlate(const float *audio, size_t start, double f0, float scores[4]) {
    jt_four coefficient, before = { 0, 0, 0, 0 }, before_that = { 0, 0, 0, 0 }, now, x, power;

    for (int k = 0; k < 4; k++)
        coefficient[k] = (float) (2 * cos(2 * JT_PI * (f0 + k * 31.25) / 12000));

    for (size_t i = 0; i < JTTY_SPS; i++) {
        float sample = audio[start + i];

        x = (jt_four) { sample, sample, sample, sample };
        now = x + coefficient * before - before_that;
        before_that = before;
        before = now;
    }

    power = before * before + before_that * before_that - coefficient * before * before_that;

    for (int k = 0; k < 4; k++)
        scores[k] = power[k] / (JTTY_SPS * JTTY_SPS);
}

double jt_sync_score(const float *audio, size_t start, double f0) {
    double score = 0;

    for (size_t i = 0; i < 13; i++) {
        float e[4];
        jt_correlate(audio, start + i * JTTY_SPS, f0, e);
        double sum = (double) e[0] + e[1] + e[2] + e[3];
        if (sum > 1e-25)
            score += e[jt_sync[i]] / sum;
    }
    return score / 13;
}

void jt_refine(const float *audio, size_t count, jt_candidate *c) {
    /* Two small grids resolve the 4 ms / 7.8125 Hz coarse acquisition grid. */
    for (int pass = 0; pass < 2; pass++) {
        jt_candidate best = *c;
        int          dt = pass ? 6 : 24;
        double       df = pass ? 0.9765625 : 3.90625;

        for (int t = -2; t <= 2; t++)
            for (int f = -2; f <= 2; f++) {
                if (t < 0 && c->sample < (size_t) (-t * dt))
                    continue;

                size_t sample = t < 0 ? c->sample - (size_t) (-t * dt) : c->sample + (size_t) (t * dt);
                double frequency = c->frequency + f * df;

                if (sample > count - JTTY_FRAME_SAMPLES || frequency <= 0 || frequency + 93.75 >= 6000)
                    continue;

                double score = jt_sync_score(audio, sample, frequency);

                if (score > best.score) {
                    best.sample = sample;
                    best.frequency = frequency;
                    best.score = score;
                }
            }
        *c = best;
    }
}
