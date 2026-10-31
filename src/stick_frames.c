/* Stick game frames: core RGB565 frames on the panel, integer scaling, retrace pacing and its statistics. */
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "stick_internal.h"

#define MAX_GAME_SCALE 4
#define LATE_FRAME_US 15000
#define PACING_WINDOW_FRAMES 300
#define SLOW_FRAME_US 20000
#define FAST_FRAME_US 10000
#define NO_INTERVAL_YET_US 999999
#define RETRACE_WAIT_MIN_US 500
#define RETRACE_WAIT_MAX_US 30000

/* 4:3 box inside the panel for core frames without an integer scale */
static struct {
    int source_w, source_h;
    int x, y, w, h;
    int *map_x;
} game_box;

static struct {
    uint32_t previous_time, frames, shortest, longest, over_20ms, under_10ms, wait_sum;
} pacing = {.shortest = NO_INTERVAL_YET_US};

static uint32_t *headless_frame; /* the last core frame becomes the saved screen */
static int vsync = 1;            /* wait for the TV retrace on each game frame (STICK_VSYNC=0 disables it) */
static uint16_t scaled_line[MAX_PANEL_W];

static void capture_headless_frame(const uint16_t *px, int w, int h, int pitch_px)
{
    if (!headless_frame) {
        headless_frame = malloc(SCREEN_W * SCREEN_H * 4);
    }
    for (int y = 0; y < SCREEN_H; y += 1) {
        for (int x = 0; x < SCREEN_W; x += 1) {
            uint16_t color = px[(y * h / SCREEN_H) * pitch_px + x * w / SCREEN_W];
            headless_frame[y * SCREEN_W + x] =
                ((color & RED_565_MASK) << 8) | ((color & GREEN_565_MASK) << 5) | ((color & BLUE_565_MASK) << 3);
        }
    }
    stick_video_show_headless_frame(headless_frame);
}

static void update_game_box(int w, int h)
{
    int panel_w = stick_video_panel_w();
    int panel_h = stick_video_panel_h();
    game_box.h = panel_h;
    game_box.w = game_box.h * 4 / 3;
    if (game_box.w > panel_w) {
        game_box.w = panel_w;
        game_box.h = game_box.w * 3 / 4;
    }
    game_box.x = (panel_w - game_box.w) / 2;
    game_box.y = (panel_h - game_box.h) / 2;
    free(game_box.map_x);
    game_box.map_x = malloc(game_box.w * sizeof(int));
    for (int x = 0; x < game_box.w; x += 1) {
        game_box.map_x[x] = x * w / game_box.w;
    }
    game_box.source_w = w;
    game_box.source_h = h;
    stick_video_clear_buffers();
}

static int game_integer_scale(int w, int h)
{
    int scale_x = stick_video_panel_w() / w;
    int scale_y = stick_video_panel_h() / h;
    int scale = scale_x < scale_y ? scale_x : scale_y;

    return scale > MAX_GAME_SCALE ? MAX_GAME_SCALE : scale;
}

static void clear_when_layout_changes(int scale, int dx, int dy)
{
    static int last_scale, last_dx, last_dy;
    if (scale != last_scale || dx != last_dx || dy != last_dy) {
        last_scale = scale;
        last_dx = dx;
        last_dy = dy;
        stick_video_clear_buffers();
    }
}

static inline void expand_line_2x(const uint16_t *src, uint32_t *dest, int w)
{
    for (int x = 0; x < w; x += 1) {
        uint32_t color = src[x];
        dest[x] = color | (color << 16);
    }
}

static inline void expand_line_3x(const uint16_t *src, uint32_t *dest, int w)
{
    for (int x = 0; x < w; x += 2) {
        uint32_t first = src[x], second = src[x + 1];
        dest[0] = first | first << 16;
        dest[1] = first | second << 16;
        dest[2] = second | second << 16;
        dest += 3;
    }
}

static inline void expand_line_4x(const uint16_t *src, uint32_t *dest, int w)
{
    for (int x = 0; x < w; x += 1) {
        uint32_t color = src[x] | (uint32_t)src[x] << 16;
        dest[x * 2] = color;
        dest[x * 2 + 1] = color;
    }
}

/* Fast path: integer scale, no table. NES/SNES/MD/SMS at 2x; GBA (240x160) and GB (160x144) at 3x
 * (the GBA fills the 720x480 exactly). Writes 2 pixels at a time (uint32) and copies the line to the other
 * scale-1. The scaled line is built in plain memory and only copied (scale times) to video memory:
 * never copy the line above from the screen itself, that memory is slow to read. */
