#pragma once
#include <stddef.h>
#include <stdint.h>

#define I2S_READ_CHUNK 512

void   i2s_driver_init(void);
size_t i2s_read_16bit(int16_t *out_buf, size_t num_samples);
