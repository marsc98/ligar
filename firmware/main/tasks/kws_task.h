#pragma once
#include "esp_http_server.h"

void kws_task(void *arg);
void kws_task_register_handlers(httpd_handle_t server);
