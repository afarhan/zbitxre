#include "internal.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Each trailing region is aligned independently so the context needs exactly
 * one allocation, including on platforms with stronger double alignment. */

static int reserve(size_t *bytes, size_t count, size_t size, size_t alignment, size_t *offset) {
    size_t pad = (alignment - *bytes % alignment) % alignment;

    if (*bytes > SIZE_MAX - pad)
        return -1;

    *bytes += pad;
    *offset = *bytes;

    if (count > (SIZE_MAX - *bytes) / size)
        return -1;

    *bytes += count * size;
    return 0;
}

jtty_rx *jtty_rx_create(const jtty_rx_config *config) {
    if (!config || !isfinite(config->fmin) || !isfinite(config->fmax) ||
        config->fmin <= 0 || config->fmax < config->fmin || config->fmax + 93.75 >= 6000 ||
        !config->candidate_capacity || !config->decode_budget ||
        config->decode_budget > config->candidate_capacity || config->candidate_capacity > SIZE_MAX / 2)
        return NULL;

    size_t bases = (size_t) floor((config->fmax - config->fmin) / JT_DF) + 1;
    size_t bins = bases + 12, bytes = sizeof(jtty_rx), bank, energy, pending, recent, output;
    size_t capacity = config->candidate_capacity;

    if (reserve(&bytes, bins, sizeof(jt_bin), _Alignof(jt_bin), &bank) ||
        reserve(&bytes, bins * JT_ENERGY_ROWS, sizeof(float), _Alignof(float), &energy) ||
        reserve(&bytes, capacity, sizeof(jt_pending), _Alignof(jt_pending), &pending) ||
        reserve(&bytes, 2 * capacity, sizeof(jtty_frame), _Alignof(jtty_frame), &recent) ||
        reserve(&bytes, 2 * capacity, sizeof(jtty_frame), _Alignof(jtty_frame), &output))
        return NULL;

    jtty_rx *rx = calloc(1, bytes);

    if (!rx)
        return NULL;

    rx->config = *config;
    rx->stats.memory_bytes = bytes;
    rx->bases = bases;
    rx->bins = bins;
    rx->bank = (jt_bin *) ((unsigned char *) rx + bank);
    rx->energy = (float *) ((unsigned char *) rx + energy);
    rx->pending = (jt_pending *) ((unsigned char *) rx + pending);
    rx->recent = (jtty_frame *) ((unsigned char *) rx + recent);
    rx->output = (jtty_frame *) ((unsigned char *) rx + output);

    jt_fec_init(&rx->fec);
    jtty_rx_reset(rx);

    return rx;
}

void jtty_rx_reset(jtty_rx *rx) {
    if (!rx || rx->busy)
        return;

    size_t bytes = rx->stats.memory_bytes;
    memset(&rx->stats, 0, sizeof(rx->stats));

    rx->stats.memory_bytes = bytes;
    rx->epoch = 0;
    rx->pending_count = rx->recent_count = rx->recent_next = rx->output_count = 0;
    rx->finished = 0;

    memset(rx->audio, 0, sizeof(rx->audio));
    jt_sync_init(rx);
}

void jtty_rx_destroy(jtty_rx *rx) {
    if (rx && !rx->busy)
        free(rx);
}

int jtty_rx_get_stats(const jtty_rx *rx, jtty_rx_stats *stats) {
    if (!rx || !stats)
        return -1;

    *stats = rx->stats;
    stats->pending_candidates = rx->pending_count;

    return 0;
}

