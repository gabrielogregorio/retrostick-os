/* Game stick platform (GeneralPlus GPL32900B): /dev/fb0 + receiver on /dev/ttyS1.
 *
 * Receiver: 115200 8N1, 9-byte packet
 *   0A 09 PP xx xx xx B6 B7 CK   PP 02 = player 1, 03 = player 2
 *   CK = ~(XOR(bytes 0..7) ^ 0x5A)
 * The B6/B7 bit map is the BUTTON_MAP table below; the diagnostics screen
 * (SELECT) shows the raw packet for checking.
 *
 * Video: gp_fb.ko HANGS the stick, so the path used is /dev/disp0 + /dev/chunkmem buffer in RGB565,
 * primary layer, double-buffer flip. /dev/fb0 remains as an alternative (STICK_VIDEO=fb).
 *
 * With no display at all (e.g. running on the PC under qemu-arm) it enters headless mode:
 *   STICK_SHOT=file.ppm      where to save the last screen (default out/stick-shot.ppm)
 *   STICK_KEYS=D,D,R,A,S     simulated keys, one every 15 frames, then exits
 *     (U D L R = directions, A B, S = select, T = start) */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <linux/soundcard.h>
#include "plat.h"

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
#define AUDIO_OUTPUT_HDMI 0xc0045007 /* gp_audio: 1 = HDMI I2S, 0 = analog output (default) */
typedef struct { uint16_t w, h, bpl; uint8_t type, pad; void *data; uint8_t rest[32]; } disp_bitmap_t; /* 44 bytes */
typedef struct { uint32_t phys; void *addr; uint32_t size; } chunk_t;
_Static_assert(sizeof(disp_bitmap_t) == 44, "bitmap struct must be 44 bytes (0x2c of the ioctl)");
_Static_assert(sizeof(chunk_t) == 12, "chunkmem struct must be 12 bytes (0x0c of the ioctl)");

static int disp = -1, chunk = -1, cur;
static disp_bitmap_t bm[2];
static uint16_t res[2];

static int fb = -1, ser = -1;
static uint8_t *fbmem;
static struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;
static int ox, oy, sw, sh;        /* usable area (letterbox) inside the fb */
static int *map_x;
static const uint32_t *last_screen;
static const char *sim_keys;
static int frame_count;
static int in_game; /* 1 after the first core frame (plat_present_565) */
static int vsync = 1; /* wait for the TV retrace on each game frame (STICK_VSYNC=0 disables it) */

/* B6 bit (0..7) and B7 bit (8..12) -> button.
 * L2 and R2 arrive on the SAME bits as L1 and R1 (the receiver does not tell them apart).
 * B7.4 has no identified button: unused. */
static const uint32_t BUTTON_MAP[13] = {
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_Y, BTN_X, /* B6.0 .. B6.7 */
    BTN_START, BTN_SELECT, BTN_L, BTN_R, 0,                        /* B7.0 .. B7.4 */
};

static struct { uint8_t buf[256]; int n; uint32_t seen[2]; } rx;
static FILE *logf;

/* step marker with fsync: if the stick hangs, the last line says where.
 * ONLY for risky steps (startup): each fsync on the SD's FAT32 costs ~20 ms, so a burst of them
 * mid-game freezes the screen. */
static void step(const char *fmt, int a, int b)
{
    if (!logf) return;
    fprintf(logf, "step: ");
    fprintf(logf, fmt, a, b);
    fprintf(logf, "\n");
    fflush(logf);
    fsync(fileno(logf));
}

/* entry without fsync or fflush: stays in the 16 KB buffer and reaches the card in a block (statistics) */
static void note(const char *fmt, int a, int b)
{
    if (!logf) return;
    fprintf(logf, "step: ");
    fprintf(logf, fmt, a, b);
    fprintf(logf, "\n");
}

