/*---------------------------------------------------------------------------*\

  rade_acq.c

  Pilot-based acquisition and synchronization for RADAE.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2024 David Rowe

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  - Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  - Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "rade_acq.h"
#include "kiss_fft.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* zbitxre, 15 October 2026: the pilot search, rade_acq_detect_pilots(),
   correlates the received signal with the pilot at every one of Nmf (960)
   times and every one of the 40 frequency offsets, a sum of M (160)
   products each, at both pilots: some 12 million complex products a
   frame, 100 million a second. On a Raspberry Pi Zero 2 W that was more
   than one processor could do, and the receiver could not keep up while
   it heard no signal. Each frequency's correlation is now done by FFT:
   the received samples are transformed once, multiplied by the spectrum
   of the frequency-shifted pilot, made when the receiver is, and
   transformed back, giving all Nmf times at once. The result is the same
   sums, to the rounding of floats, for about a tenth of the work; the
   threshold and everything after are as before. The FFT is Opus's, which
   the library is built on, and one plan serves every receiver: it is made
   when the first is, and kept. */
static kiss_fft_state *acq_fft;

static void acq_fft_make(void) {
    if (acq_fft == NULL)
        acq_fft = opus_fft_alloc(RADE_ACQ_NFFT, NULL, NULL, 0);
    assert(acq_fft != NULL);
}

/* corr[t] = sum over n of x[t+n] * conj(h[n]), for t from 0 to Nmf - 1,
   where X is x's spectrum and H h's, both as opus_fft_c() gives them,
   divided by the FFT's size */
static void acq_correlate(const kiss_fft_cpx *X, const RADE_COMP *H, kiss_fft_cpx *corr) {
    kiss_fft_cpx Y[RADE_ACQ_NFFT];
    float n = (float)RADE_ACQ_NFFT;

    for (int i = 0; i < RADE_ACQ_NFFT; i++) {
        Y[i].r = n * (X[i].r * H[i].real + X[i].i * H[i].imag);
        Y[i].i = n * (X[i].i * H[i].real - X[i].r * H[i].imag);
    }
    opus_ifft_c(acq_fft, Y, corr);
}

/* the spectrum of Nmf + M received samples from rx, as opus_fft_c() gives it */
static void acq_spectrum(const RADE_COMP *rx, kiss_fft_cpx *X) {
    kiss_fft_cpx x[RADE_ACQ_NFFT];

    memset(x, 0, sizeof(x));
    for (int n = 0; n < RADE_NMF + RADE_M; n++) {
        x[n].r = rx[n].real;
        x[n].i = rx[n].imag;
    }
    opus_fft_c(acq_fft, x, X);
}

/*---------------------------------------------------------------------------*\
                           INITIALIZATION
\*---------------------------------------------------------------------------*/

