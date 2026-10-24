#pragma once
#include <stdint.h>
#include "font_data.h"

void gfx_target(uint32_t *px, int w, int h);
void gfx_gradient_v(uint32_t top, uint32_t bottom);
void gfx_rect(int x, int y, int w, int h, uint32_t color, int alpha);
void gfx_round_rect(int x, int y, int w, int h, int radius, uint32_t color, int alpha);
void gfx_round_rect_grad(int x, int y, int w, int h, int radius, uint32_t top, uint32_t bottom, int alpha);
int gfx_text(const font_t *f, int x, int y, const char *utf8, uint32_t color, int alpha);
int gfx_width(const font_t *f, const char *utf8);
uint32_t gfx_blend(uint32_t a, uint32_t b, int t); /* t 0..255 */
void gfx_star(int cx, int cy, int radius, uint32_t top, uint32_t bottom, int alpha);
