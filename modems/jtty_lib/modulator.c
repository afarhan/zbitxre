#include "internal.h"
#include <math.h>

int jtty_modulate(const uint8_t *tones, size_t count, double f0, float *audio, size_t capacity) {
    if (!tones || !audio || !count || count > JTTY_MAX_FRAMES * JTTY_SYMBOLS ||
        count > capacity / JTTY_SPS || !isfinite(f0) || f0 <= 0 || f0 + 93.75 >= 6000)
        return -1;

    for (size_t i = 0; i < count; i++)
        if (tones[i] > 3)
            return -1;

    const size_t sps = JTTY_SPS, n = count * sps;
    double       pulse[3 * JTTY_SPS];
    const double c = JT_PI * sqrt(2 / log(2.0)) * 2; /* BT = 2 */

    for (size_t i = 0; i < 3 * sps; i++) {
        double t = ((double) i + 1 - 1.5 * (double) sps) / (double) sps;
        pulse[i] = 0.5 * (erf(c * (t + 0.5)) - erf(c * (t - 0.5)));
    }

    double phase = 0;

    for (size_t i = 0; i < n; i++) {
        size_t t = i + sps, last = t / sps;
        size_t first = last > 2 ? last - 2 : 0;

        if (last >= count)
            last = count - 1;

        double frequency = 0;

        for (size_t j = first; j <= last; j++)
            frequency += tones[j] * pulse[t - j * sps];

        if (t < 2 * sps)
            frequency += tones[0] * pulse[t + sps];

        if (t >= n)
            frequency += tones[count - 1] * pulse[t - n];

        audio[i] = (float) sin(phase);
        phase = fmod(phase + 2 * JT_PI * (f0 / 12000 + frequency / (double) sps), 2 * JT_PI);
    }

    for (size_t i = 0; i < sps / 8; i++) {
        double x = JT_PI * (double) i / (double) (sps / 8);

        audio[i] *= (float) (0.5 * (1 - cos(x)));
        audio[n - sps / 8 + i] *= (float) (0.5 * (1 + cos(x)));
    }

    return 0;
}
