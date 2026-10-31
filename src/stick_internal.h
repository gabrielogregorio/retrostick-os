/* Shared only between plat_stick.c and the stick_*.c files. */
#pragma once
#include <stdio.h>
#include "plat.h"

/* Step marker with fsync: if the stick hangs, the last line says where.
 * ONLY for risky steps (startup): each fsync on the SD's FAT32 costs ~20 ms, so a burst of them
 * mid-game freezes the screen. */
void stick_step(const char *fmt, int first, int second);
/* Entry without fsync or fflush: stays in the 16 KB buffer and reaches the card in a block (statistics). */
void stick_note(const char *fmt, int first, int second);
FILE *stick_log_file(void); /* NULL when STICK_LOG is not set */

#define BYTES_PER_PIXEL 2
#define MAX_PANEL_W 1920
#define RED_565_MASK 0xf800
#define GREEN_565_MASK 0x07e0
#define BLUE_565_MASK 0x001f

void stick_video_init(void);
int stick_video_headless(void); /* 1 when neither /dev/disp0 nor /dev/fb0 came up */
int stick_video_in_game(void);  /* 1 after the first core frame */
void stick_video_save_shot(void);

/* The display device as stick_frames.c sees it. */
int stick_video_has_display(void);
int stick_video_panel_w(void);
int stick_video_panel_h(void);
uint16_t *stick_video_next_buffer(void); /* the back buffer of the frame being drawn */
void stick_video_clear_buffers(void);
void stick_video_clear_menu_leftovers(void); /* the menu or pause menu drew on top since the last game frame */
void stick_video_show_game_frame(void);
void stick_video_request_update(void);
void stick_video_wait_updated(void);
void stick_video_show_headless_frame(const uint32_t *frame);

void stick_input_init(void); /* needs stick_video_init first: simulated keys only exist when headless */
void stick_input_log_state(FILE *log);

/* Test seam: reads packets from fd instead of the serial port, at a time the caller controls. */
void stick_input_use_fd(int fd);
void stick_input_poll(input_t *input, uint32_t now);
