#pragma once

/*
 * Calcula a distância DTW entre duas sequências MFCC.
 * query, ref: arrays float[n_frames * n_coefs], row-major.
 * Usa banda de Sakoe-Chiba (window) para reduzir custo O(n²) → O(n*w).
 */
float dtw_distance(const float *query, const float *ref,
                   int n_frames, int n_coefs, int window);
