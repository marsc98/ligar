#include "i2s_reader_task.h"
#include "app_state.h"
#include "drivers/i2s_driver.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define I2S_READ_BYTES (I2S_READ_CHUNK * sizeof(int16_t))

static const char *TAG = "I2S_RD";

void i2s_reader_task(void *arg) {
    (void)arg;
    int16_t *buf = heap_caps_malloc(I2S_READ_BYTES, MALLOC_CAP_DMA);
    if (!buf) {
        ESP_LOGE(TAG, "Falha ao alocar buffer DMA");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        size_t n = i2s_read_16bit(buf, I2S_READ_CHUNK);
        if (n == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        i2s_chunk_t chunk;
        chunk.n = n;
        memcpy(chunk.samples, buf, n * sizeof(int16_t));

        xQueueSend(g_audio_queue, &chunk, 0);
        xQueueSend(g_kws_queue,   &chunk, 0);
    }
}
