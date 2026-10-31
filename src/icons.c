/* System icons: glass bubble with the console's controller or handheld drawn inside. */
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "icons.h"

#define ICON_SIZE 102 /* small on purpose: fewer pixels per frame */
#define ICON_PIXELS (ICON_SIZE * ICON_SIZE)
#define OPAQUE 255
#define BLACK 0x000000
#define WHITE 0xffffff
#define MIN_DRAW_SIZE 4
#define REFLECTION_LAYERS 4

static struct {
    uint32_t color[ICON_PIXELS];
    uint8_t alpha[ICON_PIXELS];
    int ready;
} sprites[ICONS_MAX];

/* Where console parts land while an icon is being drawn. */
static struct {
    int cx, cy;
    float scale;
} stage;

static void part(float x, float y, float w, float h, float radius, uint32_t color)
{
    gfx_round_rect_grad(stage.cx + (int)(x * stage.scale), stage.cy + (int)(y * stage.scale), (int)(w * stage.scale),
                        (int)(h * stage.scale), (int)(radius * stage.scale), gfx_blend(color, WHITE, 70),
                        gfx_blend(color, BLACK, 40), OPAQUE);
}

static void disc(float x, float y, float radius, uint32_t color)
{
    part(x - radius, y - radius, radius * 2, radius * 2, radius, color);
}

static void dpad(float x, float y, float span, uint32_t color)
{
    part(x - span / 2, y - span / 6, span, span / 3, 1, color);
    part(x - span / 6, y - span / 2, span / 3, span, 1, color);
}

static void draw_nes(void)
{
    const uint32_t select_grey = 0x8a8a8a;
    const uint32_t button_red = 0xd42a2a;
    part(-34, -15, 68, 30, 3, 0xd8d8d4);
    part(-31, -10, 62, 22, 2, 0x2c2c2c);
    dpad(-19, 1, 13, 0x101010);
    part(-6, 3, 6, 3, 1.5f, select_grey);
    part(1, 3, 6, 3, 1.5f, select_grey);
    disc(14, 2, 4.6f, button_red);
    disc(25, 2, 4.6f, button_red);
}

static void draw_snes(void)
{
    const uint32_t select_grey = 0x606068;
    part(-35, -14, 70, 28, 14, 0xd0d0d8);
    disc(-20, 0, 9, 0xb8b8c2);
    dpad(-20, 0, 13, 0x303034);
    part(-6, 1, 5, 3, 1.5f, select_grey);
    part(1, 1, 5, 3, 1.5f, select_grey);
    disc(20, -6, 3.8f, 0x3050c8);
    disc(13.5f, 0, 3.8f, 0x30a040);
    disc(26.5f, 0, 3.8f, 0xd03030);
    disc(20, 6, 3.8f, 0xe8c020);
}

static void draw_md(void)
{
    const uint32_t button_grey = 0x5a5a66;
    part(-35, -15, 70, 30, 15, 0x34343a);
    disc(-19, 1, 9.5f, 0x1c1c20);
    dpad(-19, 1, 13, 0x56565e);
    part(-3, -10, 7, 3, 1.5f, 0xc02838);
    disc(8, 5, 4.4f, button_grey);
    disc(17, 1.5f, 4.4f, button_grey);
    disc(26, -2, 4.4f, button_grey);
}

static void draw_sms(void)
{
    const uint32_t button_grey = 0x8c8c94;
    part(-33, -14, 66, 28, 3, 0x202024);
    part(-31, -12, 62, 2, 1, 0xd02424);
    part(-26, -6, 15, 15, 2, 0x3a3a40);
    dpad(-18.5f, 1.5f, 12, 0x101012);
    disc(15, 3, 4.8f, button_grey);
    disc(26, 3, 4.8f, button_grey);
}

static void draw_gba(void)
{
    const uint32_t button_grey = 0xd8d8e0;
    part(-37, -16, 74, 32, 14, 0x5c4cc6);
    part(-16, -12, 32, 24, 3, 0x2a2a3c);
    part(-13, -9, 26, 18, 1, 0x9ad4ea);
    dpad(-26, 0, 11, 0x24242c);
    disc(25, -3, 3.6f, button_grey);
    disc(31, 3, 3.6f, button_grey);
}

