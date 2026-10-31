/* Stick video: /dev/disp0 + /dev/chunkmem device, menu frames, /dev/fb0 fallback (game frames: stick_frames.c).
 *
 * gp_fb.ko HANGS the stick, so the path used is /dev/disp0 + /dev/chunkmem buffer in RGB565, primary layer,
 * double-buffer flip. /dev/fb0 remains as an alternative (STICK_VIDEO=fb).
 *
 * With no display at all (e.g. running on the PC under qemu-arm) it enters headless mode:
 *   STICK_SHOT=file.ppm      where to save the last screen (default out/stick-shot.ppm) */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "plat.h"
#include "stick_internal.h"

/* ioctls of the gp_display ('d') and gp_chunkmem ('C') drivers */
#define DISP_GET_PANEL_RES 0x80046403 /* -> uint16 w, h */
#define DISP_SET_PRI_BITMAP 0x402c6411
#define DISP_FLIP_PRI_BITMAP 0x402c6413
#define DISP_SET_PRI_ENABLE 0x40046410
#define DISP_SET_UPDATE 0x4004640a
/* 0x6402 = disp_update: clears the "updated" flag and asks the hardware to latch the new registers.
 * 0x6407 = disp_wait_for_updated: sleeps until the frame interrupt sets "updated"
 * (timeout of 5 jiffies = 50 ms). Both together, after the flip, = wait for the TV retrace. */
#define DISP_UPDATE 0x6402
#define DISP_WAIT_UPDATED 0x6407
#define DISP_SET_OSD_ENABLE 0x40046480 /* OSD layer: it can sit on top of the primary layer, so it is turned off */
#define CHUNK_ALLOC 0xc00c4301

#define BUFFER_COUNT 2
#define BITMAP_TYPE_RGB565 0

#define MENU_TIMING_FRAMES 30

#define CLOCK_TEST_SLEEPS 10
#define CLOCK_TEST_FLIPS 120
#define US_PER_SECOND 1000000
#define NS_PER_US 1000

typedef struct {
    uint16_t w, h, bpl;
    uint8_t type, pad;
    void *data;
    uint8_t rest[32];
} disp_bitmap_t; /* 44 bytes */

typedef struct {
    uint32_t phys;
    void *addr;
    uint32_t size;
} chunk_t;

typedef struct {
    uint16_t w, h;
} panel_size_t;

_Static_assert(sizeof(disp_bitmap_t) == 44, "bitmap struct must be 44 bytes (0x2c of the ioctl)");
_Static_assert(sizeof(chunk_t) == 12, "chunkmem struct must be 12 bytes (0x0c of the ioctl)");

static struct {
    int fd;
    int current_buffer;
    disp_bitmap_t buffers[BUFFER_COUNT];
    panel_size_t panel;
} display = {.fd = -1};

static struct {
    int fd;
    uint8_t *memory;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
} fallback = {.fd = -1};

/* the menu fills the output */
static struct {
    int w, h;
    int *map_x;
} menu_area;

static const uint32_t *last_screen;
static int frame_count;
static int borders_dirty; /* plat_present drew on top (pause menu): the next game frame clears the borders */
static int in_game;       /* 1 after the first core frame (plat_present_565) */
static uint16_t scaled_line[MAX_PANEL_W];

static uint32_t frame_bytes(void)
{
    return (uint32_t)display.panel.w * display.panel.h * BYTES_PER_PIXEL;
}

void stick_video_clear_buffers(void)
{
    for (int buffer_index = 0; buffer_index < BUFFER_COUNT; buffer_index += 1) {
        memset(display.buffers[buffer_index].data, 0, frame_bytes());
    }
}

static void wait_retrace(void)
{
    ioctl(display.fd, DISP_UPDATE, 0);
    ioctl(display.fd, DISP_WAIT_UPDATED, 0);
}

static long elapsed_us(const struct timespec *start, const struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * US_PER_SECOND + (end->tv_nsec - start->tv_nsec) / NS_PER_US;
}

static void measure_usleep(int requested_us)
{
    long sum = 0;
    for (int sleep_index = 0; sleep_index < CLOCK_TEST_SLEEPS; sleep_index += 1) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        usleep(requested_us);
        clock_gettime(CLOCK_MONOTONIC, &end);
        sum += elapsed_us(&start, &end);
    }
    stick_step("clock: usleep(%d us) lasts on average %d us", requested_us, (int)(sum / CLOCK_TEST_SLEEPS));
}

