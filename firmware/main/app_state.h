#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"
#include "drivers/i2s_driver.h"

typedef enum { APP_IDLE, APP_RECORDING, APP_STREAMING } app_state_t;

typedef struct {
    int16_t samples[I2S_READ_CHUNK];
    size_t  n;
} i2s_chunk_t;

extern volatile app_state_t  g_app_state;
extern volatile float        g_dtw_threshold;
extern int                   g_ws_record_fd;
extern int                   g_ws_stream_fd;
extern int                   g_ws_monitor_fd;
extern SemaphoreHandle_t     g_ws_mutex;
extern QueueHandle_t         g_click_queue;
extern QueueHandle_t         g_led_queue;
extern QueueHandle_t         g_audio_queue;
extern QueueHandle_t         g_kws_queue;