static void draw_game_integer(const uint16_t *px, int w, int h, int pitch_px, int scale, uint16_t *base)
{
    static uint32_t line[MAX_PANEL_W / 2];
    int panel_w = stick_video_panel_w();
    int dx = ((panel_w - w * scale) / 2) & ~1;
    int dy = (stick_video_panel_h() - h * scale) / 2;
    clear_when_layout_changes(scale, dx, dy);
    for (int y = 0; y < h; y += 1) {
        const uint16_t *src = px + y * pitch_px;
        if (scale == 2) {
            expand_line_2x(src, line, w);
        } else if (scale == 3) {
            expand_line_3x(src, line, w);
        } else {
            expand_line_4x(src, line, w);
        }
        for (int repeat = 0; repeat < scale; repeat += 1) {
            memcpy(base + (dy + y * scale + repeat) * panel_w + dx, line, w * scale * BYTES_PER_PIXEL);
        }
    }
}

static void draw_game_in_box(const uint16_t *px, int h, int pitch_px, uint16_t *base)
{
    int panel_w = stick_video_panel_w();
    int previous_source_y = -1;
    for (int y = 0; y < game_box.h; y += 1) {
        int source_y = y * h / game_box.h;
        if (source_y != previous_source_y) {
            const uint16_t *src = px + source_y * pitch_px;
            for (int x = 0; x < game_box.w; x += 1) {
                scaled_line[x] = src[game_box.map_x[x]];
            }
            previous_source_y = source_y;
        }
        memcpy(base + (game_box.y + y) * panel_w + game_box.x, scaled_line, game_box.w * BYTES_PER_PIXEL);
    }
}

/* A frame that is already late (more than 15 ms since the last) does not wait for the next retrace:
 * waiting would turn 17 ms into 33 ms (30 fps); so it goes straight out (the image may tear). */
static void wait_for_game_retrace(uint32_t flip_time)
{
    static uint32_t last_wait_end;
    stick_video_request_update();
    if (!last_wait_end || (int)(flip_time - last_wait_end) < LATE_FRAME_US) {
        stick_video_wait_updated();
    }
    last_wait_end = plat_us();
}

static void report_pacing(void)
{
    uint32_t average_wait = pacing.wait_sum / PACING_WINDOW_FRAMES;
    stick_note("pacing: interval min %d max %d us", pacing.shortest, pacing.longest);
    stick_note("pacing: frames >20ms %d, <10ms %d (of 300)", pacing.over_20ms, pacing.under_10ms);
    stick_note("pacing: average retrace wait %d us (vsync %d)", average_wait, vsync);
    /* without a real retrace the wait returns at once (or hits the 50 ms timeout): fall back to the clock */
    if (vsync && (average_wait < RETRACE_WAIT_MIN_US || average_wait > RETRACE_WAIT_MAX_US)) {
        vsync = 0;
        stick_step("pacing: retrace wait does not work, pacing by clock", 0, 0);
    }
    pacing.frames = 0;
    pacing.over_20ms = 0;
    pacing.under_10ms = 0;
    pacing.longest = 0;
    pacing.wait_sum = 0;
    pacing.shortest = NO_INTERVAL_YET_US;
}

static void record_interval(uint32_t interval)
{
    if (interval < pacing.shortest) {
        pacing.shortest = interval;
    }
    if (interval > pacing.longest) {
        pacing.longest = interval;
    }
    if (interval > SLOW_FRAME_US) {
        pacing.over_20ms += 1;
    }
    if (interval < FAST_FRAME_US) {
        pacing.under_10ms += 1;
    }
    pacing.frames += 1;
    if (pacing.frames == PACING_WINDOW_FRAMES) {
        report_pacing();
    }
}

/* interval between flips (after the wait), summarized every 300 frames */
static void track_pacing(uint32_t flip_time)
{
    uint32_t now = plat_us();
    pacing.wait_sum += now - flip_time;
    if (pacing.previous_time) {
        record_interval(now - pacing.previous_time);
    }
    pacing.previous_time = now;
}

void plat_present_565(const uint16_t *px, int w, int h, int pitch_px)
{
    if (!stick_video_has_display()) {
        capture_headless_frame(px, w, h, pitch_px);

        return;
    }
    if (w != game_box.source_w || h != game_box.source_h) {
        update_game_box(w, h);
    }
    stick_video_clear_menu_leftovers();
    uint16_t *base = stick_video_next_buffer();
    int scale = game_integer_scale(w, h);
    if (scale >= 2 && !(w & 1)) {
        draw_game_integer(px, w, h, pitch_px, scale, base);
    } else {
        draw_game_in_box(px, h, pitch_px, base);
    }
    stick_video_show_game_frame();
    uint32_t flip_time = plat_us();
    if (vsync) {
        wait_for_game_retrace(flip_time);
    }
    track_pacing(flip_time);
}

void plat_disable_retrace(void)
{
    vsync = 0;
}

int plat_pacing(void)
{
    return stick_video_has_display() && vsync ? PACE_VIDEO : PACE_CLOCK;
}
