/* System icons: glass bubble with the console's controller/handheld drawn inside.
 * Each icon becomes a sprite with alpha (built once): it is drawn over a black background and over a white one
 * and alpha is taken from the difference; each frame adds only the scaled copy. */
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "icons.h"

#define ICON_SIZE 128
static struct { uint32_t color[ICON_SIZE * ICON_SIZE]; uint8_t alpha[ICON_SIZE * ICON_SIZE]; int ok; } spr[ICONS_MAX];

static int CX, CY;
static float S;

static void R(float x, float y, float w, float h, float r, uint32_t color)
{
    gfx_round_rect_grad(CX + (int)(x * S), CY + (int)(y * S), (int)(w * S), (int)(h * S), (int)(r * S),
                       gfx_blend(color, 0xffffff, 70), gfx_blend(color, 0x000000, 40), 255);
}
static void C(float x, float y, float r, uint32_t color) { R(x - r, y - r, r * 2, r * 2, r, color); }
static void cross(float x, float y, float t, uint32_t color)
{
    R(x - t / 2, y - t / 6, t, t / 3, 1, color);
    R(x - t / 6, y - t / 2, t / 3, t, 1, color);
}

static void draw_console(const char *s)
{
    if (!strcmp(s, "nes")) {
        R(-34, -15, 68, 30, 3, 0xd8d8d4);
        R(-31, -10, 62, 22, 2, 0x2c2c2c);
        cross(-19, 1, 13, 0x101010);
        R(-6, 3, 6, 3, 1.5f, 0x8a8a8a); R(1, 3, 6, 3, 1.5f, 0x8a8a8a);
        C(14, 2, 4.6f, 0xd42a2a); C(25, 2, 4.6f, 0xd42a2a);
    } else if (!strcmp(s, "snes")) {
        R(-35, -14, 70, 28, 14, 0xd0d0d8);
        C(-20, 0, 9, 0xb8b8c2);
        cross(-20, 0, 13, 0x303034);
        R(-6, 1, 5, 3, 1.5f, 0x606068); R(1, 1, 5, 3, 1.5f, 0x606068);
        C(20, -6, 3.8f, 0x3050c8); C(13.5f, 0, 3.8f, 0x30a040);
        C(26.5f, 0, 3.8f, 0xd03030); C(20, 6, 3.8f, 0xe8c020);
    } else if (!strcmp(s, "md")) {
        R(-35, -15, 70, 30, 15, 0x34343a);
        C(-19, 1, 9.5f, 0x1c1c20);
        cross(-19, 1, 13, 0x56565e);
        R(-3, -10, 7, 3, 1.5f, 0xc02838);
        C(8, 5, 4.4f, 0x5a5a66); C(17, 1.5f, 4.4f, 0x5a5a66); C(26, -2, 4.4f, 0x5a5a66);
    } else if (!strcmp(s, "sms")) {
        R(-33, -14, 66, 28, 3, 0x202024);
        R(-31, -12, 62, 2, 1, 0xd02424);
        R(-26, -6, 15, 15, 2, 0x3a3a40);
        cross(-18.5f, 1.5f, 12, 0x101012);
        C(15, 3, 4.8f, 0x8c8c94); C(26, 3, 4.8f, 0x8c8c94);
    } else if (!strcmp(s, "gba")) {
        R(-37, -16, 74, 32, 14, 0x5c4cc6);
        R(-16, -12, 32, 24, 3, 0x2a2a3c);
        R(-13, -9, 26, 18, 1, 0x9ad4ea);
        cross(-26, 0, 11, 0x24242c);
        C(25, -3, 3.6f, 0xd8d8e0); C(31, 3, 3.6f, 0xd8d8e0);
    } else if (!strcmp(s, "gb")) {
        R(-19, -29, 38, 58, 4, 0xcac6b8);
        R(-15, -25, 30, 22, 2, 0x5c5c6c);
        R(-10, -22, 20, 16, 1, 0x8bac0f);
        cross(-8, 10, 11, 0x24242a);
        C(7, 13, 3.6f, 0xa82a5c); C(13, 8, 3.6f, 0xa82a5c);
        R(-5, 20, 5, 2.5f, 1, 0x80807a); R(1, 20, 5, 2.5f, 1, 0x80807a);
    } else if (!strcmp(s, "gg")) {
        R(-38, -17, 76, 34, 11, 0x2c2c32);
        R(-15, -13, 30, 25, 3, 0x40404e);
        R(-12, -10, 24, 18, 1, 0x6ab4e4);
        cross(-26, 1, 11, 0x111114);
        C(24, 4, 3.8f, 0x5c5c6c); C(31, -1, 3.8f, 0x5c5c6c);
        R(22, -12, 7, 3, 1.5f, 0x2a50c0);
    } else if (!strcmp(s, "a26")) {
        /* CX40 joystick: black base, stick and the red button */
        R(-21, -4, 42, 30, 4, 0x1c1c1e);
        R(-18, -1, 36, 24, 3, 0x2e2e32);
        C(0, 11, 8, 0x18181a);
        R(-3, -30, 6, 42, 3, 0x26262a);
        C(-13, -4, 5, 0xd42424);
    } else if (!strcmp(s, "ngp")) {
        R(-37, -16, 74, 32, 14, 0x3a70d4);
        R(-14, -12, 28, 24, 3, 0x2a2a3c);
        R(-11, -9, 22, 18, 1, 0xa8dcb8);
        C(-25, 0, 7.5f, 0xd8d8e2); C(-25, 0, 3.6f, 0x9a9aac);
        C(23, 3, 3.8f, 0xe4e4ec); C(30, -3, 3.8f, 0xe4e4ec);
    } else if (!strcmp(s, "ws")) {
        R(-39, -15, 78, 30, 8, 0xe8e4dc);
        R(-16, -11, 32, 22, 2, 0x56605a);
        R(-13, -8, 26, 16, 1, 0xb4c4a4);
        C(-27, -5, 2.6f, 0x60606e); C(-31, 0, 2.6f, 0x60606e); C(-23, 0, 2.6f, 0x60606e); C(-27, 5, 2.6f, 0x60606e);
        C(25, 4, 3.6f, 0x60606e); C(31, -1, 3.6f, 0x60606e);
    }
}

