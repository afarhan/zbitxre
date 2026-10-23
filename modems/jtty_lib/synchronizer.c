#include "internal.h"
#include <math.h>
#include <string.h>

/* zbitxre: the energies of the bins at each hop are taken from one FFT of
 * the last symbol's samples, padded to JT_FFT_SIZE, in place of a sliding
 * DFT of every bin updated at every sample; they are the same energies,
 * for a fraction of the work, which a Pi Zero 2 W could not keep up with. */
void jt_sync_init(jtty_rx *rx) {
    memset(rx->energy, 0, JT_ENERGY_ROWS * rx->bins * sizeof(float));
    rx->fft_first = (size_t) lround(rx->config.fmin / JT_DF);
    rx->audio_at = 0;
    rx->until_hop = JTTY_SPS;
    rx->energy_row = 0;
    rx->rows = 0;
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

/* zbitxre: where the sample goes in the ring, when the next hop is and
 * which row of energies is the newest are counted as the samples come, and
 * wrap by subtraction, in place of a 64-bit remainder at every sample, which
 * a 32-bit Pi does in software and which was a fifth of its time; the
 * thirteen rows a score needs are found once a hop, not once a bin; and the
 * score is worked in single precision, a Pi's double division being slow. */
void jt_sync_push(jtty_rx *rx, float sample) {
    rx->audio[rx->audio_at] = sample;
    rx->audio_at++;
    if (rx->audio_at == JT_AUDIO_SIZE)
        rx->audio_at = 0;

    rx->stats.samples++;
    rx->until_hop--;

    if (rx->until_hop > 0)
        return;

    rx->until_hop = JT_HOP;

    size_t at = rx->audio_at >= JTTY_SPS ? rx->audio_at - JTTY_SPS : rx->audio_at + JT_AUDIO_SIZE - JTTY_SPS;

    for (size_t k = 0; k < JT_FFT_SIZE; k++) {
        rx->fft_in[k].r = 0;
        rx->fft_in[k].i = 0;
    }

    for (size_t k = 0; k < JTTY_SPS; k++) {
        rx->fft_in[k].r = rx->audio[at];
        at++;
        if (at == JT_AUDIO_SIZE)
            at = 0;
    }

    kiss_fft(rx->fft, rx->fft_in, rx->fft_out);

    float *energy = rx->energy + rx->energy_row * rx->bins;

    for (size_t b = 0; b < rx->bins; b++) {
        size_t k = rx->fft_first + b;
        float  re = k < JT_FFT_SIZE / 2 ? rx->fft_out[k].r : 0;
        float  im = k < JT_FFT_SIZE / 2 ? rx->fft_out[k].i : 0;

        energy[b] = (re * re + im * im) / (JTTY_SPS * JTTY_SPS);
    }

    uint64_t row = rx->rows;

    rx->rows++;
    rx->energy_row++;
    if (rx->energy_row == JT_ENERGY_ROWS)
        rx->energy_row = 0;

    if (row < JT_ENERGY_ROWS - 1)
        return;

    uint64_t     start = row - (JT_ENERGY_ROWS - 1);
    const float *rows[13];

    /* the oldest row is the one the newest will take next */
    for (size_t s = 0; s < 13; s++) {
        size_t r = rx->energy_row + s * 8;

        if (r >= JT_ENERGY_ROWS)
            r -= JT_ENERGY_ROWS;
        rows[s] = rx->energy + r * rx->bins;
    }

    for (size_t b = 0; b < rx->bases; b++) {
        float score = 0;

        for (size_t s = 0; s < 13; s++) {
            const float *e = rows[s] + b;
            float        sum = e[0] + e[4] + e[8] + e[12];

            if (sum > 1e-25f)
                score += e[4 * jt_sync[s]] / sum;
        }

        score /= 13;

        if (score > 0.46)
            candidate(rx, (jt_pending) { start * JT_HOP, rx->config.fmin + (double) b * JT_DF, score });
    }
}