/* retrace: 120 flips with wait; at 60 Hz that gives ~2000000 us, at 59.94 Hz ~2002000 */
static void measure_retrace(void)
{
    struct timespec start, end;
    wait_retrace();
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int flip_index = 0; flip_index < CLOCK_TEST_FLIPS; flip_index += 1) {
        ioctl(display.fd, DISP_FLIP_PRI_BITMAP, &display.buffers[flip_index & 1]);
        wait_retrace();
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    stick_step("clock: 120 flips with retrace wait took %d us", (int)elapsed_us(&start, &end), 0);
}

static void run_clock_test(void)
{
    static const int REQUESTED_US[] = {1000, 5000, 16000};
    for (unsigned request_index = 0; request_index < sizeof REQUESTED_US / sizeof REQUESTED_US[0]; request_index += 1) {
        measure_usleep(REQUESTED_US[request_index]);
    }
    measure_retrace();
}

static int alloc_buffers(void)
{
    int chunk_fd = open("/dev/chunkmem", O_RDWR);
    chunk_t chunk = {0, 0, frame_bytes() * BUFFER_COUNT};
    if (chunk_fd < 0 || ioctl(chunk_fd, CHUNK_ALLOC, &chunk) || !chunk.addr) {
        stick_step("disp: CHUNK_ALLOC failed", 0, 0);

        return -1;
    }
    stick_step("disp: chunkmem ok, %d bytes", chunk.size, 0);
    for (int buffer_index = 0; buffer_index < BUFFER_COUNT; buffer_index += 1) {
        disp_bitmap_t *bitmap = &display.buffers[buffer_index];
        memset(bitmap, 0, sizeof *bitmap);
        bitmap->w = display.panel.w;
        bitmap->h = display.panel.h;
        bitmap->bpl = display.panel.w * BYTES_PER_PIXEL;
        bitmap->type = BITMAP_TYPE_RGB565;
        bitmap->data = (uint8_t *)chunk.addr + buffer_index * frame_bytes();
    }
    memset(chunk.addr, 0, chunk.size);

    return 0;
}

static int open_disp(void)
{
    stick_step("disp: opening /dev/disp0", 0, 0);
    display.fd = open("/dev/disp0", O_RDWR);
    if (display.fd < 0) {
        return -1;
    }
    if (ioctl(display.fd, DISP_GET_PANEL_RES, &display.panel)) {
        stick_step("disp: GET_PANEL_RES failed", 0, 0);

        return -1;
    }
    stick_step("disp: panel %dx%d", display.panel.w, display.panel.h);
    if (alloc_buffers()) {
        return -1;
    }
    stick_step("disp: SET_PRI_BITMAP", 0, 0);
    ioctl(display.fd, DISP_SET_PRI_BITMAP, &display.buffers[0]);
    ioctl(display.fd, DISP_SET_PRI_ENABLE, 1);
    ioctl(display.fd, DISP_SET_OSD_ENABLE, 0);
    ioctl(display.fd, DISP_SET_UPDATE, 1);
    stick_step("disp: ready (OSD off)", 0, 0);
    const char *vsync_setting = getenv("STICK_VSYNC");
    if (vsync_setting && *vsync_setting == '0') {
        plat_disable_retrace();
    }
    if (getenv("STICK_CLOCK_TEST")) {
        run_clock_test();
    }

    return 0;
}

/* Full-screen menu (720x405 would leave black bars): stretches to the whole output.
 * On a 16:9 TV 720x480 is anamorphic, so the menu's 16:9 comes out in the right proportion. */
static void compute_menu_area(int output_w, int output_h)
{
    menu_area.w = output_w;
    menu_area.h = output_h;
    menu_area.map_x = malloc(menu_area.w * sizeof(int));
    for (int x = 0; x < menu_area.w; x += 1) {
        menu_area.map_x[x] = x * SCREEN_W / menu_area.w;
    }
}

