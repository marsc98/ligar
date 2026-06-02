#include "mfcc.h"
#include <math.h>
#include <string.h>
#include <stdbool.h>

static float s_hann[MFCC_FRAME_LEN];
static float s_mel_fb[MFCC_N_MELS][MFCC_N_FFT / 2 + 1];
static float s_dct[MFCC_N_COEFS][MFCC_N_MELS];
static bool  s_initialized = false;

static float hz_to_mel(float hz) {
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

static float mel_to_hz(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

static void init_tables(void) {
    for (int i = 0; i < MFCC_FRAME_LEN; i++) {
        s_hann[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (MFCC_FRAME_LEN - 1)));
    }

    const float fmin    = 300.0f;
    const float fmax    = 8000.0f;
    const int   n_bins  = MFCC_N_FFT / 2 + 1;
    const float mel_min = hz_to_mel(fmin);
    const float mel_max = hz_to_mel(fmax);

    float mel_pts[MFCC_N_MELS + 2];
    for (int i = 0; i < MFCC_N_MELS + 2; i++) {
        float mel = mel_min + (mel_max - mel_min) * i / (MFCC_N_MELS + 1);
        mel_pts[i] = (mel_to_hz(mel) / (MFCC_SAMPLE_RATE / 2.0f)) * (n_bins - 1);
    }

    memset(s_mel_fb, 0, sizeof(s_mel_fb));
    for (int m = 0; m < MFCC_N_MELS; m++) {
        float left   = mel_pts[m];
        float center = mel_pts[m + 1];
        float right  = mel_pts[m + 2];
        for (int k = 0; k < n_bins; k++) {
            float fk = (float)k;
            if (fk >= left && fk <= center && center > left) {
                s_mel_fb[m][k] = (fk - left) / (center - left);
            } else if (fk > center && fk <= right && right > center) {
                s_mel_fb[m][k] = (right - fk) / (right - center);
            }
        }
    }

    for (int c = 0; c < MFCC_N_COEFS; c++) {
        float norm = (c == 0) ? sqrtf(1.0f / MFCC_N_MELS)
                               : sqrtf(2.0f / MFCC_N_MELS);
        for (int m = 0; m < MFCC_N_MELS; m++) {
            s_dct[c][m] = norm * cosf((float)M_PI * c * (m + 0.5f) / MFCC_N_MELS);
        }
    }

    s_initialized = true;
}

static void fft_real(float *re, float *im, int n) {
    int bits = 0;
    int tmp_n = n;
    while (tmp_n >>= 1) bits++;
    for (int i = 0; i < n; i++) {
        int rev = 0, x = i;
        for (int b = 0; b < bits; b++) { rev = (rev << 1) | (x & 1); x >>= 1; }
        if (rev > i) {
            float t = re[i]; re[i] = re[rev]; re[rev] = t;
            t = im[i]; im[i] = im[rev]; im[rev] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / len;
        float wRe = cosf(ang), wIm = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float tRe = 1.0f, tIm = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                float uRe = re[i+j],           uIm = im[i+j];
                float vRe = re[i+j+len/2]*tRe - im[i+j+len/2]*tIm;
                float vIm = re[i+j+len/2]*tIm + im[i+j+len/2]*tRe;
                re[i+j]       = uRe + vRe;
                im[i+j]       = uIm + vIm;
                re[i+j+len/2] = uRe - vRe;
                im[i+j+len/2] = uIm - vIm;
                float nRe = tRe*wRe - tIm*wIm;
                tIm = tRe*wIm + tIm*wRe;
                tRe = nRe;
            }
        }
    }
}

void mfcc_compute(const int16_t *ring, int pos, int size, float *out) {
    if (!s_initialized) init_tables();

    static float re[MFCC_N_FFT];
    static float im[MFCC_N_FFT];
    static float mel_energy[MFCC_N_MELS];
    const int n_bins = MFCC_N_FFT / 2 + 1;

    for (int frame = 0; frame < MFCC_N_FRAMES; frame++) {
        int frame_off = frame * MFCC_HOP;

        memset(re, 0, sizeof(re));
        memset(im, 0, sizeof(im));
        for (int i = 0; i < MFCC_FRAME_LEN; i++) {
            int abs_i = frame_off + i;
            int idx   = (pos + abs_i) % size;
            float curr = (float)ring[idx];
            float prev = (abs_i > 0) ? (float)ring[(pos + abs_i - 1) % size] : 0.0f;
            re[i] = (curr - 0.97f * prev) * s_hann[i] / 32768.0f;
        }

        fft_real(re, im, MFCC_N_FFT);

        float power[MFCC_N_FFT / 2 + 1];
        for (int k = 0; k < n_bins; k++) {
            power[k] = re[k]*re[k] + im[k]*im[k];
        }

        for (int m = 0; m < MFCC_N_MELS; m++) {
            float energy = 0.0f;
            for (int k = 0; k < n_bins; k++) {
                energy += s_mel_fb[m][k] * power[k];
            }
            mel_energy[m] = logf(energy + 1e-9f);
        }

        float *row = out + frame * MFCC_N_COEFS;
        for (int c = 0; c < MFCC_N_COEFS; c++) {
            float sum = 0.0f;
            for (int m = 0; m < MFCC_N_MELS; m++) {
                sum += s_dct[c][m] * mel_energy[m];
            }
            row[c] = sum;
        }
    }

    /* CMVN: subtrai média e normaliza por desvio padrão por coeficiente */
    float mean[MFCC_N_COEFS] = {0};
    float var[MFCC_N_COEFS]  = {0};

    for (int f = 0; f < MFCC_N_FRAMES; f++)
        for (int c = 0; c < MFCC_N_COEFS; c++)
            mean[c] += out[f * MFCC_N_COEFS + c];
    for (int c = 0; c < MFCC_N_COEFS; c++)
        mean[c] /= MFCC_N_FRAMES;

    for (int f = 0; f < MFCC_N_FRAMES; f++)
        for (int c = 0; c < MFCC_N_COEFS; c++) {
            float d = out[f * MFCC_N_COEFS + c] - mean[c];
            var[c] += d * d;
        }
    for (int c = 0; c < MFCC_N_COEFS; c++)
        var[c] = sqrtf(var[c] / MFCC_N_FRAMES + 1e-8f);

    for (int f = 0; f < MFCC_N_FRAMES; f++)
        for (int c = 0; c < MFCC_N_COEFS; c++)
            out[f * MFCC_N_COEFS + c] = (out[f * MFCC_N_COEFS + c] - mean[c]) / var[c];
}
