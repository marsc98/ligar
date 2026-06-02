#include "dtw.h"
#include <float.h>
#include <math.h>

#define DTW_MAX_FRAMES 100

/* Rolling 2-row buffer: s_rows[i%2] = linha atual, s_rows[1-(i%2)] = linha anterior */
static float s_rows[2][DTW_MAX_FRAMES];

static float euclidean(const float *a, const float *b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sqrtf(sum);
}

float dtw_distance(const float *query, const float *ref,
                   int n_frames, int n_coefs, int window) {
    for (int j = 0; j < n_frames; j++) {
        s_rows[0][j] = FLT_MAX;
        s_rows[1][j] = FLT_MAX;
    }

    for (int i = 0; i < n_frames; i++) {
        int cur = i & 1;
        int prv = cur ^ 1;

        for (int j = 0; j < n_frames; j++) s_rows[cur][j] = FLT_MAX;

        int j_start = (i - window > 0) ? i - window : 0;
        int j_end   = (i + window < n_frames) ? i + window : n_frames - 1;

        for (int j = j_start; j <= j_end; j++) {
            float cost = euclidean(query + i * n_coefs, ref + j * n_coefs, n_coefs);
            if (i == 0 && j == 0) {
                s_rows[cur][0] = cost;
                continue;
            }
            float prev = FLT_MAX;
            if (i > 0              && s_rows[prv][j]   < prev) prev = s_rows[prv][j];
            if (j > 0              && s_rows[cur][j-1] < prev) prev = s_rows[cur][j-1];
            if (i > 0 && j > 0     && s_rows[prv][j-1] < prev) prev = s_rows[prv][j-1];
            s_rows[cur][j] = (prev < FLT_MAX) ? (cost + prev) : FLT_MAX;
        }
    }
    return s_rows[(n_frames - 1) & 1][n_frames - 1] / n_frames;
}
