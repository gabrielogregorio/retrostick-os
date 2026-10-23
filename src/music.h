/* Menu music and effects (synthesized, no files). */
#pragma once
#include <stdint.h>

enum { SFX_MOVE, SFX_SYSTEM, SFX_OPEN, SFX_BACK };

void music_init(int rate);
void music_render(int16_t *stereo, int frames); /* mixes music + effects */
void music_effect(int which);
void music_volume(float v); /* 0..1, with a ramp */