static void decode(jtty_rx *rx, jt_pending candidate) {
    uint64_t first = candidate.sample > JT_REFINE_MARGIN ? candidate.sample - JT_REFINE_MARGIN : 0;
    uint64_t end = candidate.sample + JTTY_FRAME_SAMPLES + JT_REFINE_MARGIN;

    if (end > rx->stats.samples)
        end = rx->stats.samples;

    size_t count = (size_t) (end - first);

    for (size_t i = 0; i < count; i++)
        rx->scratch[i] = rx->audio[(first + i) % JT_AUDIO_SIZE];

    jt_candidate c = { (size_t) (candidate.sample - first), candidate.frequency, candidate.score };
    jt_refine(rx->scratch, count, &c);

    if (c.frequency < rx->config.fmin)
        c.frequency = rx->config.fmin;

    if (c.frequency > rx->config.fmax)
        c.frequency = rx->config.fmax;

    c.score = jt_sync_score(rx->scratch, c.sample, c.frequency);

    if (c.score < 0.46)
        return;

    double time = (double) (first + c.sample) / JTTY_SAMPLE_RATE;

    for (size_t i = 0; i < rx->recent_count; i++)
        if (fabs(rx->recent[i].time - time) < 0.2 && fabs(rx->recent[i].frequency - c.frequency) < 12)
            return;

    float scores[46][4];

    for (size_t s = 0; s < 46; s++)
        jt_correlate(rx->scratch, c.sample + (s + 13) * JTTY_SPS, c.frequency, scores[s]);

    uint64_t payload;

    if (jt_decode(&rx->fec, (const float(*)[4]) scores, &payload))
        return;

    jtty_atom  atom;
    jtty_frame frame = { 0 };

    if (jtty_atom_unpack(payload, &atom, &frame.eom))
        return;

    frame.payload = payload;
    frame.time = time;
    frame.frequency = c.frequency;
    frame.sync = c.score;

    jtty_atom_render(&atom, frame.text);

    size_t recent_capacity = 2 * rx->config.candidate_capacity;

    rx->recent[rx->recent_next] = frame;
    rx->recent_next = (rx->recent_next + 1) % recent_capacity;

    if (rx->recent_count < recent_capacity)
        rx->recent_count++;

    /* Insertion sort avoids libc qsort implementations that allocate scratch. */

    size_t i = rx->output_count++;

    while (i && rx->output[i - 1].time > frame.time) {
        rx->output[i] = rx->output[i - 1];
        i--;
    }

    rx->output[i] = frame;
    rx->stats.frames++;
}

static int emit(jtty_rx *rx, double before, jtty_frame_callback callback, void *user) {
    size_t count = 0;

    while (count < rx->output_count && rx->output[count].time < before) {
        if (callback)
            callback(&rx->output[count], user);
        count++;
    }

    rx->output_count -= count;
    memmove(rx->output, rx->output + count, rx->output_count * sizeof(*rx->output));

    return (int) count;
}

static int finish_epoch(jtty_rx *rx, jtty_frame_callback callback, void *user) {
    uint64_t end = (rx->epoch + 1) * JT_EPOCH;
    size_t   attempts = 0;

    for (;;) {
        size_t best = SIZE_MAX;

        for (size_t i = 0; i < rx->pending_count; i++)
            if (rx->pending[i].sample < end &&
                (best == SIZE_MAX || rx->pending[i].score > rx->pending[best].score))
                best = i;

        if (best == SIZE_MAX)
            break;

        jt_pending c = rx->pending[best];
        rx->pending[best] = rx->pending[--rx->pending_count];

        if (c.sample + JTTY_FRAME_SAMPLES > rx->stats.samples)
            continue;

        if (attempts >= rx->config.decode_budget) {
            rx->stats.budget_dropped++;
            continue;
        }

        attempts++;
        rx->stats.decode_attempts++;
        decode(rx, c);
    }

    rx->epoch++;
    /* Future epochs may refine 60 samples backwards. Hold boundary results
     * until no future candidate can precede them. */
    return emit(rx, (double) (end - JT_REFINE_MARGIN) / JTTY_SAMPLE_RATE, callback, user);
}

int jtty_rx_process(jtty_rx *rx, const float *audio, size_t count, jtty_frame_callback callback, void *user) {
    if (!rx || rx->busy || rx->finished || (!audio && count) ||
        count > UINT64_MAX - rx->stats.samples - JT_AUDIO_SIZE || count > INT_MAX)
        return -1;

    /* Validate the whole packet before mutating the stream. */

    for (size_t i = 0; i < count; i++)
        if (!isfinite(audio[i]) || fabsf(audio[i]) > 1e6f)
            return -1;

    rx->busy = 1;
    int total = 0;

    for (size_t i = 0; i < count; i++) {
        jt_sync_push(rx, audio[i]);

        uint64_t deadline = (rx->epoch + 1) * JT_EPOCH + JTTY_FRAME_SAMPLES + JT_REFINE_MARGIN;

        if (rx->stats.samples >= deadline)
            total += finish_epoch(rx, callback, user);
    }

    rx->busy = 0;
    return total;
}

int jtty_rx_flush(jtty_rx *rx, jtty_frame_callback callback, void *user) {
    if (!rx || rx->busy)
        return -1;

    if (rx->finished)
        return 0;

    rx->busy = 1;
    int total = 0;

    while (rx->epoch * JT_EPOCH + JTTY_FRAME_SAMPLES <= rx->stats.samples)
        total += finish_epoch(rx, callback, user);

    total += emit(rx, INFINITY, callback, user);
    rx->pending_count = 0;
    rx->finished = 1;
    rx->busy = 0;

    return total;
}
