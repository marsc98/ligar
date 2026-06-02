#pragma once
#include <stdint.h>

typedef struct { uint8_t r, g, b; } led_command_t;

void led_task(void *arg);