static void open_fb0(void)
{
    fallback.fd = open("/dev/fb0", O_RDWR);
    if (fallback.fd >= 0 && !ioctl(fallback.fd, FBIOGET_VSCREENINFO, &fallback.var) &&
        !ioctl(fallback.fd, FBIOGET_FSCREENINFO, &fallback.fix)) {
        fallback.memory = mmap(0, fallback.fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fallback.fd, 0);
        if (fallback.memory == MAP_FAILED) {
            fallback.memory = 0;
        }
    }
    if (!fallback.memory) {
        return;
    }
    compute_menu_area(fallback.var.xres, fallback.var.yres);
    memset(fallback.memory, 0, fallback.fix.smem_len);
    fprintf(stderr, "fb0: %dx%d %dbpp line=%d\n", fallback.var.xres, fallback.var.yres, fallback.var.bits_per_pixel,
            fallback.fix.line_length);
}

static void log_fb0_geometry(void)
{
    FILE *log = stick_log_file();
    if (log && fallback.memory) {
        fprintf(log, "fb0: %dx%d virtual %dx%d %dbpp line=%d smem=%d\n", fallback.var.xres, fallback.var.yres,
                fallback.var.xres_virtual, fallback.var.yres_virtual, fallback.var.bits_per_pixel,
                fallback.fix.line_length, fallback.fix.smem_len);
    }
}

void stick_video_init(void)
{
    const char *video = getenv("STICK_VIDEO") ? getenv("STICK_VIDEO") : "disp";
    if (!strcmp(video, "disp")) {
        if (open_disp()) {
            display.fd = -1;
            stick_step("disp: failed, continuing without display", 0, 0);
        } else {
            compute_menu_area(display.panel.w, display.panel.h);
        }
    } else if (!strcmp(video, "fb")) {
        open_fb0();
    }
    if (stick_video_headless()) {
        fprintf(stderr, "no display: headless mode\n");
    }
    log_fb0_geometry();
}

int stick_video_headless(void)
{
    return !fallback.memory && display.fd < 0;
}

int stick_video_in_game(void)
{
    return in_game;
}

void stick_video_save_shot(void)
{
    if (!last_screen) {
        return;
    }
    const char *path = getenv("STICK_SHOT") ? getenv("STICK_SHOT") : "out/stick-shot.ppm";
    FILE *file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int pixel_index = 0; pixel_index < SCREEN_W * SCREEN_H; pixel_index += 1) {
        uint32_t color = last_screen[pixel_index];
        uint8_t rgb[3] = {color >> 16, color >> 8, color};
        fwrite(rgb, 1, sizeof rgb, file);
    }
    fclose(file);
    fprintf(stderr, "screen saved to %s\n", path);
}

static inline uint32_t rgb888_to_565(uint32_t color)
{
    return ((color >> 8) & RED_565_MASK) | ((color >> 5) & GREEN_565_MASK) | ((color >> 3) & BLUE_565_MASK);
}

static inline uint32_t two_pixels_565(uint32_t first, uint32_t second)
{
    return rgb888_to_565(first) | rgb888_to_565(second) << 16;
}

/* 1:1: converts 2 pixels at a time, no table */
static void draw_menu_unscaled(const uint32_t *screen, uint16_t *base)
{
    for (int y = 0; y < menu_area.h; y += 1) {
        const uint32_t *src = screen + y * SCREEN_W;
        uint32_t *dest = (uint32_t *)(base + y * display.panel.w);
        for (int x = 0; x < SCREEN_W; x += 2) {
            dest[x >> 1] = two_pixels_565(src[x], src[x + 1]);
        }
    }
}

/* Stretched: each source line becomes RGB565 once (2 pixels at a time) and is spread across the width
 * in a line in plain memory; video memory only receives copies (burst memcpy). Never copy a line
 * from the screen itself: video memory is very slow to read. */
static void draw_menu_stretched(const uint32_t *screen, uint16_t *base)
{
    static uint16_t source_565[SCREEN_W];
    int previous_source_y = -1;
    for (int y = 0; y < menu_area.h; y += 1) {
        int source_y = y * SCREEN_H / menu_area.h;
        if (source_y != previous_source_y) {
            const uint32_t *src = screen + source_y * SCREEN_W;
            uint32_t *pairs = (uint32_t *)source_565;
            for (int x = 0; x < SCREEN_W; x += 2) {
                pairs[x >> 1] = two_pixels_565(src[x], src[x + 1]);
            }
            for (int x = 0; x < menu_area.w; x += 1) {
                scaled_line[x] = source_565[menu_area.map_x[x]];
            }
            previous_source_y = source_y;
        }
        memcpy(base + y * display.panel.w, scaled_line, menu_area.w * BYTES_PER_PIXEL);
    }
}

