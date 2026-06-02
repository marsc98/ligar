#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    const char *name;
    uint8_t     r, g, b;
} color_entry_t;

static const color_entry_t COLOR_CATALOG[] = {
    {"vermelho", 255,   0,   0},
    {"verde",      0, 255,   0},
    {"azul",       0,   0, 255},
    {"amarelo",  255, 255,   0},
    {"ciano",      0, 255, 255},
    {"magenta",  255,   0, 255},
    {"laranja",  255, 165,   0},
    {"roxo",     128,   0, 128},
    {"branco",   255, 255, 255},
};
static const int COLOR_CATALOG_SIZE = 9;

static inline bool color_lookup(const char *name, uint8_t *r, uint8_t *g, uint8_t *b) {
    for (int i = 0; i < COLOR_CATALOG_SIZE; i++) {
        if (strcmp(COLOR_CATALOG[i].name, name) == 0) {
            *r = COLOR_CATALOG[i].r;
            *g = COLOR_CATALOG[i].g;
            *b = COLOR_CATALOG[i].b;
            return true;
        }
    }
    return false;
}