/* glass bubble + console; called twice (black and white background) */
static void draw_icon(const char *sys, uint32_t tint)
{
    int c = ICON_SIZE / 2, r = 54;
    gfx_round_rect(c - r - 2, c - r - 2, 2 * r + 4, 2 * r + 4, r + 2, 0xffffff, 70);                 /* rim of light */
    gfx_round_rect_grad(c - r, c - r, 2 * r, 2 * r, r, gfx_blend(tint, 0xffffff, 150), tint, 120);   /* tinted glass */
    gfx_round_rect_grad(c - r + 8, c + r / 3, 2 * r - 16, r * 2 / 3 - 4, r / 2, tint, 0xffffff, 70);  /* bounced light at the bottom */
    CX = c; CY = c + 4; S = 1.3f;
    draw_console(sys);
    /* glass reflection over the console: overlapping layers so the edge comes out soft */
    for (int k = 0; k < 4; k++) {
        int w = r * (13 - k * 2) / 10, h = r * (5 - k) / 10;
        gfx_round_rect_grad(c - w / 2, c - r + 4 + k * 2, w, h, h / 2, 0xffffff, gfx_blend(tint, 0xffffff, 200), 34);
    }
}

void icons_build(int i, const char *sys, uint32_t tint)
{
    static uint32_t black[ICON_SIZE * ICON_SIZE], white[ICON_SIZE * ICON_SIZE];
    memset(black, 0, sizeof black);
    memset(white, 0xff, sizeof white);
    gfx_target(black, ICON_SIZE, ICON_SIZE); draw_icon(sys, tint);
    gfx_target(white, ICON_SIZE, ICON_SIZE); draw_icon(sys, tint);
    for (int k = 0; k < ICON_SIZE * ICON_SIZE; k++) {
        int a = 255 - (int)(((white[k] >> 8) & 255) - ((black[k] >> 8) & 255));
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        spr[i].alpha[k] = a;
        if (a) {
            uint32_t p = black[k];
            int rr = ((p >> 16) & 255) * 255 / a, gg = ((p >> 8) & 255) * 255 / a, bb = (p & 255) * 255 / a;
            spr[i].color[k] = (uint32_t)(rr > 255 ? 255 : rr) << 16 | (uint32_t)(gg > 255 ? 255 : gg) << 8 | (uint32_t)(bb > 255 ? 255 : bb);
        }
    }
    spr[i].ok = 1;
}

void icons_draw(uint32_t *screen, int tw, int th, int i, int cx, int cy, float scale, int alpha)
{
    if (!spr[i].ok || alpha <= 0) return;
    int d = (int)(ICON_SIZE * scale);
    if (d < 4) return;
    int x0 = cx - d / 2, y0 = cy - d / 2;
    for (int y = 0; y < d; y++) {
        int yy = y0 + y;
        if ((unsigned)yy >= (unsigned)th) continue;
        int sy = y * ICON_SIZE / d;
        for (int x = 0; x < d; x++) {
            int xx = x0 + x;
            if ((unsigned)xx >= (unsigned)tw) continue;
            int k = sy * ICON_SIZE + x * ICON_SIZE / d, a = spr[i].alpha[k];
            if (!a) continue;
            a = a * alpha / 255;
            uint32_t *p = &screen[yy * tw + xx];
            *p = gfx_blend(*p, spr[i].color[k], a);
        }
    }
}
