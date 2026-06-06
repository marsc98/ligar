#include "app_state.h"
#include "tasks/audio_task.h"
#include "tasks/kws_task.h"
#include "tasks/button_task.h"
#include "tasks/led_task.h"
#include "tasks/i2s_reader_task.h"
#include "drivers/i2s_driver.h"
#include "drivers/ledc_driver.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_config.h"
#include <string.h>

#define PIN_BUTTON    4
#define PIN_LED       2
#define PIN_AWAIT_LED 23

static const char *TAG = "MAIN";

/* ── Definições dos globais declarados em app_state.h ── */
volatile app_state_t  g_app_state    = APP_IDLE;
volatile float        g_dtw_threshold = 4.2f;
int                   g_ws_record_fd = -1;
int                   g_ws_stream_fd = -1;
int                   g_ws_monitor_fd = -1;
SemaphoreHandle_t     g_ws_mutex;
QueueHandle_t         g_click_queue;
QueueHandle_t         g_led_queue;
QueueHandle_t         g_audio_queue;
QueueHandle_t         g_kws_queue;

/* ── Wi-Fi ── */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi desconectado, reconectando...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "═══════════════════════════════════");
        ESP_LOGI(TAG, " IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, " WebSocket record: ws://" IPSTR "/record",
                 IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, " WebSocket stream: ws://" IPSTR "/stream",
                 IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "═══════════════════════════════════");
    }
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid               = WIFI_SSID,
            .password           = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ── HTTP Server ── */
static httpd_handle_t start_webserver(void) {
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.max_open_sockets = 10;
    config.recv_wait_timeout = 120;
    config.send_wait_timeout = 120;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    ESP_LOGI(TAG, "Servidor HTTP iniciado na porta 80");
    return server;
}

/* ── GPIO ── */
static void gpio_init(void) {
    gpio_config_t btn_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_BUTTON),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_conf));

    gpio_config_t led_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_LED) | (1ULL << PIN_AWAIT_LED),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_conf));
    gpio_set_level(PIN_LED, 0);
    gpio_set_level(PIN_AWAIT_LED, 0);
}

/* ── App Main ── */
void app_main(void) {
    ESP_LOGI(TAG, "Iniciando sistema INMP441 ESP32 Audio");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Heap livre: %lu bytes", (unsigned long)esp_get_free_heap_size());

    g_ws_mutex    = xSemaphoreCreateMutex();
    g_click_queue = xQueueCreate(1, sizeof(uint8_t));
    g_led_queue   = xQueueCreate(4, sizeof(led_command_t));
    g_audio_queue = xQueueCreate(4, sizeof(i2s_chunk_t));
    g_kws_queue   = xQueueCreate(4, sizeof(i2s_chunk_t));

    i2s_driver_init();
    ledc_driver_init();
    gpio_init();
    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(3000));

    httpd_handle_t server = start_webserver();
    audio_task_register_handlers(server);
    kws_task_register_handlers(server);

    xTaskCreate(i2s_reader_task, "i2s_rd", 4096, NULL,   12, NULL);
    xTaskCreate(button_task,     "btn",    2048, NULL,   5,  NULL);
    xTaskCreate(audio_task,      "audio",  8192, server, 10, NULL);
    xTaskCreate(kws_task,        "kws",    8192, server, 6,  NULL);
    xTaskCreate(led_task,        "led",    2048, NULL,   5,  NULL);

    ESP_LOGI(TAG, "Sistema pronto. Click para gravar/transmitir.");
}
