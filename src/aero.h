/* Menu "Aero" theme: background, bubbles and glass pieces. */
#pragma once
#include <stdint.h>
#include "font_data.h"

void aero_init(int w, int h);
void aero_background(uint32_t *px, int w, int h, uint32_t tint); /* expensive: build once per system and keep it */
void aero_bubbles(uint32_t *px, float sec);
void aero_glass(int x, int y, int w, int h, int r, int alpha);
void aero_pill(int x, int y, int w, int h, int r, uint32_t color, int alpha);
void aero_orb(int cx, int cy, int r, uint32_t color, int alpha);
void aero_text(const font_t *f, int x, int y, const char *t, uint32_t color, int alpha);
