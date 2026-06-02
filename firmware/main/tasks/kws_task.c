#include "kws_task.h"
#include "app_state.h"
#include "led_task.h"
#include "drivers/i2s_driver.h"
#include "mfcc.h"
#include "dtw.h"
#include "templates.h"
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
#define VAD_RMS_THRESHOLD        300.0f
#define DTW_THRESHOLD_DEFAULT    2.0f
#define DTW_WINDOW               6
#define DETECTION_COOLDOWN_MS    1000
#define TEMPORAL_VAR_THRESHOLD   0.3f
#define GARBAGE_RATIO_THRESHOLD  0.75f
#define COLOR_TIMEOUT_MS         2000

static const char *TAG = "KWS";

typedef enum { KWS_IDLE, KWS_AWAIT_COLOR } kws_mode_t;

static int16_t    s_kws_ring[MFCC_WIN_SAMPLES];
static int        s_kws_ring_pos = 0;
static float      s_mfcc_out[MFCC_N_FRAMES * MFCC_N_COEFS];
static kws_mode_t s_kws_mode          = KWS_IDLE;
static TickType_t s_color_timeout_tick = 0;

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
    if (ret != ESP_OK) {
        int closing_fd = httpd_req_to_sockfd(req);
        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
        if (g_ws_monitor_fd == closing_fd) g_ws_monitor_fd = -1;
        xSemaphoreGive(g_ws_mutex);
    }
    return ret;
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

static int find_trigger_idx(const char *name) {
    for (int w = 0; w < KWS_N_TRIGGERS; w++) {
        if (strcmp(KWS_TRIGGERS[w].name, name) == 0) return w;
    }
    return -1;
}

static float best_dist_for_word(const kws_word_t *word) {
    float wd = 1e9f;
    for (int t = 0; t < word->n_templates; t++) {
        float d = dtw_distance(s_mfcc_out, word->templates[t],
                               MFCC_N_FRAMES, MFCC_N_COEFS, DTW_WINDOW);
        if (d < wd) wd = d;
    }
    return wd;
}

static void send_led(uint8_t r, uint8_t g, uint8_t b) {
    led_command_t cmd = { .r = r, .g = g, .b = b };
    xQueueSend(g_led_queue, &cmd, 0);
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
}