static int open_disp(void)
{
    step("disp: opening /dev/disp0", 0, 0);
    disp = open("/dev/disp0", O_RDWR);
    if (disp < 0) return -1;
    if (ioctl(disp, DISP_GET_PANEL_RES, res)) { step("disp: GET_PANEL_RES failed", 0, 0); return -1; }
    step("disp: panel %dx%d", res[0], res[1]);
    chunk = open("/dev/chunkmem", O_RDWR);
    chunk_t c = {0, 0, (uint32_t)res[0] * res[1] * 2 * 2};
    if (chunk < 0 || ioctl(chunk, CHUNK_ALLOC, &c) || !c.addr) { step("disp: CHUNK_ALLOC failed", 0, 0); return -1; }
    step("disp: chunkmem ok, %d bytes", c.size, 0);
    for (int i = 0; i < 2; i++) {
        memset(&bm[i], 0, sizeof bm[i]);
        bm[i].w = res[0]; bm[i].h = res[1]; bm[i].bpl = res[0] * 2; bm[i].type = 0; /* RGB565 */
        bm[i].data = (uint8_t *)c.addr + i * res[0] * res[1] * 2;
    }
    memset(c.addr, 0, c.size);
    step("disp: SET_PRI_BITMAP", 0, 0);
    ioctl(disp, DISP_SET_PRI_BITMAP, &bm[0]);
    ioctl(disp, DISP_SET_PRI_ENABLE, 1);
    ioctl(disp, DISP_SET_OSD_ENABLE, 0);
    ioctl(disp, DISP_SET_UPDATE, 1);
    step("disp: ready (OSD off)", 0, 0);
    const char *v = getenv("STICK_VSYNC");
    if (v && *v == '0') vsync = 0;
    if (getenv("STICK_CLOCK_TEST")) {
        struct timespec a, b;
        int requested[3] = {1000, 5000, 16000};
        for (int i = 0; i < 3; i++) {
            long sum = 0;
            for (int k = 0; k < 10; k++) {
                clock_gettime(CLOCK_MONOTONIC, &a);
                usleep(requested[i]);
                clock_gettime(CLOCK_MONOTONIC, &b);
                sum += (b.tv_sec - a.tv_sec) * 1000000 + (b.tv_nsec - a.tv_nsec) / 1000;
            }
            step("clock: usleep(%d us) lasts on average %d us", requested[i], (int)(sum / 10));
        }
        /* retrace: 120 flips with wait; at 60 Hz that gives ~2000000 us, at 59.94 Hz ~2002000 */
        ioctl(disp, DISP_UPDATE, 0);
        ioctl(disp, DISP_WAIT_UPDATED, 0);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (int k = 0; k < 120; k++) {
            ioctl(disp, DISP_FLIP_PRI_BITMAP, &bm[k & 1]);
            ioctl(disp, DISP_UPDATE, 0);
            ioctl(disp, DISP_WAIT_UPDATED, 0);
        }
        clock_gettime(CLOCK_MONOTONIC, &b);
        step("clock: 120 flips with retrace wait took %d us", (int)((b.tv_sec - a.tv_sec) * 1000000 + (b.tv_nsec - a.tv_nsec) / 1000), 0);
    }
    return 0;
}
static long log_bytes;