static void note_menu_timing(uint32_t convert_ms, uint32_t flip_ms)
{
    static uint32_t convert_sum, flip_sum, frames;
    convert_sum += convert_ms;
    flip_sum += flip_ms;
    frames += 1;
    if (frames == MENU_TIMING_FRAMES) {
        stick_note("ms per frame: convert %d, flip buffer %d", convert_sum / MENU_TIMING_FRAMES,
                   flip_sum / MENU_TIMING_FRAMES);
        convert_sum = 0;
        flip_sum = 0;
        frames = 0;
    }
}

static void present_menu_on_display(const uint32_t *screen)
{
    uint32_t start = plat_ms();
    display.current_buffer ^= 1;
    uint16_t *base = display.buffers[display.current_buffer].data;
    if (menu_area.w == SCREEN_W && menu_area.h == SCREEN_H) {
        draw_menu_unscaled(screen, base);
    } else {
        draw_menu_stretched(screen, base);
    }
    uint32_t converted = plat_ms();
    ioctl(display.fd, DISP_FLIP_PRI_BITMAP, &display.buffers[display.current_buffer]);
    ioctl(display.fd, DISP_UPDATE, 0);
    uint32_t flipped = plat_ms();
    note_menu_timing(converted - start, flipped - converted);
    ioctl(display.fd, DISP_SET_PRI_ENABLE, 1);
    if (frame_count == 1) {
        ioctl(display.fd, DISP_SET_OSD_ENABLE, 0);
        stick_step("disp: first frame shown", 0, 0);
    }
}

static void present_menu_on_fb0(const uint32_t *screen)
{
    for (int y = 0; y < menu_area.h; y += 1) {
        const uint32_t *src = screen + (y * SCREEN_H / menu_area.h) * SCREEN_W;
        uint8_t *line = fallback.memory + y * fallback.fix.line_length;
        if (fallback.var.bits_per_pixel == 16) {
            uint16_t *dest = (uint16_t *)line;
            for (int x = 0; x < menu_area.w; x += 1) {
                dest[x] = rgb888_to_565(src[menu_area.map_x[x]]);
            }
        } else {
            uint32_t *dest = (uint32_t *)line;
            for (int x = 0; x < menu_area.w; x += 1) {
                dest[x] = src[menu_area.map_x[x]];
            }
        }
    }
}

void plat_present(const uint32_t *screen)
{
    last_screen = screen;
    borders_dirty = 1;
    frame_count += 1;
    if (display.fd >= 0) {
        present_menu_on_display(screen);

        return;
    }
    if (fallback.memory) {
        present_menu_on_fb0(screen);
    }
}

int stick_video_has_display(void)
{
    return display.fd >= 0;
}

int stick_video_panel_w(void)
{
    return display.panel.w;
}

int stick_video_panel_h(void)
{
    return display.panel.h;
}

uint16_t *stick_video_next_buffer(void)
{
    display.current_buffer ^= 1;

    return display.buffers[display.current_buffer].data;
}

void stick_video_clear_menu_leftovers(void)
{
    if (borders_dirty) {
        stick_video_clear_buffers();
        borders_dirty = 0;
    }
}

void stick_video_show_game_frame(void)
{
    ioctl(display.fd, DISP_FLIP_PRI_BITMAP, &display.buffers[display.current_buffer]);
    ioctl(display.fd, DISP_SET_PRI_ENABLE, 1);
    frame_count += 1;
    if (frame_count == 1) {
        ioctl(display.fd, DISP_SET_OSD_ENABLE, 0);
        stick_step("game: first frame", 0, 0);
        in_game = 1;
    }
}

void stick_video_request_update(void)
{
    ioctl(display.fd, DISP_UPDATE, 0);
}

void stick_video_wait_updated(void)
{
    ioctl(display.fd, DISP_WAIT_UPDATED, 0);
}

void stick_video_show_headless_frame(const uint32_t *frame)
{
    frame_count += 1;
    last_screen = frame;
}

void plat_reactivate(void)
{
    if (display.fd < 0) {
        return;
    }
    ioctl(display.fd, DISP_SET_PRI_BITMAP, &display.buffers[display.current_buffer]);
    ioctl(display.fd, DISP_SET_PRI_ENABLE, 1);
    ioctl(display.fd, DISP_SET_OSD_ENABLE, 0);
}