void kws_task(void *arg) {
    httpd_handle_t server = (httpd_handle_t)arg;
    int16_t        chunk[I2S_READ_CHUNK];
    TickType_t     last_detection = 0;
    TickType_t     last_heartbeat = 0;
    static bool    s_was_voiced    = false;
    static float   s_peak_rms      = 0.0f;
    static int     s_voiced_chunks = 0;

    ESP_LOGI(TAG, "KWS iniciado");

    const int garbage_idx = find_trigger_idx("garbage");
    const int ligar_idx   = find_trigger_idx("ligar");
    const int desligar_idx = find_trigger_idx("desligar");

    while (1) {
        if (g_app_state != APP_IDLE) {
            /* Botão pressionado durante AWAIT_COLOR cancela espera */
            if (s_kws_mode == KWS_AWAIT_COLOR) {
                s_kws_mode = KWS_IDLE;
                ESP_LOGI(TAG, "KWS_AWAIT_COLOR cancelado (botão)");
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* Verifica timeout do modo AWAIT_COLOR */
        if (s_kws_mode == KWS_AWAIT_COLOR) {
            if ((xTaskGetTickCount() - s_color_timeout_tick) > pdMS_TO_TICKS(COLOR_TIMEOUT_MS)) {
                s_kws_mode = KWS_IDLE;
                ESP_LOGI(TAG, "KWS_AWAIT_COLOR timeout — voltando a IDLE");
            }
        }

        size_t n = i2s_read_16bit(chunk, I2S_READ_CHUNK);
        if (n == 0) continue;

        for (size_t i = 0; i < n; i++) {
            s_kws_ring[s_kws_ring_pos] = chunk[i];
            s_kws_ring_pos = (s_kws_ring_pos + 1) % MFCC_WIN_SAMPLES;
        }

        float rms      = compute_rms(chunk, n);
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
                    char hb[128];
                    snprintf(hb, sizeof(hb),
                             "{\"rms\":%.1f,\"threshold\":%.2f,\"word\":null,\"dists\":{},"
                             "\"rejected\":\"too_short\",\"kws_mode\":\"%s\"}",
                             rms, g_dtw_threshold,
                             s_kws_mode == KWS_IDLE ? "idle" : "await_color");
                    ws_send_text(server, mon_fd, hb);
                }
                continue;
            }

            TickType_t now = xTaskGetTickCount();
            if ((now - last_detection) < pdMS_TO_TICKS(DETECTION_COOLDOWN_MS)) {
                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int mon_fd = g_ws_monitor_fd;
                xSemaphoreGive(g_ws_mutex);
                if (mon_fd >= 0) {
                    char hb[128];
                    snprintf(hb, sizeof(hb),
                             "{\"rms\":%.1f,\"threshold\":%.2f,\"word\":null,\"dists\":{},"
                             "\"rejected\":\"cooldown\",\"kws_mode\":\"%s\"}",
                             rms, g_dtw_threshold,
                             s_kws_mode == KWS_IDLE ? "idle" : "await_color");
                    ws_send_text(server, mon_fd, hb);
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
                    char hb[160];
                    snprintf(hb, sizeof(hb),
                             "{\"rms\":%.1f,\"threshold\":%.2f,\"word\":null,\"dists\":{},"
                             "\"var\":%.3f,\"rejected\":\"var_gate\",\"kws_mode\":\"%s\"}",
                             rms, g_dtw_threshold, temporal_var,
                             s_kws_mode == KWS_IDLE ? "idle" : "await_color");
                    ws_send_text(server, mon_fd, hb);
                }
                continue;
            }

            /* ── Comparação por modo ── */
            const char *detected_word   = NULL;
            float       best_dist_val   = 1e9f;
            const char *reject_reason   = NULL;
            bool        led_action      = false;

            if (s_kws_mode == KWS_IDLE) {
                /* Compara contra KWS_TRIGGERS */
                float trigger_best[KWS_N_TRIGGERS];
                int   best_word = -1;
                for (int w = 0; w < KWS_N_TRIGGERS; w++) {
                    trigger_best[w] = best_dist_for_word(&KWS_TRIGGERS[w]);
                    if (trigger_best[w] < best_dist_val) {
                        best_dist_val = trigger_best[w];
                        best_word     = w;
                    }
                }

                float garbage_dist = (garbage_idx >= 0) ? trigger_best[garbage_idx] : 1e9f;
                bool  below_thr    = (best_word >= 0 && best_word != garbage_idx &&
                                      best_dist_val < g_dtw_threshold);
                bool  ratio_ok     = true;
                if (below_thr && garbage_idx >= 0) {
                    float ratio = best_dist_val / garbage_dist;
                    ratio_ok = (ratio < GARBAGE_RATIO_THRESHOLD);
                    if (!ratio_ok) reject_reason = "garbage_ratio";
                }

                if (below_thr && ratio_ok) {
                    detected_word  = KWS_TRIGGERS[best_word].name;
                    last_detection = now;

                    if (best_word == ligar_idx) {
                        s_kws_mode          = KWS_AWAIT_COLOR;
                        s_color_timeout_tick = now;
                        ESP_LOGI(TAG, "\"ligar\" detectado — aguardando cor");
                    } else if (best_word == desligar_idx) {
                        send_led(0, 0, 0);
                        ESP_LOGI(TAG, "\"desligar\" — LED apagado");
                    }

                    gpio_set_level(PIN_LED, 1);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    gpio_set_level(PIN_LED, 0);
                    led_action = true;
                }

                /* JSON do monitor */
                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int mon_fd = g_ws_monitor_fd;
                xSemaphoreGive(g_ws_mutex);
                if (mon_fd >= 0) {
                    char json[512];
                    int  pos = snprintf(json, sizeof(json),
                                        "{\"rms\":%.1f,\"threshold\":%.1f,\"var\":%.3f,"
                                        "\"word\":%s%s%s,\"dists\":{",
                                        rms, g_dtw_threshold, temporal_var,
                                        detected_word ? "\"" : "",
                                        detected_word ? detected_word : "null",
                                        detected_word ? "\"" : "");
                    for (int w = 0; w < KWS_N_TRIGGERS && pos < (int)sizeof(json) - 32; w++) {
                        pos += snprintf(json + pos, sizeof(json) - pos,
                                        "%s\"%s\":%.1f",
                                        w > 0 ? "," : "",
                                        KWS_TRIGGERS[w].name, trigger_best[w]);
                    }
                    pos += snprintf(json + pos, sizeof(json) - pos, "}");
                    if (garbage_idx >= 0 && garbage_dist < 1e8f)
                        pos += snprintf(json + pos, sizeof(json) - pos,
                                        ",\"garbage_dist\":%.1f", garbage_dist);
                    if (reject_reason)
                        pos += snprintf(json + pos, sizeof(json) - pos,
                                        ",\"rejected\":\"%s\"", reject_reason);
                    pos += snprintf(json + pos, sizeof(json) - pos,
                                    ",\"kws_mode\":\"idle\"}");
                    ws_send_text(server, mon_fd, json);
                }

            } else { /* KWS_AWAIT_COLOR */
                /* Compara contra KWS_COLORS */
                float color_best[KWS_N_COLORS];
                int   best_color = -1;
                for (int w = 0; w < KWS_N_COLORS; w++) {
                    color_best[w] = best_dist_for_word(&KWS_COLORS[w]);
                    if (color_best[w] < best_dist_val) {
                        best_dist_val = color_best[w];
                        best_color    = w;
                    }
                }

                bool color_detected = (best_color >= 0 && best_dist_val < g_dtw_threshold);

                if (color_detected) {
                    detected_word = KWS_COLORS[best_color].name;
                    last_detection = now;
                    uint8_t r, g_val, b;
                    if (color_lookup(detected_word, &r, &g_val, &b)) {
                        send_led(r, g_val, b);
                        ESP_LOGI(TAG, "Cor detectada: %s → LED RGB(%d,%d,%d)",
                                 detected_word, r, g_val, b);
                    }
                    s_kws_mode = KWS_IDLE;
                    gpio_set_level(PIN_LED, 1);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    gpio_set_level(PIN_LED, 0);
                    led_action = true;
                } else {
                    /* Nenhuma cor abaixo do threshold — verifica triggers especiais */
                    float trigger_best[KWS_N_TRIGGERS];
                    int   best_trig = -1;
                    float trig_best_dist = 1e9f;
                    for (int w = 0; w < KWS_N_TRIGGERS; w++) {
                        trigger_best[w] = best_dist_for_word(&KWS_TRIGGERS[w]);
                        if (w == garbage_idx) continue;
                        if (trigger_best[w] < trig_best_dist) {
                            trig_best_dist = trigger_best[w];
                            best_trig      = w;
                        }
                    }

                    if (best_trig >= 0 && trig_best_dist < g_dtw_threshold) {
                        detected_word  = KWS_TRIGGERS[best_trig].name;
                        last_detection = now;

                        if (best_trig == ligar_idx) {
                            s_color_timeout_tick = now; /* reinicia timer */
                            ESP_LOGI(TAG, "\"ligar\" em AWAIT_COLOR — timer reiniciado");
                        } else if (best_trig == desligar_idx) {
                            send_led(0, 0, 0);
                            s_kws_mode = KWS_IDLE;
                            ESP_LOGI(TAG, "\"desligar\" em AWAIT_COLOR — LED apagado");
                        }
                        gpio_set_level(PIN_LED, 1);
                        vTaskDelay(pdMS_TO_TICKS(200));
                        gpio_set_level(PIN_LED, 0);
                        led_action = true;
                    }
                }

                /* JSON do monitor */
                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int mon_fd = g_ws_monitor_fd;
                xSemaphoreGive(g_ws_mutex);
                if (mon_fd >= 0) {
                    char json[512];
                    int  pos = snprintf(json, sizeof(json),
                                        "{\"rms\":%.1f,\"threshold\":%.1f,\"var\":%.3f,"
                                        "\"word\":%s%s%s,\"dists\":{",
                                        rms, g_dtw_threshold, temporal_var,
                                        detected_word ? "\"" : "",
                                        detected_word ? detected_word : "null",
                                        detected_word ? "\"" : "");
                    for (int w = 0; w < KWS_N_COLORS && pos < (int)sizeof(json) - 64; w++) {
                        pos += snprintf(json + pos, sizeof(json) - pos,
                                        "%s\"%s\":%.1f",
                                        w > 0 ? "," : "",
                                        KWS_COLORS[w].name, color_best[w]);
                    }
                    if (reject_reason)
                        pos += snprintf(json + pos, sizeof(json) - pos,
                                        "},\"rejected\":\"%s\"", reject_reason);
                    else
                        pos += snprintf(json + pos, sizeof(json) - pos, "}");
                    pos += snprintf(json + pos, sizeof(json) - pos,
                                    ",\"kws_mode\":\"await_color\"}");
                    ws_send_text(server, mon_fd, json);
                }
            }

            (void)led_action;
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
                char hb[128];
                snprintf(hb, sizeof(hb),
                         "{\"rms\":%.1f,\"threshold\":%.2f,\"word\":null,\"dists\":{},"
                         "\"kws_mode\":\"%s\"}",
                         rms, g_dtw_threshold,
                         s_kws_mode == KWS_IDLE ? "idle" : "await_color");
                ws_send_text(server, mon_fd, hb);
            }
        }
    }
}
