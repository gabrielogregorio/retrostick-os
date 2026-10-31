/* 8-bit menu music (NES chip style), generated on the PC by tools/gen_audio.c. */
#pragma once
#include <stdint.h>

void chip_init(int rate);
long chip_intro_frames(void);     /* intro length, in audio frames */
long chip_loop_frames(void);      /* loop length (32 bars) */
void chip_intro(int16_t *stereo); /* call before chip_loop (the state carries on) */
void chip_loop(int16_t *stereo);
