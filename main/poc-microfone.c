/*
 * INMP441 + ESP32 — Captura e Transmissão de Áudio
 *
 * Pinout:
 *   INMP441 VDD  → ESP32 3.3V
 *   INMP441 GND  → ESP32 GND
 *   INMP441 SCK  → GPIO 26 (BCLK)
 *   INMP441 WS   → GPIO 25 (LRCLK)
 *   INMP441 SD   → GPIO 22 (DATA IN)
 *   INMP441 L/R  → GND (canal Left)
 *   Botão        → GPIO 4 (com pull-up interno)
 *   LED          → GPIO 2 (LED onboard — aceso durante gravação/streaming)
 *
 * Endpoints WebSocket:
 *   ws://<IP>/record  — 1º click inicia; ESP envia PCM em chunks; 2º click → "RECORDING_END:<n>"
 *   ws://<IP>/stream  — 1º click inicia stream PCM raw, 2º click encerra
 *
 * Credenciais Wi-Fi: copie main/wifi_config.h.example → main/wifi_config.h e preencha.
 */

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────── CONFIG ─────────────── */
#include "wifi_config.h"

#define I2S_NUM I2S_NUM_0
#define PIN_BCLK    26
#define PIN_WS      25
#define PIN_DATA_IN 22
#define PIN_BUTTON  4
#define PIN_LED     2

#define SAMPLE_RATE     16000
#define BITS_PER_SAMPLE 16
#define CHANNELS        1
#define MIC_GAIN        16 /* 24 dB — ajustar conforme ambiente */

/* Buffer de leitura I2S por iteração */
#define I2S_READ_CHUNK 512
#define I2S_READ_BYTES (I2S_READ_CHUNK * sizeof(int16_t))

/* WebSocket chunk de transmissão ao vivo */
#define WS_STREAM_CHUNK (I2S_READ_CHUNK * sizeof(int16_t))

static const char *TAG = "INMP441";

/* ─────────────── ESTADO GLOBAL ─────────────── */
typedef enum {
  APP_IDLE,
  APP_RECORDING,
  APP_STREAMING,
} app_state_t;

static volatile app_state_t g_state = APP_IDLE;

static size_t g_record_len = 0; /* amostras enviadas na gravação atual */

/* WebSocket — file descriptors ativos (apenas um de cada) */
static int g_ws_record_fd = -1;
static int g_ws_stream_fd = -1;

static SemaphoreHandle_t g_ws_mutex;
static QueueHandle_t     g_click_queue;

/* Handle I2S */
static i2s_chan_handle_t g_rx_chan = NULL;

/* ─────────────── WAV HEADER ─────────────── */
typedef struct __attribute__((packed)) {
  char     riff[4];
  uint32_t file_size;
  char     wave[4];
  char     fmt[4];
  uint32_t fmt_size;
  uint16_t audio_fmt;
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;
  uint16_t block_align;
  uint16_t bits_per_sample;
  char     data[4];
  uint32_t data_size;
} wav_header_t;

static void fill_wav_header(wav_header_t *h, uint32_t num_samples) {
  uint32_t data_bytes = num_samples * CHANNELS * (BITS_PER_SAMPLE / 8);
  memcpy(h->riff, "RIFF", 4);
  h->file_size       = data_bytes + sizeof(wav_header_t) - 8;
  memcpy(h->wave, "WAVE", 4);
  memcpy(h->fmt, "fmt ", 4);
  h->fmt_size        = 16;
  h->audio_fmt       = 1;
  h->num_channels    = CHANNELS;
  h->sample_rate     = SAMPLE_RATE;
  h->bits_per_sample = BITS_PER_SAMPLE;
  h->byte_rate       = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8);
  h->block_align     = CHANNELS * (BITS_PER_SAMPLE / 8);
  memcpy(h->data, "data", 4);
  h->data_size       = data_bytes;
}

