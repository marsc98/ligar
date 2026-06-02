#include "button_task.h"
#include "app_state.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define PIN_BUTTON 4

static const char *TAG = "BTN";

void button_task(void *arg) {
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

            if (reported && !prev) {
                uint8_t evt = 1;
                xQueueSend(g_click_queue, &evt, 0);
                ESP_LOGI(TAG, "Click registrado");
            }
        }
    }
}
