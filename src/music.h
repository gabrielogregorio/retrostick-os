/* Menu music and effects. Synthesized here; tools/gen_audio.c writes them to files so the stick only plays them. */
#pragma once
#include <stdint.h>

enum { SFX_MOVE, SFX_SYSTEM, SFX_OPEN, SFX_BACK, SFX_FAVORITE, SFX_UNFAVORITE, SFX_COUNT };

void music_init(int rate);
void music_render(int16_t *stereo, int frames); /* mixes music + effects */
void music_effect(int which);
void music_volume(float v); /* 0..1, with a ramp */
void music_effects_only(int on); /* 1 = sequencer stopped (recording effects) */