/* logs to STICK_LOG each packet that CHANGED (button map and controller 2) */
static void log_packet(int j, const uint8_t *p, uint32_t now)
{
    if (!logf || log_bytes > 400000 || in_game) return; /* in game, no writing to the card on each button */
    log_bytes += fprintf(logf, "t=%u P%d %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", now, j + 1, p[0], p[1],
                         p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
}

static void compute_area(int W, int H)
{
    sw = W; sh = W * SCREEN_H / SCREEN_W;
    if (sh > H) { sh = H; sw = H * SCREEN_W / SCREEN_H; }
    /* fits without stretching? goes out 1:1 (fewer pixels to write and direct conversion, no table) */
    if (W >= SCREEN_W && H >= SCREEN_H) { sw = SCREEN_W; sh = SCREEN_H; }
    ox = ((W - sw) / 2) & ~1; oy = (H - sh) / 2; /* even ox: the conversion writes 2 pixels at a time */
    map_x = malloc(sw * sizeof(int));
    for (int x = 0; x < sw; x++) map_x[x] = x * SCREEN_W / sw;
}

int plat_init(void)
{
    if (getenv("STICK_LOG")) logf = fopen(getenv("STICK_LOG"), "a");
    if (logf) setvbuf(logf, NULL, _IOFBF, 16384);
    const char *video = getenv("STICK_VIDEO") ? getenv("STICK_VIDEO") : "disp";
    if (!strcmp(video, "disp")) {
        if (open_disp()) { disp = -1; step("disp: failed, continuing without display", 0, 0); }
        else compute_area(res[0], res[1]);
    } else if (!strcmp(video, "fb")) {
        fb = open("/dev/fb0", O_RDWR);
        if (fb >= 0 && !ioctl(fb, FBIOGET_VSCREENINFO, &vi) && !ioctl(fb, FBIOGET_FSCREENINFO, &fi)) {
            fbmem = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
            if (fbmem == MAP_FAILED) fbmem = 0;
        }
        if (fbmem) {
            compute_area(vi.xres, vi.yres);
            memset(fbmem, 0, fi.smem_len);
            fprintf(stderr, "fb0: %dx%d %dbpp line=%d\n", vi.xres, vi.yres, vi.bits_per_pixel, fi.line_length);
        }
    }
    if (disp < 0 && !fbmem) {
        sim_keys = getenv("STICK_KEYS");
        if (!sim_keys) sim_keys = "";
        fprintf(stderr, "no display: headless mode\n");
    }

    if (logf && fbmem)
        fprintf(logf, "fb0: %dx%d virtual %dx%d %dbpp line=%d smem=%d\n", vi.xres, vi.yres, vi.xres_virtual,
                vi.yres_virtual, vi.bits_per_pixel, fi.line_length, fi.smem_len);
    ser = open(getenv("STICK_SERIAL") ? getenv("STICK_SERIAL") : "/dev/ttyS1", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ser >= 0) {
        struct termios t;
        tcgetattr(ser, &t);
        cfmakeraw(&t);
        cfsetispeed(&t, B115200);
        cfsetospeed(&t, B115200);
        t.c_cflag |= CLOCAL | CREAD;
        tcsetattr(ser, TCSANOW, &t);
    }
    return 0;
}

static void save_ppm(void)
{
    if (!last_screen) return;
    const char *path = getenv("STICK_SHOT") ? getenv("STICK_SHOT") : "out/stick-shot.ppm";
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint8_t p[3] = {last_screen[i] >> 16, last_screen[i] >> 8, last_screen[i]};
        fwrite(p, 1, 3, f);
    }
    fclose(f);
    fprintf(stderr, "screen saved to %s\n", path);
}

void plat_shutdown(void)
{
    if (logf) { fflush(logf); fsync(fileno(logf)); }
    if (!fbmem && disp < 0) save_ppm();
}