void rade_acq_init(rade_acq *acq, const rade_ofdm *ofdm, float frange, float fstep) {
    memset(acq, 0, sizeof(rade_acq));

    acq->fs = RADE_FS;
    acq->m = RADE_M;
    acq->ncp = RADE_NCP;
    acq->nmf = RADE_NMF;

    acq->Pacq_error1 = RADE_ACQ_PACQ_ERR1;
    acq->Pacq_error2 = RADE_ACQ_PACQ_ERR2;

    /* Copy pilot symbols from OFDM */
    memcpy(acq->p, ofdm->p, sizeof(RADE_COMP) * RADE_M);
    memcpy(acq->pend, ofdm->pend, sizeof(RADE_COMP) * RADE_M);

    /* Calculate pilot power */
    RADE_COMP p_dot = rade_cdot(ofdm->p, ofdm->p, RADE_M);
    acq->sigma_p = sqrtf(p_dot.real);

    /* Set up frequency search range */
    acq->n_fcoarse = 0;
    for (float f = -frange / 2.0f; f < frange / 2.0f && acq->n_fcoarse < RADE_ACQ_NFREQ; f += fstep) {
        acq->fcoarse_range[acq->n_fcoarse++] = f;
    }

    /* Pre-compute frequency-shifted pilots: p_w[n][f_idx] = p[n] * exp(j*w*n)
       where w = 2*pi*f/Fs */
    for (int f_idx = 0; f_idx < acq->n_fcoarse; f_idx++) {
        float f = acq->fcoarse_range[f_idx];
        float w = 2.0f * M_PI * f / acq->fs;

        for (int n = 0; n < RADE_M; n++) {
            RADE_COMP w_vec = rade_cexp(w * n);
            acq->p_w[f_idx][n] = rade_cmul(w_vec, acq->p[n]);
        }
    }

    /* zbitxre: the shifted pilots' spectra, for the search by FFT */
    acq_fft_make();
    for (int f_idx = 0; f_idx < acq->n_fcoarse; f_idx++) {
        kiss_fft_cpx h[RADE_ACQ_NFFT];
        kiss_fft_cpx H[RADE_ACQ_NFFT];

        memset(h, 0, sizeof(h));
        for (int n = 0; n < RADE_M; n++) {
            h[n].r = acq->p_w[f_idx][n].real;
            h[n].i = acq->p_w[f_idx][n].imag;
        }
        opus_fft_c(acq_fft, h, H);
        for (int i = 0; i < RADE_ACQ_NFFT; i++) {
            acq->P_w[f_idx][i].real = H[i].r;
            acq->P_w[f_idx][i].imag = H[i].i;
        }
    }
}

/*---------------------------------------------------------------------------*\
                           PILOT DETECTION
\*---------------------------------------------------------------------------*/

int rade_acq_detect_pilots(rade_acq *acq, const RADE_COMP *rx, int *tmax, float *fmax) {
    int M = acq->m;
    int Nmf = acq->nmf;
    int n_fcoarse = acq->n_fcoarse;

    /* We need buffer of 2*Nmf + M + Ncp samples */
    /* Search over one modem frame for maxima */

    float Dtmax12 = 0.0f;
    int f_ind_max = 0;
    int t_max = 0;
    float f_max = 0.0f;

    /* Clear correlation grids */
    memset(acq->Dt1, 0, sizeof(acq->Dt1));
    memset(acq->Dt2, 0, sizeof(acq->Dt2));

    /* Correlate with each frequency-shifted pilot at every time:
       Dt1[t] = sum(conj(rx[t:t+M]) * p_w[:][f_idx]), and Dt2 the same a
       frame later. zbitxre: by FFT, which gives the conjugate of the sum,
       sum(rx[t:t+M] * conj(p_w)), for every t at once */
    kiss_fft_cpx X1[RADE_ACQ_NFFT];
    kiss_fft_cpx X2[RADE_ACQ_NFFT];
    kiss_fft_cpx c1[RADE_ACQ_NFFT];
    kiss_fft_cpx c2[RADE_ACQ_NFFT];

    (void)M;
    acq_spectrum(rx, X1);
    acq_spectrum(rx + Nmf, X2);
    for (int f_idx = 0; f_idx < n_fcoarse; f_idx++) {
        acq_correlate(X1, acq->P_w[f_idx], c1);
        acq_correlate(X2, acq->P_w[f_idx], c2);
        for (int t = 0; t < Nmf; t++) {
            acq->Dt1[t][f_idx].real = c1[t].r;
            acq->Dt1[t][f_idx].imag = -c1[t].i;
            acq->Dt2[t][f_idx].real = c2[t].r;
            acq->Dt2[t][f_idx].imag = -c2[t].i;
        }
    }

    /* Search over time and frequency, in the order the sums were made
       before, so that a tie goes as it did */
    for (int t = 0; t < Nmf; t++) {
        for (int f_idx = 0; f_idx < n_fcoarse; f_idx++) {
            /* Combined metric: |Dt1| + |Dt2| */
            float Dt12 = rade_cabs(acq->Dt1[t][f_idx]) + rade_cabs(acq->Dt2[t][f_idx]);

            if (Dt12 > Dtmax12) {
                Dtmax12 = Dt12;
                f_ind_max = f_idx;
                f_max = acq->fcoarse_range[f_idx];
                t_max = t;
            }
        }
    }

    /* Calculate threshold based on noise statistics
       Ref: radae.pdf "Pilot Detection over Multiple Frames" */
    float sum_abs_Dt1 = 0.0f;
    float sum_abs_Dt2 = 0.0f;
    int count = 0;

    for (int t = 0; t < Nmf; t++) {
        for (int f_idx = 0; f_idx < n_fcoarse; f_idx++) {
            sum_abs_Dt1 += rade_cabs(acq->Dt1[t][f_idx]);
            sum_abs_Dt2 += rade_cabs(acq->Dt2[t][f_idx]);
            count++;
        }
    }

    float sigma_r1 = (sum_abs_Dt1 / count) / sqrtf(M_PI / 2.0f);
    float sigma_r2 = (sum_abs_Dt2 / count) / sqrtf(M_PI / 2.0f);
    float sigma_r = (sigma_r1 + sigma_r2) / 2.0f;

    /* Threshold for detection */
    acq->Dthresh = 2.0f * sigma_r * sqrtf(-logf(acq->Pacq_error1 / 5.0f));
    acq->Dtmax12 = Dtmax12;
    acq->f_ind_max = f_ind_max;

    *tmax = t_max;
    *fmax = f_max;

    return (Dtmax12 > acq->Dthresh) ? 1 : 0;
}

