#include "internal.h"
#include <math.h>

void jt_correlate(const float *audio, size_t start, double f0, float scores[4]) {
    for (int k = 0; k < 4; k++) {
        double angle = 2 * JT_PI * (f0 + k * 31.25) / 12000;
        double wr = cos(angle), wi = -sin(angle), zr = 1, zi = 0, re = 0, im = 0;

        for (size_t i = 0; i < JTTY_SPS; i++) {
            double x = audio[start + i];
            re += x * zr;
            im += x * zi;
            double r = zr * wr - zi * wi;
            zi = zr * wi + zi * wr;
            zr = r;
        }
        scores[k] = (float) ((re * re + im * im) / (JTTY_SPS * JTTY_SPS));
    }
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
