/* "Aero" theme (glossy desktop look): sky, light, aurora, hills, bubbles and glass.
 * The background is built ONCE (expensive, uses float); each frame adds only bubbles and panels. */
#include <math.h>
#include <stdlib.h>
#include "aero.h"
#include "gfx.h"

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static uint32_t rgb(float r, float g, float b)
{
    return ((uint32_t)(clamp01(r) * 255) << 16) | ((uint32_t)(clamp01(g) * 255) << 8) | (uint32_t)(clamp01(b) * 255);
}
static void color_to_float(uint32_t c, float *r, float *g, float *b)
{
    *r = ((c >> 16) & 255) / 255.0f; *g = ((c >> 8) & 255) / 255.0f; *b = (c & 255) / 255.0f;
}
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

void aero_background(uint32_t *px, int w, int h, uint32_t tint)
{
    float tr, tg, tb;
    color_to_float(tint, &tr, &tg, &tb);
    const float sx = w * 0.82f, sy = h * 0.08f; /* sun */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float fy = (float)y / h;
            /* sky: deep blue -> cyan -> almost white at the horizon */
            float r, g, b;
            if (fy < 0.55f) { float t = fy / 0.55f; r = lerpf(0.02f, 0.40f, t); g = lerpf(0.40f, 0.80f, t); b = lerpf(0.78f, 0.97f, t); }
            else { float t = clamp01((fy - 0.55f) / 0.2f); r = lerpf(0.40f, 0.88f, t); g = lerpf(0.80f, 0.97f, t); b = lerpf(0.97f, 1.0f, t); }
            /* optional light tint (0 = no tint) */
            if (tint) { r = lerpf(r, tr, 0.10f); g = lerpf(g, tg, 0.10f); b = lerpf(b, tb, 0.10f); }
            /* sun glow and light rays */
            float dx = x - sx, dy = y - sy, d = sqrtf(dx * dx + dy * dy) / w;
            float glow = expf(-d * 4.5f) * 0.55f;
            float ang = atan2f(dy, dx);
            float ray = powf(fabsf(sinf(ang * 7 + 0.6f)), 10) * expf(-d * 2.2f) * 0.22f;
            r += glow + ray; g += glow + ray; b += glow * 0.8f + ray;
            /* aurora ribbons */
            float c1 = h * 0.42f + h * 0.10f * sinf(x * 0.011f + 0.8f) + h * 0.03f * sinf(x * 0.031f);
            float a1 = expf(-powf((y - c1) / (h * 0.045f), 2)) * 0.35f;
            float c2 = h * 0.52f + h * 0.07f * sinf(x * 0.008f + 2.6f);
            float a2 = expf(-powf((y - c2) / (h * 0.03f), 2)) * 0.28f;
            r = lerpf(r, 1.0f, a1); g = lerpf(g, 1.0f, a1); b = lerpf(b, 1.0f, a1);
            r = lerpf(r, 0.55f, a2); g = lerpf(g, 0.98f, a2); b = lerpf(b, 0.85f, a2);
            /* hills: the back one lighter, the front one deeper; glowing edge */
            float h1 = h * 0.74f + h * 0.05f * sinf(x * 0.009f + 1.0f);
            float h2 = h * 0.82f + h * 0.06f * sinf(x * 0.007f + 3.0f) + h * 0.015f * sinf(x * 0.023f);
            float m1 = clamp01(y - h1 + 0.5f), m2 = clamp01(y - h2 + 0.5f);
            if (m1 > 0) {
                float t = clamp01((y - h1) / (h - h1));
                float cr = lerpf(0.62f, 0.30f, t), cg = lerpf(0.92f, 0.72f, t), cb = lerpf(0.42f, 0.28f, t);
                float edge = expf(-(y - h1) / 3.0f) * 0.6f;
                cr += edge; cg += edge; cb += edge;
                r = lerpf(r, cr, m1); g = lerpf(g, cg, m1); b = lerpf(b, cb, m1);
            }
            if (m2 > 0) {
                float t = clamp01((y - h2) / (h - h2));
                float cr = lerpf(0.45f, 0.10f, t), cg = lerpf(0.85f, 0.52f, t), cb = lerpf(0.30f, 0.22f, t);
                float edge = expf(-(y - h2) / 3.0f) * 0.7f;
                /* light reflection on the grass, on the right (sun side) */
                float shine = expf(-powf((x - sx) / (w * 0.25f), 2)) * expf(-(y - h2) / 12.0f) * 0.35f;
                cr += edge + shine; cg += edge + shine; cb += edge + shine * 0.6f;
                r = lerpf(r, cr, m2); g = lerpf(g, cg, m2); b = lerpf(b, cb, m2);
            }
            px[y * w + x] = rgb(r, g, b);
        }
    /* fixed bokeh in the sky */
    gfx_target(px, w, h);
    srand(7);
    for (int i = 0; i < 18; i++) {
        int bx = rand() % w, by = rand() % (h * 2 / 3), br = 6 + rand() % 22;
        for (int y = -br; y <= br; y++)
            for (int x = -br; x <= br; x++) {
                float d = sqrtf(x * x + y * y) / br;
                if (d > 1) continue;
                int a = (int)((0.25f + 0.75f * powf(d, 6)) * (1 - powf(d, 24)) * 46);
                int xx = bx + x, yy = by + y;
                if (xx >= 0 && xx < w && yy >= 0 && yy < h) px[yy * w + xx] = gfx_blend(px[yy * w + xx], 0xffffff, a);
            }
    }
}

/* ---------- soap bubbles (sprites with per-pixel alpha) ---------- */
typedef struct { int r, d; uint32_t *color; uint8_t *alpha; } bubble_sprite_t;
static bubble_sprite_t spr[3];

