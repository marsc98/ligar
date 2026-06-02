#include "audio_task.h"
#include "app_state.h"
#include "drivers/i2s_driver.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define SAMPLE_RATE 16000
#define PIN_LED     2

#define I2S_READ_BYTES  (I2S_READ_CHUNK * sizeof(int16_t))

static const char *TAG = "AUDIO";

static esp_err_t ws_send_binary(httpd_handle_t hd, int fd,
                                const void *data, size_t len) {
    httpd_ws_frame_t pkt = {
        .final      = true,
        .fragmented = false,
        .type       = HTTPD_WS_TYPE_BINARY,
        .payload    = (uint8_t *)data,
        .len        = len,
    };
    return httpd_ws_send_frame_async(hd, fd, &pkt);
}

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

static esp_err_t ws_record_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
        g_ws_record_fd = httpd_req_to_sockfd(req);
        xSemaphoreGive(g_ws_mutex);
        g_kws_paused = true;
        ESP_LOGI(TAG, "/record conectado — KWS pausado");
        ESP_LOGI(TAG, "WebSocket /record conectado, fd=%d", g_ws_record_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = {};
    uint8_t          buf[128] = {};
    pkt.payload = buf;
    pkt.len     = sizeof(buf) - 1;
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
    if (ret != ESP_OK || pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int closing_fd = httpd_req_to_sockfd(req);
        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
        if (g_ws_record_fd == closing_fd) {
            g_ws_record_fd = -1;
            ESP_LOGI(TAG, "/record desconectado — KWS retomado");
        }
        xSemaphoreGive(g_ws_mutex);
    }
    return ret;
}

static esp_err_t ws_stream_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
        g_ws_stream_fd = httpd_req_to_sockfd(req);
        xSemaphoreGive(g_ws_mutex);
        ESP_LOGI(TAG, "WebSocket /stream conectado, fd=%d", g_ws_stream_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = {};
    uint8_t          buf[16] = {};
    pkt.payload = buf;
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, sizeof(buf));
    if (ret != ESP_OK) {
        int closing_fd = httpd_req_to_sockfd(req);
        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
        if (g_ws_stream_fd == closing_fd) g_ws_stream_fd = -1;
        xSemaphoreGive(g_ws_mutex);
    }
    return ret;
}

void audio_task_register_handlers(httpd_handle_t server) {
    httpd_uri_t ws_record_uri = {
        .uri          = "/record",
        .method       = HTTP_GET,
        .handler      = ws_record_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_record_uri);

    httpd_uri_t ws_stream_uri = {
        .uri          = "/stream",
        .method       = HTTP_GET,
        .handler      = ws_stream_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_stream_uri);
}

void audio_task(void *arg) {
    httpd_handle_t server = (httpd_handle_t)arg;
    static size_t  g_record_len = 0;

    int16_t *read_buf = heap_caps_malloc(I2S_READ_BYTES, MALLOC_CAP_DMA);
    if (!read_buf) {
        ESP_LOGE(TAG, "Falha ao alocar buffer de leitura");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Tarefa de áudio iniciada");

    while (1) {
        uint8_t evt;
        if (xQueueReceive(g_click_queue, &evt, 0) == pdTRUE) {
            switch (g_app_state) {
                case APP_IDLE:
                    if (g_ws_stream_fd >= 0) {
                        g_app_state = APP_STREAMING;
                        gpio_set_level(PIN_LED, 1);
                        ESP_LOGI(TAG, "Streaming iniciado");
                    } else if (g_ws_record_fd >= 0) {
                        g_app_state  = APP_RECORDING;
                        g_record_len = 0;
                        gpio_set_level(PIN_LED, 1);
                        ws_send_text(server, g_ws_record_fd, "RECORDING_START");
                        ESP_LOGI(TAG, "Gravação iniciada");
                    }
                    break;

                case APP_RECORDING: {
                    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                    int fd = g_ws_record_fd;
                    xSemaphoreGive(g_ws_mutex);
                    if (fd >= 0) {
                        char msg[48];
                        snprintf(msg, sizeof(msg), "RECORDING_END:%zu", g_record_len);
                        ws_send_text(server, fd, msg);
                        ESP_LOGI(TAG, "Gravação finalizada: %zu amostras (%.1f s)",
                                 g_record_len, (float)g_record_len / SAMPLE_RATE);
                    }
                    g_record_len = 0;
                    g_app_state  = APP_IDLE;
                    gpio_set_level(PIN_LED, 0);
                    break;
                }

                case APP_STREAMING:
                    g_app_state = APP_IDLE;
                    gpio_set_level(PIN_LED, 0);
                    ESP_LOGI(TAG, "Streaming encerrado");
                    break;
            }
        }

        switch (g_app_state) {
            case APP_STREAMING: {
                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int fd = g_ws_stream_fd;
                xSemaphoreGive(g_ws_mutex);

                if (fd >= 0) {
                    size_t n = i2s_read_16bit(read_buf, I2S_READ_CHUNK);
                    if (n > 0)
                        ws_send_binary(server, fd, read_buf, n * sizeof(int16_t));
                } else {
                    g_app_state = APP_IDLE;
                    gpio_set_level(PIN_LED, 0);
                }
                break;
            }

            case APP_RECORDING: {
                xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                int fd = g_ws_record_fd;
                xSemaphoreGive(g_ws_mutex);

                if (fd < 0) {
                    g_record_len = 0;
                    g_app_state  = APP_IDLE;
                    gpio_set_level(PIN_LED, 0);
                    break;
                }

                size_t n = i2s_read_16bit(read_buf, I2S_READ_CHUNK);
                if (n > 0) {
                    if (ws_send_binary(server, fd, read_buf, n * sizeof(int16_t)) != ESP_OK) {
                        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
                        g_ws_record_fd = -1;
                        xSemaphoreGive(g_ws_mutex);
                        g_record_len = 0;
                        g_app_state  = APP_IDLE;
                        gpio_set_level(PIN_LED, 0);
                    } else {
                        g_record_len += n;
                    }
                }
                break;
            }

            case APP_IDLE:
                vTaskDelay(pdMS_TO_TICKS(10));
                break;
        }
    }
}
