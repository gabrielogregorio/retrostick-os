/* Menu "Aero" theme: background, bubbles and glass pieces. */
#pragma once
#include <stdint.h>
#include "font_data.h"

void aero_init(int w, int h);
void aero_background(uint32_t *px, int w, int h, uint32_t tint); /* expensive: build once per system and keep it */
void aero_bubbles(uint32_t *px, float secs);
void aero_glass(int x, int y, int w, int h, int radius, int alpha);
void aero_pill(int x, int y, int w, int h, int radius, uint32_t color, int alpha);
void aero_orb(int cx, int cy, int radius, uint32_t color, int alpha);
void aero_text(const font_t *font, int x, int y, const char *text, uint32_t color, int alpha);