void rade_acq_refine(rade_acq *acq, const RADE_COMP *rx,
                     int *tmax, float *fmax,
                     int tfine_range_start, int tfine_range_end,
                     float ffine_range_start, float ffine_range_end, float ffine_step) {
    int M = acq->m;
    int Nmf = acq->nmf;

    float Dtmax = 0.0f;
    int t_best = *tmax;
    float f_best = *fmax;

    /* Fine search over time and frequency */
    for (float f = ffine_range_start; f < ffine_range_end; f += ffine_step) {
        float w = 2.0f * M_PI * f / acq->fs;

        /* Pre-compute frequency shift vectors */
        RADE_COMP w_vec1_p[RADE_M];
        RADE_COMP w_vec2_p[RADE_M];

        for (int n = 0; n < M; n++) {
            RADE_COMP w_vec1 = rade_cexp(-w * n);
            w_vec1_p[n] = rade_cmul(w_vec1, rade_cconj(acq->p[n]));

            RADE_COMP w_vec2 = rade_cmul(w_vec1, rade_cexp(-w * Nmf));
            w_vec2_p[n] = rade_cmul(w_vec2, rade_cconj(acq->p[n]));
        }

        for (int t = tfine_range_start; t < tfine_range_end; t++) {
            /* Correlate at this time/freq */
            RADE_COMP Dt1 = rade_czero();
            RADE_COMP Dt2 = rade_czero();

            for (int n = 0; n < M; n++) {
                Dt1 = rade_cadd(Dt1, rade_cmul(rx[t + n], w_vec1_p[n]));
                Dt2 = rade_cadd(Dt2, rade_cmul(rx[t + Nmf + n], w_vec2_p[n]));
            }

            /* Combined metric: |Dt1 + Dt2| */
            RADE_COMP Dt_sum = rade_cadd(Dt1, Dt2);
            float Dt = rade_cabs(Dt_sum);

            if (Dt > Dtmax) {
                Dtmax = Dt;
                t_best = t;
                f_best = f;
            }
        }
    }

    *tmax = t_best;
    *fmax = f_best;
}