void plat_present(const uint32_t *t)
{
    last_screen = t;
    frame_count++;
    if (disp >= 0) {
        uint32_t t0 = plat_ms();
        cur ^= 1;
        uint16_t *base = bm[cur].data;
        if (sw == SCREEN_W && sh == SCREEN_H) {
            /* 1:1: converts 2 pixels at a time, no table */
            for (int y = 0; y < sh; y++) {
                const uint32_t *src = t + y * SCREEN_W;
                uint32_t *d = (uint32_t *)(base + (oy + y) * res[0] + ox);
                for (int x = 0; x < SCREEN_W; x += 2) {
                    uint32_t a = src[x], b = src[x + 1];
                    d[x >> 1] = (((a >> 8) & 0xf800) | ((a >> 5) & 0x07e0) | ((a >> 3) & 0x001f)) |
                                (((b >> 8) & 0xf800) | ((b >> 5) & 0x07e0) | ((b >> 3) & 0x001f)) << 16;
                }
            }
        } else
        for (int y = 0; y < sh; y++) {
            const uint32_t *src = t + (y * SCREEN_H / sh) * SCREEN_W;
            uint16_t *d = base + (oy + y) * res[0] + ox;
            for (int x = 0; x < sw; x++) {
                uint32_t c = src[map_x[x]];
                d[x] = ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f);
            }
        }
        uint32_t t1 = plat_ms();
        ioctl(disp, DISP_FLIP_PRI_BITMAP, &bm[cur]);
        ioctl(disp, DISP_UPDATE, 0);
        uint32_t t2 = plat_ms();
        static uint32_t sum_conv, sum_flip, n;
        sum_conv += t1 - t0; sum_flip += t2 - t1;
        if (++n == 30) { note("ms per frame: convert %d, flip buffer %d", sum_conv / 30, sum_flip / 30); sum_conv = sum_flip = n = 0; }
        ioctl(disp, DISP_SET_PRI_ENABLE, 1);
        if (frame_count == 1) ioctl(disp, DISP_SET_OSD_ENABLE, 0);
        if (frame_count == 1) step("disp: first frame shown", 0, 0);
        return;
    }
    if (!fbmem) return;
    for (int y = 0; y < sh; y++) {
        const uint32_t *src = t + (y * SCREEN_H / sh) * SCREEN_W;
        uint8_t *line = fbmem + (oy + y) * fi.line_length;
        if (vi.bits_per_pixel == 16) {
            uint16_t *d = (uint16_t *)line + ox;
            for (int x = 0; x < sw; x++) {
                uint32_t c = src[map_x[x]];
                d[x] = ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f);
            }
        } else {
            uint32_t *d = (uint32_t *)line + ox;
            for (int x = 0; x < sw; x++) d[x] = src[map_x[x]];
        }
    }
}

static void read_serial(input_t *e, uint32_t now)
{
    static uint32_t bt[2];
    static uint8_t raw[2][9];
    if (ser >= 0) {
        int n = read(ser, rx.buf + rx.n, sizeof rx.buf - rx.n);
        if (n > 0) rx.n += n;
    }
    int i = 0;
    while (rx.n - i >= 9) {
        uint8_t *p = rx.buf + i;
        if (p[0] != 0x0a || p[1] != 0x09) { i++; continue; }
        uint8_t x = 0;
        for (int k = 0; k < 8; k++) x ^= p[k];
        if ((uint8_t)~(x ^ 0x5a) != p[8] || (p[2] != 2 && p[2] != 3)) { i++; continue; }
        int j = p[2] - 2;
        uint32_t m = 0;
        for (int b = 0; b < 8; b++) if (p[6] & (1 << b)) m |= BUTTON_MAP[b];
        for (int b = 0; b < 5; b++) if (p[7] & (1 << b)) m |= BUTTON_MAP[8 + b];
        bt[j] = m;
        /* logs the first 30 of each player and, after that, whenever any byte from 3 to 7
         * changes (buttons and possible analog values); the 400 KB cap holds back noise */
        static int counted[2];
        if (counted[j] < 30 || memcmp(raw[j] + 3, p + 3, 5)) { log_packet(j, p, now); counted[j]++; }
        memcpy(raw[j], p, 9);
        rx.seen[j] = now;
        i += 9;
    }
    memmove(rx.buf, rx.buf + i, rx.n - i);
    rx.n -= i;
    for (int j = 0; j < 2; j++) {
        e->present[j] = rx.seen[j] && now - rx.seen[j] < 500;
        e->bt[j] = e->present[j] ? bt[j] : 0;
        memcpy(e->raw[j], raw[j], 9);
    }
}

static uint32_t simulated_key(void)
{
    int idx = frame_count / 15, mid = frame_count % 15;
    const char *p = sim_keys;
    for (int k = 0; *p && k < idx; p++)
        if (*p == ',') k++;
    if (!*p) {
        int n = 1;
        for (const char *q = sim_keys; *q; q++) n += *q == ',';
        return frame_count > n * 15 + 30 ? BTN_QUIT : 0;
    }
    if (mid > 3) return 0; /* release between keys */
    switch (*p) {
    case 'U': return BTN_UP;
    case 'D': return BTN_DOWN;
    case 'L': return BTN_LEFT;
    case 'R': return BTN_RIGHT;
    case 'A': return BTN_A;
    case 'B': return BTN_B;
    case 'S': return BTN_SELECT;
    case 'T': return BTN_START;
    }
    return 0;
}

