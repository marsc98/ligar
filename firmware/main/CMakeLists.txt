idf_component_register(
  SRCS
    "app_main.c"
    "tasks/audio_task.c"
    "tasks/kws_task.c"
    "tasks/button_task.c"
    "tasks/led_task.c"
    "drivers/i2s_driver.c"
    "drivers/ledc_driver.c"
    "kws/mfcc.c"
    "kws/dtw.c"
  INCLUDE_DIRS "." "tasks" "drivers" "kws"
  REQUIRES driver esp_wifi esp_event esp_netif nvs_flash esp_http_server
  PRIV_REQUIRES esp_driver_gpio esp_driver_i2s esp_driver_ledc
)
