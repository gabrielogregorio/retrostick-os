/* Menu audio: music and effects prebuilt in files, with live synthesis as a fallback for the PC preview. */
#pragma once
#include <stdio.h>

FILE *menu_audio_open_asset(const char *name); /* a file of the retro/audio folder, NULL if missing */
void menu_audio_init(void);
void menu_audio_feed(void);       /* tops up the device queue; call every frame */
void menu_audio_play(int effect); /* one of the SFX_* values */
void menu_audio_set_volume(float volume);

/* The game needs /dev/dsp all to itself. */
void menu_audio_release(void);
void menu_audio_reopen(void);