void plat_input(input_t *e)
{
    memset(e, 0, sizeof *e);
    read_serial(e, plat_ms());
    if (!fbmem && disp < 0 && sim_keys) { e->bt[0] |= simulated_key(); e->present[0] = 1; }
}

uint32_t plat_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000u + t.tv_nsec / 1000;
}

uint32_t plat_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void plat_sleep(int ms) { usleep(ms * 1000); }

const char *plat_roms_dir(void)
{
    const char *d = getenv("STICK_ROMS");
    return d ? d : "/usr/retro/roms";
}

void plat_log(const char *fmt, int v) { note(fmt, v, 0); }

/* ---------- core frames (RGB565 -> RGB565, 4:3 box inside the panel) ---------- */
static int *mx565, mw565, mh565, ex, ey, ew, eh;

static uint32_t *test_frame; /* headless mode: the last core frame becomes the saved screen */

void plat_present_565(const uint16_t *px, int w, int h, int pitch_px)
{
    if (disp < 0) {
        frame_count++;
        if (!test_frame) test_frame = malloc(SCREEN_W * SCREEN_H * 4);
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++) {
                uint16_t c = px[(y * h / SCREEN_H) * pitch_px + x * w / SCREEN_W];
                test_frame[y * SCREEN_W + x] = ((c & 0xf800) << 8) | ((c & 0x07e0) << 5) | ((c & 0x1f) << 3);
            }
        last_screen = test_frame;
        return;
    }
    if (w != mw565 || h != mh565) {
        eh = res[1]; ew = eh * 4 / 3;
        if (ew > res[0]) { ew = res[0]; eh = ew * 3 / 4; }
        ex = (res[0] - ew) / 2; ey = (res[1] - eh) / 2;
        free(mx565);
        mx565 = malloc(ew * sizeof(int));
        for (int x = 0; x < ew; x++) mx565[x] = x * w / ew;
        mw565 = w; mh565 = h;
        memset(bm[0].data, 0, res[0] * res[1] * 2);
        memset(bm[1].data, 0, res[0] * res[1] * 2);
    }
    cur ^= 1;
    uint16_t *base = bm[cur].data;
    /* fast path: integer scale, no table. NES/SNES/MD/SMS at 2x; GBA (240x160) and GB (160x144) at 3x
     * (the GBA fills the 720x480 exactly). Writes 2 pixels at a time (uint32) and copies the line to the other k-1. */
    int k = res[0] / w < res[1] / h ? res[0] / w : res[1] / h;
    if (k > 4) k = 4;
    if (k >= 2 && !(w & 1)) {
        static int last_k, last_dx, last_dy;
        int dx = ((res[0] - w * k) / 2) & ~1, dy = (res[1] - h * k) / 2;
        if (k != last_k || dx != last_dx || dy != last_dy) {
            last_k = k; last_dx = dx; last_dy = dy;
            memset(bm[0].data, 0, res[0] * res[1] * 2);
            memset(bm[1].data, 0, res[0] * res[1] * 2);
        }
        for (int y = 0; y < h; y++) {
            const uint16_t *src = px + y * pitch_px;
            uint32_t *d = (uint32_t *)(base + (dy + y * k) * res[0] + dx);
            if (k == 2) {
                for (int x = 0; x < w; x++) { uint32_t c = src[x]; d[x] = c | (c << 16); }
            } else if (k == 3) {
                for (int x = 0; x < w; x += 2) {
                    uint32_t a = src[x], b = src[x + 1];
                    d[0] = a | a << 16; d[1] = a | b << 16; d[2] = b | b << 16;
                    d += 3;
                }
                d = (uint32_t *)(base + (dy + y * k) * res[0] + dx);
            } else {
                for (int x = 0; x < w; x++) { uint32_t c = src[x] | (uint32_t)src[x] << 16; d[x * 2] = c; d[x * 2 + 1] = c; }
            }
            for (int r = 1; r < k; r++) memcpy(base + (dy + y * k + r) * res[0] + dx, d, w * k * 2);
        }
        goto flip;
    }
    int prev = -1;
    for (int y = 0; y < eh; y++) {
        int sy = y * h / eh;
        uint16_t *d = base + (ey + y) * res[0] + ex;
        if (sy == prev) { memcpy(d, d - res[0], ew * 2); continue; }
        const uint16_t *src = px + sy * pitch_px;
        for (int x = 0; x < ew; x++) d[x] = src[mx565[x]];
        prev = sy;
    }
