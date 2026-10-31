/* Shared between the runner's files: audio, saves, pause menu and what runner.c lends them. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "libretro.h"

/* One character: last letter of the state file extension and content of the .last file. */
enum { SLOT_NONE = 0, SLOT_1 = '1', SLOT_2 = '2', SLOT_AUTO = 'a' };

/* runner.c */
uint32_t runner_poll_buttons(void);                         /* polls and returns the buttons of both players */
const uint16_t *runner_last_frame(int *width, int *height); /* RGB565, NULL before the first drawn frame */

/* runner_audio.c */
void runner_audio_open(double core_sample_rate);
void runner_audio_set_rate_control(bool enabled);
void runner_audio_set_status_callback(retro_audio_buffer_status_callback_t callback);
void runner_audio_report_status(int delay_us, int consecutive_skips); /* lets the core decide on frame skip */
void runner_audio_sample(int16_t left, int16_t right);
size_t runner_audio_batch(const int16_t *samples, size_t frames);
void runner_audio_send(void); /* sends what the core produced this frame */
void runner_audio_idle(void); /* keeps the device queue fed with silence while the game is stopped */
uint32_t runner_audio_ms(void);
uint32_t runner_audio_frames_received(void);
void runner_audio_reset_frame_stats(void);
void runner_audio_log_queue_stats(void);
void runner_audio_log_level_stats_and_reset(void);

/* runner_saves.c: functions returning int give 0 on success */
void runner_saves_init(const char *rom, const char *saves_dir);
const char *runner_saves_dir(void);
void runner_sram_load(void);
void runner_sram_save(void);
int runner_save_state(char slot);
int runner_load_state(char slot);
bool runner_slot_exists(char slot);
char runner_last_slot(void); /* SLOT_NONE if nothing was saved yet */

/* runner_pause.c */
bool runner_pause_menu(void); /* true if the player chose to quit the game */