static void build_bubble(bubble_sprite_t *s, int r)
{
    s->r = r; s->d = r * 2 + 1;
    s->color = malloc(s->d * s->d * 4);
    s->alpha = malloc(s->d * s->d);
    for (int y = 0; y < s->d; y++)
        for (int x = 0; x < s->d; x++) {
            float fx = (x - r) / (float)r, fy = (y - r) / (float)r, d = sqrtf(fx * fx + fy * fy);
            float a = 0, cr = 0.85f, cg = 0.97f, cb = 1.0f;
            if (d <= 1.02f) {
                float aa = clamp01((1.02f - d) * r);              /* soft edge */
                a = (0.10f + 0.65f * powf(d, 5)) * aa;            /* rim stronger than the middle */
                /* light iridescence on the rim */
                cr = 0.80f + 0.2f * sinf(fx * 3 + 1); cg = 0.92f + 0.08f * sinf(fy * 3); cb = 1.0f;
                /* main reflection (upper left) */
                float hx = (fx + 0.38f) / 0.36f, hy = (fy + 0.42f) / 0.22f, hd = hx * hx + hy * hy;
                if (hd < 1) { float t = 1 - hd; a = a + (1 - a) * t * 0.9f; cr = lerpf(cr, 1, t); cg = lerpf(cg, 1, t); }
                /* small reflection at the lower right */
                float kx = (fx - 0.45f) / 0.15f, ky = (fy - 0.5f) / 0.1f, kd = kx * kx + ky * ky;
                if (kd < 1) a = a + (1 - a) * (1 - kd) * 0.5f;
            }
            s->color[y * s->d + x] = rgb(cr, cg, cb);
            s->alpha[y * s->d + x] = (uint8_t)(clamp01(a) * 255);
        }
}

typedef struct { float x, y, speed, phase, osc; int size; } bubble_t;
#define NUM_BUBBLES 6
static bubble_t bubbles[NUM_BUBBLES];
static int bw, bh;

void aero_init(int w, int h)
{
    bw = w; bh = h;
    build_bubble(&spr[0], 7);
    build_bubble(&spr[1], 12);
    build_bubble(&spr[2], 19);
    srand(3);
    for (int i = 0; i < NUM_BUBBLES; i++) {
        bubbles[i].x = rand() % w;
        bubbles[i].y = rand() % h;
        bubbles[i].size = i % 3;
        bubbles[i].speed = 6 + rand() % 10 + bubbles[i].size * 3; /* px per second */
        bubbles[i].phase = (rand() % 628) / 100.0f;
        bubbles[i].osc = 6 + rand() % 14;
    }
}

void aero_bubbles(uint32_t *px, float secs)
{
    for (int i = 0; i < NUM_BUBBLES; i++) {
        bubble_t *b = &bubbles[i];
        bubble_sprite_t *s = &spr[b->size];
        float y = fmodf(b->y - secs * b->speed, bh + s->d * 2.0f);
        if (y < 0) y += bh + s->d * 2.0f;
        y -= s->d;
        int x0 = (int)(b->x + sinf(secs * 0.6f + b->phase) * b->osc) - s->r, y0 = (int)y - s->r;
        for (int j = 0; j < s->d; j++) {
            int yy = y0 + j;
            if ((unsigned)yy >= (unsigned)bh) continue;
            for (int k = 0; k < s->d; k++) {
                int xx = x0 + k, a = s->alpha[j * s->d + k];
                if (!a || (unsigned)xx >= (unsigned)bw) continue;
                uint32_t *d = &px[yy * bw + xx];
                *d = gfx_blend(*d, s->color[j * s->d + k], a);
            }
        }
    }
}

/* ---------- glass pieces ---------- */
void aero_glass(int x, int y, int w, int h, int r, int alpha)
{
    gfx_round_rect(x - 1, y - 1, w + 2, h + 2, r + 1, 0x1a5f8f, alpha / 5);       /* thin blue outline */
    gfx_round_rect_grad(x, y, w, h, r, 0xffffff, 0xe4f4ff, alpha);                 /* frosted glass */
    gfx_round_rect_grad(x + 2, y + 2, w - 4, h / 3, r - 1, 0xffffff, 0xffffff, alpha / 2); /* shine at the top */
}

void aero_pill(int x, int y, int w, int h, int r, uint32_t color, int alpha)
{
    gfx_round_rect(x + 2, y + 4, w, h, r, 0x0a3a5a, alpha / 4);                   /* shadow (bluish, never black) */
    gfx_round_rect_grad(x, y, w, h, r, gfx_blend(color, 0xffffff, 70), gfx_blend(color, 0x06203a, 70), alpha);
    /* glass reflection on the upper half */
    gfx_round_rect_grad(x + 3, y + 2, w - 6, h * 46 / 100, r - 2, gfx_blend(color, 0xffffff, 230),
                       gfx_blend(color, 0xffffff, 120), alpha * 170 / 255);
    /* bounced light at the bottom */
    gfx_round_rect_grad(x + r / 2, y + h * 70 / 100, w - r, h * 25 / 100, r / 2, gfx_blend(color, 0x06203a, 40),
                       gfx_blend(color, 0xffffff, 110), alpha * 120 / 255);
}

void aero_orb(int cx, int cy, int r, uint32_t color, int alpha)
{
    aero_pill(cx - r, cy - r, r * 2, r * 2, r, color, alpha);
}

void aero_text(const font_t *f, int x, int y, const char *t, uint32_t color, int alpha)
{
    uint32_t shadow = (color & 0xff) > 0x80 ? 0x0a3050 : 0xffffff; /* light text: dark blue shadow; dark text: white halo */
    gfx_text(f, x + 1, y + 1, t, shadow, alpha * 110 / 255);
    gfx_text(f, x, y, t, color, alpha);
}
