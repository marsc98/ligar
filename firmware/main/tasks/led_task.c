#include "led_task.h"
#include "app_state.h"
#include "drivers/ledc_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

void led_task(void *arg) {
    (void)arg;
    led_command_t cmd;
    while (1) {
        xQueueReceive(g_led_queue, &cmd, portMAX_DELAY);
        ledc_set_color(cmd.r, cmd.g, cmd.b);
    }
}
