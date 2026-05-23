idf_component_register(SRCS "poc-microfone.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi
esp_event esp_netif nvs_flash esp_http_server
                    PRIV_REQUIRES esp_driver_gpio esp_driver_i2s)
