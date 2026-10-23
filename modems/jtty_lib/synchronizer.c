#include "internal.h"
#include <math.h>
#include <string.h>

void jt_sync_init(jtty_rx *rx) {
    memset(rx->energy, 0, JT_ENERGY_ROWS * rx->bins * sizeof(float));

    for (size_t b = 0; b < rx->bins; b++) {
        double w = 2 * JT_PI * (rx->config.fmin + (double) b * JT_DF) / JTTY_SAMPLE_RATE;
        rx->bank[b] = (jt_bin) { cos(w), -sin(w), cos(w * JTTY_SPS), sin(w * JTTY_SPS), 1, 0, 0, 0 };
    }
}

static int nearby(const jt_pending *a, const jt_pending *b) {
    uint64_t dt = a->sample > b->sample ? a->sample - b->sample : b->sample - a->sample;

    return dt <= 12 * JT_HOP && fabs(a->frequency - b->frequency) <= 2 * JT_DF;
}

/* Online nonmaximum suppression. Retain stronger hypotheses when full.
 * All decisions depend on sample positions, never on process boundaries. */
static void candidate(jtty_rx *rx, jt_pending c) {
    for (size_t i = 0; i < rx->pending_count; i++)
        if (nearby(&c, &rx->pending[i]) && rx->pending[i].score >= c.score)
            return;

    for (size_t i = 0; i < rx->pending_count;)
        if (nearby(&c, &rx->pending[i]))
            rx->pending[i] = rx->pending[--rx->pending_count];
        else
            i++;

    if (rx->pending_count < rx->config.candidate_capacity) {
        rx->pending[rx->pending_count++] = c;
        return;
    }

    size_t weakest = 0;

    for (size_t i = 1; i < rx->pending_count; i++)
        if (rx->pending[i].score < rx->pending[weakest].score)
            weakest = i;

    if (c.score > rx->pending[weakest].score)
        rx->pending[weakest] = c;

    rx->stats.candidates_dropped++;
}

void jt_sync_push(jtty_rx *rx, float sample) {
    uint64_t n = rx->stats.samples;
    double   old = n >= JTTY_SPS ? rx->audio[(n - JTTY_SPS) % JT_AUDIO_SIZE] : 0;

    rx->audio[n % JT_AUDIO_SIZE] = sample;

    int      snapshot = n + 1 >= JTTY_SPS && (n + 1 - JTTY_SPS) % JT_HOP == 0;
    uint64_t row = snapshot ? (n + 1 - JTTY_SPS) / JT_HOP : 0;
    float   *energy = rx->energy + (row % JT_ENERGY_ROWS) * rx->bins;

    for (size_t b = 0; b < rx->bins; b++) {
        jt_bin *v = &rx->bank[b];
        v->re += sample * v->zr - old * (v->zr * v->dr - v->zi * v->di);
        v->im += sample * v->zi - old * (v->zr * v->di + v->zi * v->dr);

        /* Periodic direct reconstruction bounds accumulated cancellation error
         * during indefinitely long streams, including silence after a signal. */
        if ((n & 65535) == 65535) {
            double zr = v->zr, zi = v->zi;

            v->re = v->im = 0;

            for (size_t k = 0; k < JTTY_SPS; k++) {
                double x = rx->audio[(n - k) % JT_AUDIO_SIZE];

                v->re += x * zr;
                v->im += x * zi;

                double r = zr * v->wr + zi * v->wi;

                zi = zi * v->wr - zr * v->wi;
                zr = r;
            }
        }

        if (snapshot)
            energy[b] = (float) ((v->re * v->re + v->im * v->im) / (JTTY_SPS * JTTY_SPS));

        double r = v->zr * v->wr - v->zi * v->wi;
        v->zi = v->zr * v->wi + v->zi * v->wr;
        v->zr = r;

        if ((n & 4095) == 4095) {
            double norm = hypot(v->zr, v->zi);
            v->zr /= norm;
            v->zi /= norm;
        }
    }

    rx->stats.samples++;

    if (!snapshot || row < JT_ENERGY_ROWS - 1)
        return;

    uint64_t start = row - (JT_ENERGY_ROWS - 1);

    for (size_t b = 0; b < rx->bases; b++) {
        double score = 0;

        for (size_t s = 0; s < 13; s++) {
            const float *e = rx->energy + ((start + s * 8) % JT_ENERGY_ROWS) * rx->bins + b;
            double       sum = (double) e[0] + e[4] + e[8] + e[12];

            if (sum > 1e-25)
                score += e[4 * jt_sync[s]] / sum;
        }

        score /= 13;

        if (score > 0.46)
            candidate(rx, (jt_pending) { start * JT_HOP, rx->config.fmin + (double) b * JT_DF, score });
    }
}
