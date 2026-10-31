/* Runner pause menu drawing: the list over the last game frame, darkened. */
#include <string.h>
#include "gfx.h"
#include "pause.h"
#include "plat.h"

#define BACKGROUND_LEVEL 115 /* 45 % brightness, out of 255 */
#define PANEL_W 360
#define PANEL_RADIUS 14
#define PANEL_HEADER_H 70
#define PANEL_FOOTER_H 34
#define LIST_TOP 56
#define LINE_H 30
#define ROW_H (LINE_H - 4)
#define TEXT_MARGIN 26
#define OPAQUE 255
#define COLOR_WHITE 0xffffff
#define COLOR_TITLE 0x0e4a7a
#define COLOR_ITEM 0x163a5c
#define COLOR_NOTE 0x4a6f8f
#define COLOR_NOTE_SELECTED 0xe8f4ff
#define COLOR_NOTICE 0x0e7a3a

static uint32_t screen[SCREEN_W * SCREEN_H];

/* last game frame (RGB565) stretched to 4:3 in the center of the 16:9 screen */
static void draw_background(const uint16_t *frame, int w, int h)
{
    memset(screen, 0, sizeof screen);
    if (!frame || !w || !h) {
        return;
    }
    int screen_frame_w = SCREEN_H * 4 / 3;
    int screen_frame_x = (SCREEN_W - screen_frame_w) / 2;
    for (int y = 0; y < SCREEN_H; y += 1) {
        const uint16_t *src = frame + (y * h / SCREEN_H) * w;
        uint32_t *dst = screen + y * SCREEN_W + screen_frame_x;
        for (int x = 0; x < screen_frame_w; x += 1) {
            uint16_t px = src[x * w / screen_frame_w];
            uint32_t r = (px >> 11) * 255 / 31;
            uint32_t g = ((px >> 5) & 63) * 255 / 63;
            uint32_t b = (px & 31) * 255 / 31;
            dst[x] =
                (r * BACKGROUND_LEVEL / 255) << 16 | (g * BACKGROUND_LEVEL / 255) << 8 | (b * BACKGROUND_LEVEL / 255);
        }
    }
}

static void draw_panel(int x, int y, int height)
{
    gfx_round_rect(x + 3, y + 5, PANEL_W, height, PANEL_RADIUS, 0x000000, 90);
    gfx_round_rect_grad(x, y, PANEL_W, height, PANEL_RADIUS, 0xf4fbff, 0xcfe6f7, 235);
    gfx_round_rect_grad(x + 2, y + 2, PANEL_W - 4, 34, 12, COLOR_WHITE, COLOR_WHITE, 70);
    gfx_text(&font_big, x + 22, y + 38, "Pause", COLOR_TITLE, OPAQUE);
}

static void draw_item(int panel_x, int y, const char *label, const char *note, int selected)
{
    if (selected) {
        gfx_round_rect_grad(panel_x + 12, y, PANEL_W - 24, ROW_H, 10, 0x5ab8f0, 0x1f86d0, OPAQUE);
        gfx_round_rect_grad(panel_x + 14, y + 1, PANEL_W - 28, ROW_H / 2, 8, COLOR_WHITE, COLOR_WHITE, 60);
    }
    gfx_text(&font_small, panel_x + TEXT_MARGIN, y + 19, label, selected ? COLOR_WHITE : COLOR_ITEM, OPAQUE);
    if (note && note[0]) {
        int note_x = panel_x + PANEL_W - TEXT_MARGIN - gfx_width(&font_tiny, note);
        gfx_text(&font_tiny, note_x, y + 18, note, selected ? COLOR_NOTE_SELECTED : COLOR_NOTE, OPAQUE);
    }
}

static void draw_footer(int panel_x, int baseline_y, const char *notice)
{
    int has_notice = notice && notice[0];
    const char *footer = has_notice ? notice : "A select    B or MODE back to game";
    int footer_x = panel_x + (PANEL_W - gfx_width(&font_tiny, footer)) / 2;
    gfx_text(&font_tiny, footer_x, baseline_y, footer, has_notice ? COLOR_NOTICE : COLOR_NOTE, OPAQUE);
}

void pause_draw(const uint16_t *frame, int w, int h, const char *const *items, const char *const *notes, int item_count,
                int sel, const char *notice)
{
    gfx_target(screen, SCREEN_W, SCREEN_H);
    draw_background(frame, w, h);
    int panel_h = PANEL_HEADER_H + item_count * LINE_H + PANEL_FOOTER_H;
    int panel_x = (SCREEN_W - PANEL_W) / 2;
    int panel_y = (SCREEN_H - panel_h) / 2;
    draw_panel(panel_x, panel_y, panel_h);
    for (int item = 0; item < item_count; item += 1) {
        const char *note = notes ? notes[item] : NULL;
        draw_item(panel_x, panel_y + LIST_TOP + item * LINE_H, items[item], note, item == sel);
    }
    draw_footer(panel_x, panel_y + panel_h - 14, notice);
    plat_present(screen);
}
