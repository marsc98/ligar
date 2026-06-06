#include "kws_task.h"
#include "app_state.h"
#include "led_task.h"
#include "drivers/i2s_driver.h"
#include "mfcc.h"
#include "mlp.h"
#include "weights.h"
#include "color_catalog.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define PIN_LED                  2
#define PIN_AWAIT_LED            23
#define VAD_RMS_THRESHOLD        300.0f
#define DETECTION_COOLDOWN_MS    1000
#define TEMPORAL_VAR_THRESHOLD   0.3f
#define COLOR_TIMEOUT_MS         2000
/* threshold ajustável via /threshold endpoint (g_dtw_threshold), clampeado a [0,1] */
#define MLP_THRESHOLD_DEFAULT    0.50f

static const char *TAG = "KWS";

typedef enum { KWS_IDLE, KWS_AWAIT_COLOR } kws_mode_t;

static int16_t    s_kws_ring[MFCC_WIN_SAMPLES];
static int        s_kws_ring_pos = 0;
static float      s_mfcc_out[MFCC_N_FRAMES * MFCC_N_COEFS];
static kws_mode_t s_kws_mode          = KWS_IDLE;
static TickType_t s_color_timeout_tick = 0;
static bool       s_led_on             = false;

static esp_err_t ws_send_text(httpd_handle_t hd, int fd, const char *text) {
    httpd_ws_frame_t pkt = {
        .final      = true,
        .fragmented = false,
        .type       = HTTPD_WS_TYPE_TEXT,
        .payload    = (uint8_t *)text,
        .len        = strlen(text),
    };
    return httpd_ws_send_frame_async(hd, fd, &pkt);
}

static void send_led(uint8_t r, uint8_t g, uint8_t b);

static esp_err_t ws_monitor_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
        g_ws_monitor_fd = httpd_req_to_sockfd(req);
        xSemaphoreGive(g_ws_mutex);
        ESP_LOGI(TAG, "WebSocket /monitor conectado, fd=%d", g_ws_monitor_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = {};
    uint8_t          buf[16] = {};
    pkt.payload = buf;
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, sizeof(buf));
    if (ret != ESP_OK || pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int closing_fd = httpd_req_to_sockfd(req);
        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
        if (g_ws_monitor_fd == closing_fd) g_ws_monitor_fd = -1;
        xSemaphoreGive(g_ws_mutex);
    }
    return ret;
}