/* ─────────────── I2S INIT ─────────────── */
static void i2s_init(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &g_rx_chan));

  i2s_std_config_t std_cfg = {
      .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                      I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk        = I2S_GPIO_UNUSED,
          .bclk        = PIN_BCLK,
          .ws          = PIN_WS,
          .dout        = I2S_GPIO_UNUSED,
          .din         = PIN_DATA_IN,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv   = false,
          },
      },
  };

  /* INMP441 envia 24 bits de dados nos MSB de uma palavra de 32 bits */
  std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
  std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
  std_cfg.slot_cfg.slot_mode      = I2S_SLOT_MODE_MONO;
  std_cfg.slot_cfg.slot_mask      = I2S_STD_SLOT_LEFT;

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_rx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(g_rx_chan));
  ESP_LOGI(TAG, "I2S inicializado: %d Hz, 32-bit (dados 24-bit MSB), mono",
           SAMPLE_RATE);
}

/*
 * Lê amostras do I2S e converte de 32-bit MSB para 16-bit.
 * O INMP441 coloca os dados nos 24 bits MSB da palavra de 32 bits.
 */
static size_t i2s_read_16bit(int16_t *out_buf, size_t num_samples) {
  int32_t raw32[I2S_READ_CHUNK];
  size_t  bytes_read = 0;

  esp_err_t err = i2s_channel_read(g_rx_chan, raw32,
                                   num_samples * sizeof(int32_t),
                                   &bytes_read, portMAX_DELAY);
  if (err != ESP_OK)
    return 0;

  size_t samples_read = bytes_read / sizeof(int32_t);

  /* DIAGNÓSTICO — remover após confirmar funcionamento */
  static uint32_t dbg_calls = 0;
  if (++dbg_calls <= 5) {
    int32_t mn = raw32[0], mx = raw32[0];
    for (size_t i = 1; i < samples_read; i++) {
      if (raw32[i] < mn) mn = raw32[i];
      if (raw32[i] > mx) mx = raw32[i];
    }
    ESP_LOGI(TAG, "I2S #%lu: bytes=%u raw[0]=0x%08lX min=%ld max=%ld",
             dbg_calls, (unsigned)bytes_read, (uint32_t)raw32[0], mn, mx);
  }

  for (size_t i = 0; i < samples_read; i++) {
    int32_t s = (int32_t)(raw32[i] >> 16) * MIC_GAIN;
    if (s > 32767)       s =  32767;
    else if (s < -32768) s = -32768;
    out_buf[i] = (int16_t)s;
  }
  return samples_read;
}

/* ─────────────── WEBSOCKET HANDLERS ─────────────── */

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
    ESP_LOGI(TAG, "WebSocket /record conectado, fd=%d", g_ws_record_fd);
    return ESP_OK;
  }

  httpd_ws_frame_t pkt = {};
  uint8_t          buf[128] = {};
  pkt.payload = buf;
  pkt.len     = sizeof(buf) - 1;
  esp_err_t ret = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
  if (ret != ESP_OK) {
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    g_ws_record_fd = -1;
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
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    g_ws_stream_fd = -1;
    xSemaphoreGive(g_ws_mutex);
  }
  return ret;
}

/* ─────────────── TAREFA PRINCIPAL DE ÁUDIO ─────────────── */

/*
 * Máquina de estados controlada por clicks do botão.
 *
 * IDLE      →(click)→  RECORDING ou STREAMING
 * RECORDING →(click)→  envia "RECORDING_END:<n>" → IDLE
 * STREAMING →(click)→  IDLE
 *
 * Em RECORDING o ESP faz stream de chunks PCM raw sem buffer fixo.
 * O cliente acumula os chunks e monta o WAV ao receber RECORDING_END.
 */
