#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"

typedef enum { APP_IDLE, APP_RECORDING, APP_STREAMING } app_state_t;

extern volatile app_state_t  g_app_state;
extern volatile float        g_dtw_threshold;
extern bool                  g_kws_paused;
extern int                   g_ws_record_fd;
extern int                   g_ws_stream_fd;
extern int                   g_ws_monitor_fd;
extern SemaphoreHandle_t     g_ws_mutex;
extern QueueHandle_t         g_click_queue;
extern QueueHandle_t         g_led_queue;