static esp_err_t http_led_handler(httpd_req_t *req) {
    char query[64] = {};
    if (httpd_req_get_url_query_len(req) > 0)
        httpd_req_get_url_query_str(req, query, sizeof(query));

    char color_str[32] = {};
    char intensity_str[8] = {};

    if (httpd_query_key_value(query, "color", color_str, sizeof(color_str)) != ESP_OK ||
        httpd_query_key_value(query, "intensity", intensity_str, sizeof(intensity_str)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Bad Request");
        return ESP_OK;
    }

    int intensity = -1;
    if (sscanf(intensity_str, "%d", &intensity) != 1 || intensity < 0 || intensity > 100) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Bad Request");
        return ESP_OK;
    }

    uint8_t r, g, b;
    if (!color_lookup(color_str, &r, &g, &b)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Bad Request");
        return ESP_OK;
    }

    send_led((uint8_t)((int)r * intensity / 100),
             (uint8_t)((int)g * intensity / 100),
             (uint8_t)((int)b * intensity / 100));

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t http_threshold_handler(httpd_req_t *req) {
    char query[32] = {};
    if (httpd_req_get_url_query_len(req) > 0) {
        httpd_req_get_url_query_str(req, query, sizeof(query));
        char val[16] = {};
        if (httpd_query_key_value(query, "v", val, sizeof(val)) == ESP_OK) {
            float thr = 0.0f;
            if (sscanf(val, "%f", &thr) == 1 && thr > 0.0f) {
                g_dtw_threshold = thr;
                ESP_LOGI(TAG, "DTW threshold: %.2f", g_dtw_threshold);
            }
        }
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static float compute_rms(const int16_t *buf, size_t n) {
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++) sum += (float)buf[i] * buf[i];
    return sqrtf(sum / n);
}

static float compute_temporal_var(const float *mfcc, int n_frames, int n_coefs) {
    float var_sum = 0.0f;
    for (int c = 0; c < n_coefs; c++) {
        float mean = 0.0f;
        for (int f = 0; f < n_frames; f++) mean += mfcc[f * n_coefs + c];
        mean /= n_frames;
        float sq_sum = 0.0f;
        for (int f = 0; f < n_frames; f++) {
            float d = mfcc[f * n_coefs + c] - mean;
            sq_sum += d * d;
        }
        var_sum += sqrtf(sq_sum / n_frames);
    }
    return var_sum / n_coefs;
}

static void send_led(uint8_t r, uint8_t g, uint8_t b) {
    led_command_t cmd = { .r = r, .g = g, .b = b };
    xQueueSend(g_led_queue, &cmd, 0);
    s_led_on = (r | g | b) != 0;
}

void kws_task_register_handlers(httpd_handle_t server) {
    httpd_uri_t ws_monitor_uri = {
        .uri          = "/monitor",
        .method       = HTTP_GET,
        .handler      = ws_monitor_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_monitor_uri);

    httpd_uri_t threshold_uri = {
        .uri     = "/threshold",
        .method  = HTTP_GET,
        .handler = http_threshold_handler,
    };
    httpd_register_uri_handler(server, &threshold_uri);

    httpd_uri_t led_uri = {
        .uri     = "/led",
        .method  = HTTP_GET,
        .handler = http_led_handler,
    };
    httpd_register_uri_handler(server, &led_uri);
}

void kws_task(void *arg) {
    httpd_handle_t server = (httpd_handle_t)arg;
    int16_t        chunk[I2S_READ_CHUNK];
    TickType_t     last_detection = 0;
    TickType_t     last_heartbeat = 0;
    static bool    s_was_voiced    = false;
    static float   s_peak_rms      = 0.0f;
    static int     s_voiced_chunks = 0;

    ESP_LOGI(TAG, "KWS iniciado (MLP)");

    while (1) {
        /* Verifica timeout do modo AWAIT_COLOR */
        if (s_kws_mode == KWS_AWAIT_COLOR) {
            if ((xTaskGetTickCount() - s_color_timeout_tick) > pdMS_TO_TICKS(COLOR_TIMEOUT_MS)) {
                s_kws_mode = KWS_IDLE;
                gpio_set_level(PIN_AWAIT_LED, 0);
                ESP_LOGI(TAG, "KWS_AWAIT_COLOR timeout — voltando a IDLE");
            }
        }

        i2s_chunk_t ichunk;
        if (xQueueReceive(g_kws_queue, &ichunk, pdMS_TO_TICKS(50)) != pdTRUE) continue;
        size_t n = ichunk.n;
        memcpy(chunk, ichunk.samples, n * sizeof(int16_t));

        for (size_t i = 0; i < n; i++) {
            s_kws_ring[s_kws_ring_pos] = chunk[i];
            s_kws_ring_pos = (s_kws_ring_pos + 1) % MFCC_WIN_SAMPLES;
        }

        float rms       = compute_rms(chunk, n);
        bool  is_voiced = (rms >= VAD_RMS_THRESHOLD);

        if (is_voiced) {
            if (rms > s_peak_rms) s_peak_rms = rms;
            s_was_voiced = true;
            s_voiced_chunks++;
            continue;
        }

        if (s_was_voiced) {
            bool long_enough = (s_voiced_chunks >= 3);
            s_was_voiced    = false;
            s_peak_rms      = 0.0f;
            s_voiced_chunks = 0;

            if (!long_enough) {
                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int mon_fd = g_ws_monitor_fd;
                xSemaphoreGive(g_ws_mutex);
                if (mon_fd >= 0) {
                    char hb[160];
                    snprintf(hb, sizeof(hb),
                             "{\"rms\":%.1f,\"threshold\":%.2f,\"word\":null,\"probs\":{},"
                             "\"rejected\":\"too_short\",\"kws_mode\":\"%s\"}",
                             rms, (double)g_dtw_threshold,
                             s_kws_mode == KWS_IDLE ? "idle" : "await_color");
                    esp_err_t ws_ret = ws_send_text(server, mon_fd, hb);
                    if (ws_ret != ESP_OK) {
                        ESP_LOGW(TAG, "monitor send falhou fd=%d", mon_fd);
                        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                        if (g_ws_monitor_fd == mon_fd) g_ws_monitor_fd = -1;
                        xSemaphoreGive(g_ws_mutex);
                    }
                }
                continue;
            }

            TickType_t now = xTaskGetTickCount();
            if ((now - last_detection) < pdMS_TO_TICKS(DETECTION_COOLDOWN_MS)) {
                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int mon_fd = g_ws_monitor_fd;
                xSemaphoreGive(g_ws_mutex);
                if (mon_fd >= 0) {
                    char hb[160];
                    snprintf(hb, sizeof(hb),
                             "{\"rms\":%.1f,\"threshold\":%.2f,\"word\":null,\"probs\":{},"
                             "\"rejected\":\"cooldown\",\"kws_mode\":\"%s\"}",
                             rms, (double)g_dtw_threshold,
                             s_kws_mode == KWS_IDLE ? "idle" : "await_color");
                    esp_err_t ws_ret = ws_send_text(server, mon_fd, hb);
                    if (ws_ret != ESP_OK) {
                        ESP_LOGW(TAG, "monitor send falhou fd=%d", mon_fd);
                        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                        if (g_ws_monitor_fd == mon_fd) g_ws_monitor_fd = -1;
                        xSemaphoreGive(g_ws_mutex);
                    }
                }
                continue;
            }

            mfcc_compute(s_kws_ring, s_kws_ring_pos, MFCC_WIN_SAMPLES, s_mfcc_out);

            float temporal_var = compute_temporal_var(s_mfcc_out, MFCC_N_FRAMES, MFCC_N_COEFS);

            if (temporal_var < TEMPORAL_VAR_THRESHOLD) {
                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int mon_fd = g_ws_monitor_fd;
                xSemaphoreGive(g_ws_mutex);
                if (mon_fd >= 0) {
                    char hb[192];
                    snprintf(hb, sizeof(hb),
                             "{\"rms\":%.1f,\"threshold\":%.2f,\"word\":null,\"probs\":{},"
                             "\"var\":%.3f,\"rejected\":\"var_gate\",\"kws_mode\":\"%s\"}",
                             rms, (double)g_dtw_threshold, temporal_var,
                             s_kws_mode == KWS_IDLE ? "idle" : "await_color");
                    esp_err_t ws_ret = ws_send_text(server, mon_fd, hb);
                    if (ws_ret != ESP_OK) {
                        ESP_LOGW(TAG, "monitor send falhou fd=%d", mon_fd);
                        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                        if (g_ws_monitor_fd == mon_fd) g_ws_monitor_fd = -1;
                        xSemaphoreGive(g_ws_mutex);
                    }
                }
                continue;
            }

            /* ── MLP inference ── */
            static float s_probs[MLP_N_CLASSES];
            mlp_infer(s_mfcc_out, s_probs);

            int best = 0;
            for (int i = 1; i < MLP_N_CLASSES; i++)
                if (s_probs[i] > s_probs[best]) best = i;

            static int s_garbage_idx = -1;
            if (s_garbage_idx < 0) {
                for (int i = 0; i < MLP_N_CLASSES; i++)
                    if (strcmp(MLP_CLASS_NAMES[i], "garbage") == 0) s_garbage_idx = i;
            }

            float mlp_thr = (g_dtw_threshold > 0.0f && g_dtw_threshold <= 1.0f)
                            ? g_dtw_threshold : MLP_THRESHOLD_DEFAULT;
            bool valid = (s_probs[best] >= mlp_thr) && (best != s_garbage_idx);
            const char *detected_word = valid ? MLP_CLASS_NAMES[best] : NULL;

            if (s_kws_mode == KWS_IDLE) {
                if (detected_word) {
                    last_detection = now;
                    if (strcmp(detected_word, "ligar") == 0) {
                        if (!s_led_on) {
                            send_led(255, 255, 255);
                            ESP_LOGI(TAG, "\"ligar\" — LED apagado, ligando branco");
                        } else {
                            s_kws_mode           = KWS_AWAIT_COLOR;
                            s_color_timeout_tick = now;
                            gpio_set_level(PIN_AWAIT_LED, 1);
                            ESP_LOGI(TAG, "\"ligar\" — LED ligado, aguardando cor");
                        }
                    } else if (strcmp(detected_word, "desligar") == 0) {
                        send_led(0, 0, 0);
                        ESP_LOGI(TAG, "\"desligar\" — LED apagado");
                    }
                    gpio_set_level(PIN_LED, 1);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    gpio_set_level(PIN_LED, 0);
                }

                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int mon_fd = g_ws_monitor_fd;
                xSemaphoreGive(g_ws_mutex);
                if (mon_fd >= 0) {
                    char json[560];
                    int  pos = snprintf(json, sizeof(json),
                                        "{\"rms\":%.1f,\"threshold\":%.2f,\"var\":%.3f,"
                                        "\"word\":%s%s%s,\"probs\":{",
                                        rms, (double)g_dtw_threshold, temporal_var,
                                        detected_word ? "\"" : "",
                                        detected_word ? detected_word : "null",
                                        detected_word ? "\"" : "");
                    for (int i = 0; i < MLP_N_CLASSES && pos < (int)sizeof(json) - 32; i++) {
                        pos += snprintf(json + pos, sizeof(json) - pos,
                                        "%s\"%s\":%.3f",
                                        i > 0 ? "," : "",
                                        MLP_CLASS_NAMES[i], s_probs[i]);
                    }
                    pos += snprintf(json + pos, sizeof(json) - pos,
                                    "},\"kws_mode\":\"idle\"}");
                    (void)pos;
                    esp_err_t ws_ret = ws_send_text(server, mon_fd, json);
                    if (ws_ret != ESP_OK) {
                        ESP_LOGW(TAG, "monitor send falhou fd=%d", mon_fd);
                        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                        if (g_ws_monitor_fd == mon_fd) g_ws_monitor_fd = -1;
                        xSemaphoreGive(g_ws_mutex);
                    }
                }

            } else { /* KWS_AWAIT_COLOR */
                if (detected_word) {
                    uint8_t r, g_val, b;
                    if (color_lookup(detected_word, &r, &g_val, &b)) {
                        send_led(r, g_val, b);
                        s_kws_mode = KWS_IDLE;
                        gpio_set_level(PIN_AWAIT_LED, 0);
                        last_detection = now;
                        ESP_LOGI(TAG, "Cor detectada: %s → LED RGB(%d,%d,%d)",
                                 detected_word, r, g_val, b);
                    } else if (strcmp(detected_word, "desligar") == 0) {
                        send_led(0, 0, 0);
                        s_kws_mode = KWS_IDLE;
                        gpio_set_level(PIN_AWAIT_LED, 0);
                        last_detection = now;
                        ESP_LOGI(TAG, "\"desligar\" em AWAIT_COLOR — LED apagado");
                    } else if (strcmp(detected_word, "ligar") == 0) {
                        s_color_timeout_tick = now;
                        last_detection = now;
                        ESP_LOGI(TAG, "\"ligar\" em AWAIT_COLOR — timer reiniciado");
                    }
                    gpio_set_level(PIN_LED, 1);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    gpio_set_level(PIN_LED, 0);
                }

                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int mon_fd = g_ws_monitor_fd;
                xSemaphoreGive(g_ws_mutex);
                if (mon_fd >= 0) {
                    char json[560];
                    int  pos = snprintf(json, sizeof(json),
                                        "{\"rms\":%.1f,\"threshold\":%.2f,\"var\":%.3f,"
                                        "\"word\":%s%s%s,\"probs\":{",
                                        rms, (double)g_dtw_threshold, temporal_var,
                                        detected_word ? "\"" : "",
                                        detected_word ? detected_word : "null",
                                        detected_word ? "\"" : "");
                    for (int i = 0; i < MLP_N_CLASSES && pos < (int)sizeof(json) - 32; i++) {
                        pos += snprintf(json + pos, sizeof(json) - pos,
                                        "%s\"%s\":%.3f",
                                        i > 0 ? "," : "",
                                        MLP_CLASS_NAMES[i], s_probs[i]);
                    }
                    pos += snprintf(json + pos, sizeof(json) - pos,
                                    "},\"kws_mode\":\"await_color\"}");
                    (void)pos;
                    esp_err_t ws_ret = ws_send_text(server, mon_fd, json);
                    if (ws_ret != ESP_OK) {
                        ESP_LOGW(TAG, "monitor send falhou fd=%d", mon_fd);
                        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                        if (g_ws_monitor_fd == mon_fd) g_ws_monitor_fd = -1;
                        xSemaphoreGive(g_ws_mutex);
                    }
                }
            }

            continue;
        }

        /* Heartbeat */
        TickType_t now_hb = xTaskGetTickCount();
        if ((now_hb - last_heartbeat) >= pdMS_TO_TICKS(1000)) {
            last_heartbeat = now_hb;
            xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
            int mon_fd = g_ws_monitor_fd;
            xSemaphoreGive(g_ws_mutex);
            if (mon_fd >= 0) {
                char hb[160];
                snprintf(hb, sizeof(hb),
                         "{\"rms\":%.1f,\"threshold\":%.2f,\"word\":null,\"probs\":{},"
                         "\"kws_mode\":\"%s\"}",
                         rms, (double)g_dtw_threshold,
                         s_kws_mode == KWS_IDLE ? "idle" : "await_color");
                esp_err_t ws_ret = ws_send_text(server, mon_fd, hb);
                if (ws_ret != ESP_OK) {
                    ESP_LOGW(TAG, "monitor send falhou fd=%d", mon_fd);
                    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                    if (g_ws_monitor_fd == mon_fd) g_ws_monitor_fd = -1;
                    xSemaphoreGive(g_ws_mutex);
                }
            }
        }
    }
}
