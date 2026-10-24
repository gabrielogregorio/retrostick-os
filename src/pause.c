/* Runner pause menu (MODE button): draws the list over the last game frame, darkened.
 * Drawing only; the logic (save, load, quit) lives in runner.c. */
#include <string.h>
#include "gfx.h"
#include "pause.h"
#include "plat.h"

static uint32_t screen[SCREEN_W * SCREEN_H];

/* last game frame (RGB565) stretched to 4:3 in the center of the 16:9 screen, at 45 % brightness */
static void draw_background(const uint16_t *q, int w, int h)
{
    memset(screen, 0, sizeof screen);
    if (!q || !w || !h) return;
    int fw = SCREEN_H * 4 / 3, fx = (SCREEN_W - fw) / 2;
    for (int y = 0; y < SCREEN_H; y++) {
        const uint16_t *src = q + (y * h / SCREEN_H) * w;
        uint32_t *d = screen + y * SCREEN_W + fx;
        for (int x = 0; x < fw; x++) {
            uint16_t c = src[x * w / fw];
            uint32_t r = (c >> 11) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
            d[x] = (r * 115 / 255) << 16 | (g * 115 / 255) << 8 | (b * 115 / 255);
        }
    }
}

void pause_draw(const uint16_t *frame, int w, int h, const char *const *items, const char *const *notes, int n,
                   int sel, const char *notice)
{
    gfx_target(screen, SCREEN_W, SCREEN_H);
    draw_background(frame, w, h);
    int lh = 30, pw = 360, ph = 70 + n * lh + 34, px = (SCREEN_W - pw) / 2, py = (SCREEN_H - ph) / 2;
    /* glass panel */
    gfx_round_rect(px + 3, py + 5, pw, ph, 14, 0x000000, 90);
    gfx_round_rect_grad(px, py, pw, ph, 14, 0xf4fbff, 0xcfe6f7, 235);
    gfx_round_rect_grad(px + 2, py + 2, pw - 4, 34, 12, 0xffffff, 0xffffff, 70);
    gfx_text(&font_big, px + 22, py + 38, "Pause", 0x0e4a7a, 255);
    int y0 = py + 56;
    for (int i = 0; i < n; i++) {
        int y = y0 + i * lh;
        if (i == sel) {
            gfx_round_rect_grad(px + 12, y, pw - 24, lh - 4, 10, 0x5ab8f0, 0x1f86d0, 255);
            gfx_round_rect_grad(px + 14, y + 1, pw - 28, (lh - 4) / 2, 8, 0xffffff, 0xffffff, 60);
        }
        uint32_t color = i == sel ? 0xffffff : 0x163a5c;
        gfx_text(&font_small, px + 26, y + 19, items[i], color, 255);
        if (notes && notes[i] && notes[i][0]) {
            int nw = gfx_width(&font_tiny, notes[i]);
            gfx_text(&font_tiny, px + pw - 26 - nw, y + 18, notes[i], i == sel ? 0xe8f4ff : 0x4a6f8f, 255);
        }
    }
    const char *footer = notice && notice[0] ? notice : "A select    B or MODE back to game";
    int rw = gfx_width(&font_tiny, footer);
    gfx_text(&font_tiny, px + (pw - rw) / 2, py + ph - 14, footer, notice && notice[0] ? 0x0e7a3a : 0x4a6f8f, 255);
    plat_present(screen);
}
