/* Platform layer: the menu only talks to this.
 * plat_sdl.c = window on the PC; plat_stick.c = framebuffer + serial on the game stick. */
#pragma once
#include <stdint.h>

/* Logical menu screen (16:9). The platform scales it to the real output. */
#define SCREEN_W 640
#define SCREEN_H 360

enum {
    BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
    BTN_A = 1 << 4, BTN_B = 1 << 5, BTN_X = 1 << 6, BTN_Y = 1 << 7,
    BTN_L = 1 << 8, BTN_R = 1 << 9, BTN_SELECT = 1 << 10, BTN_START = 1 << 11,
    BTN_QUIT = 1 << 15, /* PC only: close the window / Esc */
};

typedef struct {
    uint32_t bt[2];        /* pressed buttons, players 1 and 2 */
    int present[2];       /* 1 if the controller sent a packet recently */
    uint8_t raw[2][9];     /* last raw packet (diagnostics); zeros on the PC */
} input_t;

int plat_init(void);
void plat_shutdown(void);
void plat_present(const uint32_t *screen);  /* SCREEN_W x SCREEN_H, 0x00RRGGBB */
void plat_input(input_t *e);
uint32_t plat_ms(void);
void plat_sleep(int ms);
const char *plat_roms_dir(void);
void plat_log(const char *fmt, int v); /* short log entry (FPS etc.) */
void plat_diagnostics(void);              /* dumps serial, load and processes to the log */

/* used by the game runner (runner.c) */
void plat_present_565(const uint16_t *px, int w, int h, int pitch_px); /* core frame, 4:3 */
int plat_audio_init(int rate);              /* 0 = ok; 16-bit stereo */
void plat_audio_close(void);               /* releases audio (the menu closes it before opening the game) */
int plat_audio(const int16_t *s, int frames); /* returns how many audio frames were accepted */
int plat_audio_queue(void);                 /* audio frames still in the device queue; -1 if unknown */
/* what holds the game's pacing: PACE_VIDEO = plat_present_565 waits for the TV retrace;
 * PACE_AUDIO = plat_audio blocks; PACE_CLOCK = nothing, the runner waits on the clock */
enum { PACE_CLOCK, PACE_VIDEO, PACE_AUDIO };
int plat_pacing(void);
uint32_t plat_us(void);                    /* clock in microseconds (wraps in ~71 min) */
/* used by the menu to open the game */
int plat_launch(const char *prog, const char *game, const char *saves);
const char *plat_runner(const char *system_name); /* runner path, NULL if it does not exist */