static void draw_gb(void)
{
    const uint32_t button_magenta = 0xa82a5c;
    const uint32_t select_grey = 0x80807a;
    part(-19, -29, 38, 58, 4, 0xcac6b8);
    part(-15, -25, 30, 22, 2, 0x5c5c6c);
    part(-10, -22, 20, 16, 1, 0x8bac0f);
    dpad(-8, 10, 11, 0x24242a);
    disc(7, 13, 3.6f, button_magenta);
    disc(13, 8, 3.6f, button_magenta);
    part(-5, 20, 5, 2.5f, 1, select_grey);
    part(1, 20, 5, 2.5f, 1, select_grey);
}

/* Favorites: golden star with shine. */
static void draw_favorites(void)
{
    gfx_star(stage.cx, stage.cy - 2, (int)(33 * stage.scale), 0xfff6b0, 0xe89a10, OPAQUE);
    gfx_star(stage.cx - (int)(4 * stage.scale), stage.cy - (int)(7 * stage.scale), (int)(12 * stage.scale), WHITE,
             0xfff0a0, 150);
}

static void draw_gg(void)
{
    const uint32_t button_grey = 0x5c5c6c;
    part(-38, -17, 76, 34, 11, 0x2c2c32);
    part(-15, -13, 30, 25, 3, 0x40404e);
    part(-12, -10, 24, 18, 1, 0x6ab4e4);
    dpad(-26, 1, 11, 0x111114);
    disc(24, 4, 3.8f, button_grey);
    disc(31, -1, 3.8f, button_grey);
    part(22, -12, 7, 3, 1.5f, 0x2a50c0);
}

/* CX40 joystick: black base, stick and the red button. */
static void draw_a26(void)
{
    part(-21, -4, 42, 30, 4, 0x1c1c1e);
    part(-18, -1, 36, 24, 3, 0x2e2e32);
    disc(0, 11, 8, 0x18181a);
    part(-3, -30, 6, 42, 3, 0x26262a);
    disc(-13, -4, 5, 0xd42424);
}

static void draw_ngp(void)
{
    const uint32_t button_grey = 0xe4e4ec;
    part(-37, -16, 74, 32, 14, 0x3a70d4);
    part(-14, -12, 28, 24, 3, 0x2a2a3c);
    part(-11, -9, 22, 18, 1, 0xa8dcb8);
    disc(-25, 0, 7.5f, 0xd8d8e2);
    disc(-25, 0, 3.6f, 0x9a9aac);
    disc(23, 3, 3.8f, button_grey);
    disc(30, -3, 3.8f, button_grey);
}

static void draw_ws(void)
{
    const uint32_t button_grey = 0x60606e;
    part(-39, -15, 78, 30, 8, 0xe8e4dc);
    part(-16, -11, 32, 22, 2, 0x56605a);
    part(-13, -8, 26, 16, 1, 0xb4c4a4);
    disc(-27, -5, 2.6f, button_grey);
    disc(-31, 0, 2.6f, button_grey);
    disc(-23, 0, 2.6f, button_grey);
    disc(-27, 5, 2.6f, button_grey);
    disc(25, 4, 3.6f, button_grey);
    disc(31, -1, 3.6f, button_grey);
}

static const struct {
    const char *system;
    void (*draw)(void);
} CONSOLES[] = {
    {"nes", draw_nes}, {"snes", draw_snes}, {"md", draw_md},         {"sms", draw_sms},
    {"gba", draw_gba}, {"gb", draw_gb},     {"fav", draw_favorites}, {"gg", draw_gg},
    {"a26", draw_a26}, {"ngp", draw_ngp},   {"ws", draw_ws},
};

static void draw_console(const char *system)
{
    for (size_t index = 0; index < sizeof CONSOLES / sizeof CONSOLES[0]; index += 1) {
        if (!strcmp(system, CONSOLES[index].system)) {
            CONSOLES[index].draw();

            return;
        }
    }
}

