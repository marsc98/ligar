#pragma once
#include <stdint.h>

// Executa forward pass MLP sobre features MFCC já normalizadas.
// mfcc: float[MLP_N_FRAMES * MLP_N_COEFS] (row-major, saída do mfcc_compute)
// probs: float[MLP_N_CLASSES] — saída: probabilidades softmax, soma ≈ 1.0
void mlp_infer(const float *mfcc, float *probs);
