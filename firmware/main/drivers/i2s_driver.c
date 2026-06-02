#include "i2s_driver.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define PIN_BCLK    26
#define PIN_WS      25
#define PIN_DATA_IN 22
#define SAMPLE_RATE 16000
#define MIC_GAIN    16

static const char           *TAG      = "I2S";
static i2s_chan_handle_t     g_rx_chan = NULL;

void i2s_driver_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &g_rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk  = I2S_GPIO_UNUSED,
            .bclk  = PIN_BCLK,
            .ws    = PIN_WS,
            .dout  = I2S_GPIO_UNUSED,
            .din   = PIN_DATA_IN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.slot_mode      = I2S_SLOT_MODE_MONO;
    std_cfg.slot_cfg.slot_mask      = I2S_STD_SLOT_LEFT;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(g_rx_chan));
    ESP_LOGI(TAG, "I2S inicializado: %d Hz, 32-bit (dados 24-bit MSB), mono", SAMPLE_RATE);
}

size_t i2s_read_16bit(int16_t *out_buf, size_t num_samples) {
    int32_t raw32[I2S_READ_CHUNK];
    size_t  bytes_read = 0;

    esp_err_t err = i2s_channel_read(g_rx_chan, raw32,
                                     num_samples * sizeof(int32_t),
                                     &bytes_read, portMAX_DELAY);
    if (err != ESP_OK) return 0;

    size_t samples_read = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < samples_read; i++) {
        int32_t s = (int32_t)(raw32[i] >> 16) * MIC_GAIN;
        if (s > 32767)       s =  32767;
        else if (s < -32768) s = -32768;
        out_buf[i] = (int16_t)s;
    }
    return samples_read;
}