static void draw_glass_bubble(int center, int radius, uint32_t tint)
{
    gfx_round_rect(center - radius - 2, center - radius - 2, 2 * radius + 4, 2 * radius + 4, radius + 2, WHITE, 70);
    gfx_round_rect_grad(center - radius, center - radius, 2 * radius, 2 * radius, radius, gfx_blend(tint, WHITE, 150),
                        tint, 120);
    /* bounced light at the bottom */
    gfx_round_rect_grad(center - radius + 8, center + radius / 3, 2 * radius - 16, radius * 2 / 3 - 4, radius / 2, tint,
                        WHITE, 70);
}

/* Overlapping layers so the edge of the reflection comes out soft. */
static void draw_glass_reflection(int center, int radius, uint32_t tint)
{
    for (int layer = 0; layer < REFLECTION_LAYERS; layer += 1) {
        int w = radius * (13 - layer * 2) / 10;
        int h = radius * (5 - layer) / 10;
        gfx_round_rect_grad(center - w / 2, center - radius + 4 + layer * 2, w, h, h / 2, WHITE,
                            gfx_blend(tint, WHITE, 200), 34);
    }
}

static void draw_icon(const char *system, uint32_t tint)
{
    int center = ICON_SIZE / 2;
    int radius = ICON_SIZE * 54 / 128;
    draw_glass_bubble(center, radius, tint);
    stage.cx = center;
    stage.cy = center + 3;
    stage.scale = 1.3f * ICON_SIZE / 128;
    draw_console(system);
    draw_glass_reflection(center, radius, tint);
}

static int clamp_byte(int value)
{
    return value > 255 ? 255 : value;
}

static uint32_t unpremultiply(uint32_t pixel, int alpha)
{
    int r = ((pixel >> 16) & 255) * 255 / alpha;
    int g = ((pixel >> 8) & 255) * 255 / alpha;
    int b = (pixel & 255) * 255 / alpha;

    return (uint32_t)clamp_byte(r) << 16 | (uint32_t)clamp_byte(g) << 8 | (uint32_t)clamp_byte(b);
}

/* The icon is drawn over black and over white: alpha comes from the difference between the two renders,
 * so a built sprite composites over any background. */
void icons_build(int icon, const char *system, uint32_t tint)
{
    static uint32_t on_black[ICON_PIXELS], on_white[ICON_PIXELS];
    memset(on_black, 0, sizeof on_black);
    memset(on_white, 0xff, sizeof on_white);
    gfx_target(on_black, ICON_SIZE, ICON_SIZE);
    draw_icon(system, tint);
    gfx_target(on_white, ICON_SIZE, ICON_SIZE);
    draw_icon(system, tint);
    for (int index = 0; index < ICON_PIXELS; index += 1) {
        int alpha = 255 - (int)(((on_white[index] >> 8) & 255) - ((on_black[index] >> 8) & 255));
        if (alpha < 0) {
            alpha = 0;
        }
        alpha = clamp_byte(alpha);
        sprites[icon].alpha[index] = alpha;
        if (alpha) {
            sprites[icon].color[index] = unpremultiply(on_black[index], alpha);
        }
    }
    sprites[icon].ready = 1;
}

void icons_draw(uint32_t *screen, int w, int h, int icon, int cx, int cy, float scale, int alpha)
{
    if (!sprites[icon].ready || alpha <= 0) {
        return;
    }
    int size = (int)(ICON_SIZE * scale);
    if (size < MIN_DRAW_SIZE) {
        return;
    }
    int left = cx - size / 2;
    int top = cy - size / 2;
    for (int y = 0; y < size; y += 1) {
        int screen_y = top + y;
        if ((unsigned)screen_y >= (unsigned)h) {
            continue;
        }
        int source_row = y * ICON_SIZE / size;
        for (int x = 0; x < size; x += 1) {
            int screen_x = left + x;
            if ((unsigned)screen_x >= (unsigned)w) {
                continue;
            }
            int source = source_row * ICON_SIZE + x * ICON_SIZE / size;
            int pixel_alpha = sprites[icon].alpha[source];
            if (!pixel_alpha) {
                continue;
            }
            uint32_t *pixel = &screen[screen_y * w + screen_x];
            *pixel = gfx_blend(*pixel, sprites[icon].color[source], pixel_alpha * alpha / OPAQUE);
        }
    }
}
