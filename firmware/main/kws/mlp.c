#include "mlp.h"
#include "weights.h"
#include <math.h>

_Static_assert(MLP_N_CLASSES > 0, "weights.h nao gerado");

static int8_t s_x_q[MLP_N_INPUT];
static int8_t s_h1_q[MLP_N_H1];
static int8_t s_h2_q[MLP_N_H2];
static float  s_h1[MLP_N_H1];
static float  s_h2[MLP_N_H2];

void mlp_infer(const float *mfcc, float *probs) {
    /* Quantiza entrada com escala própria do range MFCC de treino */
    for (int i = 0; i < MLP_N_INPUT; i++) {
        float v = mfcc[i] / MLP_SCALE_INPUT;
        if (v >  127.0f) v =  127.0f;
        if (v < -127.0f) v = -127.0f;
        s_x_q[i] = (int8_t)v;
    }

    /* Camada 1: W1 (H1 × INPUT) · x_q + b1 → ReLU
     * dequant: acc * scale_W1 * scale_INPUT (escalas independentes) */
    for (int i = 0; i < MLP_N_H1; i++) {
        int32_t acc = 0;
        for (int j = 0; j < MLP_N_INPUT; j++)
            acc += (int32_t)MLP_W1[i][j] * (int32_t)s_x_q[j];
        float v = (float)acc * MLP_SCALE_W1 * MLP_SCALE_INPUT
                + (float)MLP_B1[i] * MLP_SCALE_B1;
        s_h1[i] = (v > 0.0f) ? v : 0.0f;
    }

    /* Re-quantiza h1 com escala das ativações reais de treino */
    for (int i = 0; i < MLP_N_H1; i++) {
        float v = s_h1[i] / MLP_SCALE_ACT1;
        if (v >  127.0f) v =  127.0f;
        if (v < -127.0f) v = -127.0f;
        s_h1_q[i] = (int8_t)v;
    }

    /* Camada 2: W2 (H2 × H1) · h1_q + b2 → ReLU */
    for (int i = 0; i < MLP_N_H2; i++) {
        int32_t acc = 0;
        for (int j = 0; j < MLP_N_H1; j++)
            acc += (int32_t)MLP_W2[i][j] * (int32_t)s_h1_q[j];
        float v = (float)acc * MLP_SCALE_W2 * MLP_SCALE_ACT1
                + (float)MLP_B2[i] * MLP_SCALE_B2;
        s_h2[i] = (v > 0.0f) ? v : 0.0f;
    }

    /* Re-quantiza h2 com escala das ativações reais de treino */
    for (int i = 0; i < MLP_N_H2; i++) {
        float v = s_h2[i] / MLP_SCALE_ACT2;
        if (v >  127.0f) v =  127.0f;
        if (v < -127.0f) v = -127.0f;
        s_h2_q[i] = (int8_t)v;
    }

    /* Camada de saída: WOUT (N_CLASSES × H2) · h2_q + bout → softmax */
    float max_v = -1e38f;
    for (int i = 0; i < MLP_N_CLASSES; i++) {
        int32_t acc = 0;
        for (int j = 0; j < MLP_N_H2; j++)
            acc += (int32_t)MLP_WOUT[i][j] * (int32_t)s_h2_q[j];
        probs[i] = (float)acc * MLP_SCALE_WOUT * MLP_SCALE_ACT2
                 + (float)MLP_BOUT[i] * MLP_SCALE_BOUT;
        if (probs[i] > max_v) max_v = probs[i];
    }

    float sum = 0.0f;
    for (int i = 0; i < MLP_N_CLASSES; i++) {
        probs[i] = expf(probs[i] - max_v);
        sum += probs[i];
    }
    for (int i = 0; i < MLP_N_CLASSES; i++)
        probs[i] /= sum;
}