static void audio_task(void *arg) {
  httpd_handle_t server = (httpd_handle_t)arg;

  int16_t *read_buf = heap_caps_malloc(I2S_READ_BYTES, MALLOC_CAP_DMA);
  if (!read_buf) {
    ESP_LOGE(TAG, "Falha ao alocar buffer de leitura");
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "Tarefa de áudio iniciada");

  while (1) {
    /* Consome evento de click (não-bloqueante) */
    uint8_t evt;
    if (xQueueReceive(g_click_queue, &evt, 0) == pdTRUE) {
      switch (g_state) {
        case APP_IDLE:
          /* Prioridade: /stream > /record */
          if (g_ws_stream_fd >= 0) {
            g_state = APP_STREAMING;
            gpio_set_level(PIN_LED, 1);
            ESP_LOGI(TAG, "Streaming iniciado");
          } else if (g_ws_record_fd >= 0) {
            g_state       = APP_RECORDING;
            g_record_len  = 0;
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
          g_state = APP_IDLE;
          gpio_set_level(PIN_LED, 0);
          break;
        }

        case APP_STREAMING:
          g_state = APP_IDLE;
          gpio_set_level(PIN_LED, 0);
          ESP_LOGI(TAG, "Streaming encerrado");
          break;
      }
    }

    /* Executa ação do estado atual */
    switch (g_state) {
      case APP_STREAMING: {
        xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
        int fd = g_ws_stream_fd;
        xSemaphoreGive(g_ws_mutex);

        if (fd >= 0) {
          size_t n = i2s_read_16bit(read_buf, I2S_READ_CHUNK);
          if (n > 0)
            ws_send_binary(server, fd, read_buf, n * sizeof(int16_t));
        } else {
          /* Conexão perdida */
          g_state = APP_IDLE;
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
          g_state = APP_IDLE;
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
            g_state = APP_IDLE;
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

/* ─────────────── TAREFA DO BOTÃO ─────────────── */

/*
 * Polling a cada 10ms com debounce de 50ms (5 leituras estáveis).
 * Detecta falling edge (GPIO LOW = botão pressionado) e envia
 * evento de click para g_click_queue.
 */
static void button_task(void *arg) {
  (void)arg;
  const int STABLE_NEEDED = 5;
  bool      reported      = false;
  bool      candidate     = false;
  int       stable_count  = 0;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10));

    bool reading = (gpio_get_level(PIN_BUTTON) == 0);

    if (reading == candidate) {
      if (stable_count < STABLE_NEEDED)
        stable_count++;
    } else {
      candidate    = reading;
      stable_count = 1;
    }

    if (stable_count >= STABLE_NEEDED && candidate != reported) {
      bool prev = reported;
      reported  = candidate;
      ESP_LOGI(TAG, "Botão: %s", reported ? "PRESSIONADO" : "SOLTO");

      /* Dispara click no falling edge (pressionado) */
      if (reported && !prev) {
        uint8_t evt = 1;
        xQueueSend(g_click_queue, &evt, 0);
        ESP_LOGI(TAG, "Click registrado");
      }
    }
  }
}

/* ─────────────── WIFI ─────────────── */
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
          .ssid              = WIFI_SSID,
          .password          = WIFI_PASS,
          .threshold.authmode = WIFI_AUTH_WPA2_PSK,
      },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
}

/* ─────────────── HTTP SERVER ─────────────── */
static httpd_handle_t start_webserver(void) {
  httpd_config_t config      = HTTPD_DEFAULT_CONFIG();
  config.server_port         = 80;
  config.max_open_sockets    = 4;

  httpd_handle_t server = NULL;
  ESP_ERROR_CHECK(httpd_start(&server, &config));

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

  ESP_LOGI(TAG, "Servidor HTTP iniciado na porta 80");
  return server;
}

/* ─────────────── GPIO ─────────────── */
static void gpio_init(void) {
  /* Botão — entrada com pull-up */
  gpio_config_t btn_conf = {
      .intr_type    = GPIO_INTR_DISABLE,
      .mode         = GPIO_MODE_INPUT,
      .pin_bit_mask = (1ULL << PIN_BUTTON),
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .pull_up_en   = GPIO_PULLUP_ENABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&btn_conf));

  /* LED — saída, apagado inicialmente */
  gpio_config_t led_conf = {
      .intr_type    = GPIO_INTR_DISABLE,
      .mode         = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << PIN_LED),
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .pull_up_en   = GPIO_PULLUP_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&led_conf));
  gpio_set_level(PIN_LED, 0);
}

/* ─────────────── APP MAIN ─────────────── */
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

  g_ws_mutex   = xSemaphoreCreateMutex();
  g_click_queue = xQueueCreate(1, sizeof(uint8_t));

  i2s_init();
  wifi_init();
  vTaskDelay(pdMS_TO_TICKS(3000));

  httpd_handle_t server = start_webserver();

  gpio_init();
  xTaskCreate(button_task, "btn_task",   2048, NULL,   5, NULL);
  xTaskCreate(audio_task,  "audio_task", 8192, server, 10, NULL);

  ESP_LOGI(TAG, "Sistema pronto. Click para gravar/transmitir.");
}
