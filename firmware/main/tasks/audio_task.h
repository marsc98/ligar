#pragma once
#include "esp_http_server.h"

void audio_task(void *arg);
void audio_task_register_handlers(httpd_handle_t server);
