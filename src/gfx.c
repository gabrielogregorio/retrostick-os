/* Software drawing on a 0x00RRGGBB buffer. No dependencies at all. */
#include "gfx.h"

static uint32_t *P;
static int W, H;

void gfx_target(uint32_t *px, int w, int h) { P = px; W = w; H = h; }

uint32_t gfx_blend(uint32_t a, uint32_t b, int t)
{
    uint32_t rb = ((a & 0xff00ff) * (255 - t) + (b & 0xff00ff) * t) >> 8;
    uint32_t g = ((a & 0x00ff00) * (255 - t) + (b & 0x00ff00) * t) >> 8;
    return (rb & 0xff00ff) | (g & 0x00ff00);
}

static inline void plot(int x, int y, uint32_t color, int alpha)
{
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    uint32_t *d = &P[y * W + x];
    *d = alpha >= 255 ? color : gfx_blend(*d, color, alpha);
}

void gfx_gradient_v(uint32_t top, uint32_t bottom)
{
    for (int y = 0; y < H; y++) {
        uint32_t c = gfx_blend(top, bottom, y * 255 / (H - 1));
        uint32_t *l = &P[y * W];
        for (int x = 0; x < W; x++) l[x] = c;
    }
}

void gfx_rect(int x, int y, int w, int h, uint32_t color, int alpha)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) plot(i, j, color, alpha);
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t color, int alpha)
{
    gfx_round_rect_grad(x, y, w, h, r, color, color, alpha);
}

static inline void hline(uint32_t *d, int n, uint32_t color, int alpha)
{
    if (alpha >= 255) { while (n-- > 0) *d++ = color; return; }
    uint32_t crb = (color & 0xff00ff) * alpha, cg = (color & 0x00ff00) * alpha;
    int inv = 255 - alpha;
    while (n-- > 0) {
        uint32_t v = *d;
        *d++ = ((((v & 0xff00ff) * inv + crb) >> 8) & 0xff00ff) | ((((v & 0x00ff00) * inv + cg) >> 8) & 0x00ff00);
    }
}

void gfx_round_rect_grad(int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom, int alpha)
{
    if (alpha <= 0) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if ((unsigned)yy >= (unsigned)H) continue;
        uint32_t color = h > 1 ? gfx_blend(top, bottom, j * 255 / (h - 1)) : top;
        int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
        int m0 = 0, m1 = w; /* middle span without corners */
        if (dy) {
            m0 = r; m1 = w - r;
            for (int i = 0; i < r; i++) {
                int dx = r - i, d2 = dx * dx + dy * dy, r2 = r * r, a = alpha;
                if (d2 > r2 + r) continue;
                if (d2 > r2 - r) a = alpha * (r2 + r - d2) / (2 * r);
                plot(x + i, yy, color, a);
                plot(x + w - 1 - i, yy, color, a);
            }
        }
        int x0 = x + m0, x1 = x + m1;
        if (x0 < 0) x0 = 0;
        if (x1 > W) x1 = W;
        if (x1 > x0) hline(&P[yy * W + x0], x1 - x0, color, alpha);
    }
}

/* UTF-8 -> Latin-1 (whatever does not fit becomes '?') */
static int next_char(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p++;
    if (c >= 0xc0 && c < 0xe0 && (*p & 0xc0) == 0x80) c = ((c & 0x1f) << 6) | (*p++ & 0x3f);
    else if (c >= 0xe0) { while ((*p & 0xc0) == 0x80) p++; c = '?'; }
    *s = (const char *)p;
    return (c < 32 || c > 255) ? '?' : c;
}

int gfx_width(const font_t *f, const char *s)
{
    int w = 0;
    while (*s) w += f->g[next_char(&s) - 32].adv;
    return w;
}

/* y is the text baseline */
int gfx_text(const font_t *f, int x, int y, const char *s, uint32_t color, int alpha)
{
    y -= f->ascent;
    while (*s) {
        const glyph_t *g = &f->g[next_char(&s) - 32];
        const uint8_t *a = f->a + g->off;
        for (int j = 0; j < g->h; j++)
            for (int i = 0; i < g->w; i++) {
                int v = a[j * g->w + i];
                if (v) plot(x + g->x + i, y + g->y + j, color, v * alpha / 255);
            }
        x += g->adv;
    }
    return x;
}
