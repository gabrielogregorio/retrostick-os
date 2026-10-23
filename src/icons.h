/* System icons (glass bubble + drawn console). */
#pragma once
#include <stdint.h>

#define ICONS_MAX 8
void icons_build(int i, const char *system, uint32_t tint); /* changes the gfx target: restore it afterwards */
void icons_draw(uint32_t *screen, int w, int h, int i, int cx, int cy, float scale, int alpha);