flip:
    ioctl(disp, DISP_FLIP_PRI_BITMAP, &bm[cur]);
    ioctl(disp, DISP_SET_PRI_ENABLE, 1);
    if (++frame_count == 1) { ioctl(disp, DISP_SET_OSD_ENABLE, 0); step("game: first frame", 0, 0); in_game = 1; }
    uint32_t t0 = plat_us();
    static uint32_t t_last; /* end of the last retrace wait */
    if (vsync) {
        ioctl(disp, DISP_UPDATE, 0);
        /* a frame that is already late (more than 15 ms since the last) does not wait for the next retrace:
         * waiting would turn 17 ms into 33 ms (30 fps); so it goes straight out (the image may tear) */
        if (!t_last || (int)(t0 - t_last) < 15000) ioctl(disp, DISP_WAIT_UPDATED, 0);
        t_last = plat_us();
    }
    {
        /* pacing: interval between flips (after the wait), summarized every 300 frames */
        static uint32_t t_prev, n, mn = 999999, mx, over20, under10, wait_sum;
        uint32_t t = plat_us();
        wait_sum += t - t0;
        if (t_prev) {
            uint32_t d = t - t_prev;
            if (d < mn) mn = d;
            if (d > mx) mx = d;
            if (d > 20000) over20++;
            if (d < 10000) under10++;
            if (++n == 300) {
                note("pacing: interval min %d max %d us", mn, mx);
                note("pacing: frames >20ms %d, <10ms %d (of 300)", over20, under10);
                note("pacing: average retrace wait %d us (vsync %d)", wait_sum / 300, vsync);
                /* without a real retrace the wait returns at once (or hits the 50 ms timeout): fall back to the clock */
                if (vsync && (wait_sum / 300 < 500 || wait_sum / 300 > 30000)) {
                    vsync = 0;
                    step("pacing: retrace wait does not work, pacing by clock", 0, 0);
                }
                n = over20 = under10 = mx = wait_sum = 0; mn = 999999;
            }
        }
        t_prev = t;
    }
}

void plat_disable_retrace(void) { vsync = 0; }
int plat_pacing(void) { return disp >= 0 && vsync ? PACE_VIDEO : PACE_CLOCK; }

/* ---------- audio: OSS on /dev/dsp, NON-blocking ----------
 * gp_audio.ko's blocking write waits for space with msleep(1), which at HZ=100 sleeps 10 to 20 ms:
 * far too coarse to pace frames. Non-blocking mode can only be enabled with SNDCTL_DSP_NONBLOCK (the
 * driver ignores O_NONBLOCK on open). The TV retrace holds the pacing; the runner adjusts the audio
 * rate from the queue (SNDCTL_DSP_GETODELAY). */
static int dsp = -1, dsp_rate;

