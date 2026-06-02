#pragma once
#include <stddef.h>
#include <stdint.h>

/* Parâmetros — devem ser IDÊNTICOS ao training/extract_features.py */
#define MFCC_SAMPLE_RATE  16000
#define MFCC_FRAME_LEN    400       /* 25ms */
#define MFCC_HOP          160       /* 10ms */
#define MFCC_N_FFT        512
#define MFCC_N_MELS       26
#define MFCC_N_COEFS      13
#define MFCC_N_FRAMES     48        /* (8000 - 400) / 160 + 1 */
#define MFCC_WIN_SAMPLES  8000      /* 0.5s */

/*
 * Extrai MFCC direto do ring buffer circular.
 * ring: buffer circular de int16_t com 'size' amostras.
 * pos:  índice da amostra mais antiga (início lógico da janela).
 * size: tamanho total do ring buffer (== MFCC_WIN_SAMPLES).
 * out:  float[MFCC_N_FRAMES * MFCC_N_COEFS], linha-maior.
 */
void mfcc_compute(const int16_t *ring, int pos, int size, float *out);