int rade_acq_check_pilots(rade_acq *acq, const RADE_COMP *rx,
                          int tmax, float fmax,
                          int *valid, int *endofover) {
    int M = acq->m;
    int Ncp = acq->ncp;
    int Nmf = acq->nmf;
    float Fs = (float)acq->fs;

    /* Update 5% of the correlation grid for noise estimation */
    int Nupdate = (int)(0.05f * Nmf);
    for (int i = 0; i < Nupdate; i++) {
        int t = rand() % Nmf;

        for (int f_idx = 0; f_idx < acq->n_fcoarse; f_idx++) {
            RADE_COMP Dt1 = rade_czero();
            RADE_COMP Dt2 = rade_czero();

            for (int n = 0; n < M; n++) {
                Dt1 = rade_cadd(Dt1, rade_cmul(rade_cconj(rx[t + n]), acq->p_w[f_idx][n]));
                Dt2 = rade_cadd(Dt2, rade_cmul(rade_cconj(rx[t + Nmf + n]), acq->p_w[f_idx][n]));
            }

            acq->Dt1[t][f_idx] = Dt1;
            acq->Dt2[t][f_idx] = Dt2;
        }
    }

    /* Recalculate noise statistics */
    float sum_abs_Dt1 = 0.0f;
    float sum_abs_Dt2 = 0.0f;
    int count = 0;

    for (int t = 0; t < Nmf; t++) {
        for (int f_idx = 0; f_idx < acq->n_fcoarse; f_idx++) {
            sum_abs_Dt1 += rade_cabs(acq->Dt1[t][f_idx]);
            sum_abs_Dt2 += rade_cabs(acq->Dt2[t][f_idx]);
            count++;
        }
    }

    float sigma_r1 = (sum_abs_Dt1 / count) / sqrtf(M_PI / 2.0f);
    float sigma_r2 = (sum_abs_Dt2 / count) / sqrtf(M_PI / 2.0f);
    float sigma_r = (sigma_r1 + sigma_r2) / 2.0f;

    acq->Dthresh = 2.0f * sigma_r * sqrtf(-logf(acq->Pacq_error2 / 5.0f));
    float Dthresh_eoo = 2.0f * sigma_r * sqrtf(-logf(acq->Pacq_error1 / 5.0f));

    /* Compute correlation at current timing/freq */
    float w = 2.0f * M_PI * fmax / Fs;
    RADE_COMP w_vec[RADE_M];
    for (int n = 0; n < M; n++) {
        w_vec[n] = rade_cexp(-w * n);
    }

    /* Correlate with normal pilots */
    RADE_COMP Dt1 = rade_czero();
    RADE_COMP Dt2 = rade_czero();
    for (int n = 0; n < M; n++) {
        RADE_COMP rx_shifted = rade_cmul(w_vec[n], rx[tmax + n]);
        Dt1 = rade_cadd(Dt1, rade_cmul(rade_cconj(rx_shifted), acq->p[n]));

        rx_shifted = rade_cmul(w_vec[n], rx[tmax + Nmf + n]);
        Dt2 = rade_cadd(Dt2, rade_cmul(rade_cconj(rx_shifted), acq->p[n]));
    }

    float Dtmax12 = rade_cabs(Dt1) + rade_cabs(Dt2);
    acq->Dtmax12 = Dtmax12;

    /* Correlate with EOO pilots */
    RADE_COMP Dt1_eoo = rade_czero();
    RADE_COMP Dt2_eoo = rade_czero();
    for (int n = 0; n < M; n++) {
        /* EOO pilot at position M+Ncp after start */
        RADE_COMP rx_shifted = rade_cmul(w_vec[n], rx[tmax + M + Ncp + n]);
        Dt1_eoo = rade_cadd(Dt1_eoo, rade_cmul(rade_cconj(rx_shifted), acq->pend[n]));

        /* EOO pilot at Nmf */
        rx_shifted = rade_cmul(w_vec[n], rx[tmax + Nmf + n]);
        Dt2_eoo = rade_cadd(Dt2_eoo, rade_cmul(rade_cconj(rx_shifted), acq->pend[n]));
    }

    float Dtmax12_eoo = rade_cabs(Dt1_eoo) + rade_cabs(Dt2_eoo);
    acq->Dtmax12_eoo = Dtmax12_eoo;

    *valid = (Dtmax12 > acq->Dthresh) ? 1 : 0;
    *endofover = (Dtmax12_eoo > Dthresh_eoo) ? 1 : 0;

    return *valid;
}