int plat_audio_init(int rate)
{
    dsp = open("/dev/dsp", O_WRONLY);
    if (dsp < 0) { step("audio: could not open /dev/dsp (busy, is the factory emulator running?)", 0, 0); return -1; }
    /* OUTPUT: gp_audio.ko sends to the analog output (headphone/AV) by default; ioctl 0xc0045007 with 1 switches
     * to HDMI I2S (DMA "ocp"). Without it the audio goes somewhere the TV does not hear.
     * It must come BEFORE SETFRAGMENT: that rebuilds the fragments. */
    const char *output = getenv("STICK_AUDIO_OUTPUT");
    int hdmi = !(output && *output == '0');
    if (ioctl(dsp, AUDIO_OUTPUT_HDMI, &hdmi)) step("audio: output selection (0xc0045007) failed", 0, 0);
    step("audio: output hdmi %d", hdmi, 0);
    int fmt = AFMT_S16_LE, ch = 2, t = rate, frag = (32 << 16) | 10; /* 32 fragments of 1 KB (~170 ms at 48 kHz) */
    ioctl(dsp, SNDCTL_DSP_SETFRAGMENT, &frag);
    ioctl(dsp, SNDCTL_DSP_SETFMT, &fmt);
    ioctl(dsp, SNDCTL_DSP_CHANNELS, &ch);
    ioctl(dsp, SNDCTL_DSP_SPEED, &t);
    step("audio: requested rate %d, accepted %d", rate, t);
    dsp_rate = t;
    audio_buf_info bi;
    if (!ioctl(dsp, SNDCTL_DSP_GETOSPACE, &bi)) step("audio: %d fragments of %d bytes", bi.fragstotal, bi.fragsize);
    if (ioctl(dsp, SNDCTL_DSP_NONBLOCK, 0)) step("audio: SNDCTL_DSP_NONBLOCK failed", 0, 0);
    /* mixer volume (OSS: left | right << 8, 0 to 100): without the stock 10 in 1 nobody sets it */
    int mix = open("/dev/mixer", O_WRONLY);
    if (mix >= 0) {
        int vol = 90 | 90 << 8;
        if (ioctl(mix, SOUND_MIXER_WRITE_VOLUME, &vol)) step("audio: mixer volume failed", 0, 0);
        else step("audio: mixer volume %d", vol & 0xff, 0);
        close(mix);
    } else step("audio: could not open /dev/mixer", 0, 0);
    return 0;
}

int plat_audio(const int16_t *s, int n)
{
    if (dsp < 0) return 0;
    int r = write(dsp, s, n * 4);
    if (r < 0) return errno == EAGAIN ? 0 : (close(dsp), dsp = -1, 0);
    return r / 4;
}

int plat_audio_rate(void) { return dsp >= 0 ? dsp_rate : 0; }
void plat_audio_close(void) { if (dsp >= 0) close(dsp); dsp = -1; }

int plat_audio_queue(void)
{
    int b;
    if (dsp < 0 || ioctl(dsp, SNDCTL_DSP_GETODELAY, &b)) return -1;
    return b / 4;
}

void plat_reactivate(void)
{
    if (disp < 0) return;
    ioctl(disp, DISP_SET_PRI_BITMAP, &bm[cur]);
    ioctl(disp, DISP_SET_PRI_ENABLE, 1);
    ioctl(disp, DISP_SET_OSD_ENABLE, 0);
}

const char *plat_runner(const char *system_name)
{
    static char path[256];
    snprintf(path, sizeof path, "%s/../%s-arm", plat_roms_dir(), system_name);
    return access(path, X_OK) ? NULL : path;
}

static void copy_file_to_log(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f || !logf) { if (f) fclose(f); return; }
    char l[256];
    fprintf(logf, "--- %s\n", path);
    while (fgets(l, sizeof l, f)) fputs(l, logf);
    fclose(f);
}

void plat_diagnostics(void)
{
    if (!logf) return;
    fprintf(logf, "--- bytes in serial buffer now: %d, serial fd %d\n", rx.n, ser);
    copy_file_to_log("/proc/tty/driver/serial");
    copy_file_to_log("/proc/loadavg");
    copy_file_to_log("/proc/interrupts");
    /* processes: name and state of each /proc/<pid>/stat */
    fprintf(logf, "--- processes (pid name state)\n");
    for (int pid = 1; pid < 2000; pid++) {
        char path[64], l[256];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(l, sizeof l, f)) fputs(l, logf);
        fclose(f);
    }
    fflush(logf);
    fsync(fileno(logf));
}
