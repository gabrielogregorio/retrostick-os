/* "Aero" theme: sky, light, aurora, hills, bubbles and glass.
 * The background is built once (expensive, uses float); each frame adds only bubbles and panels. */
#include <math.h>
#include <stdlib.h>
#include "aero.h"
#include "gfx.h"

#define OPAQUE 255
#define WHITE 0xffffff
#define DEEP_NAVY 0x06203a

#define SKY_SPLIT 0.55f
#define SKY_FADE 0.2f
#define TINT_STRENGTH 0.10f
#define SUN_X_RATIO 0.82f
#define SUN_Y_RATIO 0.08f
#define SUN_RAY_COUNT 7

#define BOKEH_SEED 7
#define BOKEH_COUNT 18
#define BOKEH_MIN_RADIUS 6
#define BOKEH_RADIUS_RANGE 22
#define BOKEH_ALPHA 46

#define BUBBLE_SEED 3
#define BUBBLE_COUNT 6
#define BUBBLE_SIZES 3
#define BUBBLE_EDGE 1.02f
#define BUBBLE_PHASE_STEPS 628 /* a full turn in hundredths of a radian */

typedef struct {
    float r, g, b;
} color_t;

typedef struct {
    color_t crest, foot;
    float edge_strength;
} hill_t;

typedef struct {
    int r, size;
    uint32_t *color;
    uint8_t *alpha;
} bubble_sprite_t;

typedef struct {
    float x, y, speed, phase, sway;
    int size_class;
} bubble_t;

static const color_t SKY_TOP = {0.02f, 0.40f, 0.78f};
static const color_t SKY_MIDDLE = {0.40f, 0.80f, 0.97f};
static const color_t SKY_HORIZON = {0.88f, 0.97f, 1.0f};
static const color_t AURORA_WHITE = {1.0f, 1.0f, 1.0f};
static const color_t AURORA_GREEN = {0.55f, 0.98f, 0.85f};
static const hill_t BACK_HILL = {{0.62f, 0.92f, 0.42f}, {0.30f, 0.72f, 0.28f}, 0.6f};
static const hill_t FRONT_HILL = {{0.45f, 0.85f, 0.30f}, {0.10f, 0.52f, 0.22f}, 0.7f};

static struct {
    bubble_sprite_t sprites[BUBBLE_SIZES];
    bubble_t bubbles[BUBBLE_COUNT];
    int screen_w, screen_h;
} field;

static float clamp01(float value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 1) {
        return 1;
    }

    return value;
}

static uint32_t rgb(float r, float g, float b)
{
    return ((uint32_t)(clamp01(r) * 255) << 16) | ((uint32_t)(clamp01(g) * 255) << 8) | (uint32_t)(clamp01(b) * 255);
}

static color_t color_from_rgb(uint32_t color)
{
    color_t result = {((color >> 16) & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, (color & 255) / 255.0f};

    return result;
}

static float lerpf(float from, float to, float amount)
{
    return from + (to - from) * amount;
}

static color_t color_lerp(color_t from, color_t to, float amount)
{
    color_t result = {lerpf(from.r, to.r, amount), lerpf(from.g, to.g, amount), lerpf(from.b, to.b, amount)};

    return result;
}

static float gaussian(float distance, float width)
{
    return expf(-powf(distance / width, 2));
}

/* Deep blue to cyan to almost white at the horizon. */
static color_t sky_gradient(float height_ratio)
{
    if (height_ratio < SKY_SPLIT) {
        return color_lerp(SKY_TOP, SKY_MIDDLE, height_ratio / SKY_SPLIT);
    }

    return color_lerp(SKY_MIDDLE, SKY_HORIZON, clamp01((height_ratio - SKY_SPLIT) / SKY_FADE));
}

static color_t add_sun_light(color_t pixel, float dx, float dy, int w)
{
    float distance = sqrtf(dx * dx + dy * dy) / w;
    float glow = expf(-distance * 4.5f) * 0.55f;
    float angle = atan2f(dy, dx);
    float ray = powf(fabsf(sinf(angle * SUN_RAY_COUNT + 0.6f)), 10) * expf(-distance * 2.2f) * 0.22f;
    pixel.r += glow + ray;
    pixel.g += glow + ray;
    pixel.b += glow * 0.8f + ray;

    return pixel;
}

static color_t add_aurora(color_t pixel, int x, int y, int h)
{
    float center_a = h * 0.42f + h * 0.10f * sinf(x * 0.011f + 0.8f) + h * 0.03f * sinf(x * 0.031f);
    float strength_a = gaussian(y - center_a, h * 0.045f) * 0.35f;
    float center_b = h * 0.52f + h * 0.07f * sinf(x * 0.008f + 2.6f);
    float strength_b = gaussian(y - center_b, h * 0.03f) * 0.28f;
    pixel = color_lerp(pixel, AURORA_WHITE, strength_a);

    return color_lerp(pixel, AURORA_GREEN, strength_b);
}

/* Grass color at y: crest to foot gradient, with a glowing edge at the ridge plus sun shine. */
static color_t hill_color(const hill_t *hill, int y, float ridge, int h, float shine)
{
    color_t color = color_lerp(hill->crest, hill->foot, clamp01((y - ridge) / (h - ridge)));
    float edge = expf(-(y - ridge) / 3.0f) * hill->edge_strength;
    color.r += edge + shine;
    color.g += edge + shine;
    color.b += edge + shine * 0.6f;

    return color;
}

static color_t add_back_hill(color_t pixel, int x, int y, int h)
{
    float ridge = h * 0.74f + h * 0.05f * sinf(x * 0.009f + 1.0f);
    float coverage = clamp01(y - ridge + 0.5f);
    if (coverage <= 0) {
        return pixel;
    }

    return color_lerp(pixel, hill_color(&BACK_HILL, y, ridge, h, 0), coverage);
}

/* The front hill also reflects the sun on the grass, on the right. */
static color_t add_front_hill(color_t pixel, int x, int y, int h, float sun_x, int w)
{
    float ridge = h * 0.82f + h * 0.06f * sinf(x * 0.007f + 3.0f) + h * 0.015f * sinf(x * 0.023f);
    float coverage = clamp01(y - ridge + 0.5f);
    if (coverage <= 0) {
        return pixel;
    }
    float shine = gaussian(x - sun_x, w * 0.25f) * expf(-(y - ridge) / 12.0f) * 0.35f;

    return color_lerp(pixel, hill_color(&FRONT_HILL, y, ridge, h, shine), coverage);
}

static void draw_bokeh_disc(uint32_t *px, int w, int h, int center_x, int center_y, int radius)
{
    for (int dy = -radius; dy <= radius; dy += 1) {
        for (int dx = -radius; dx <= radius; dx += 1) {
            float distance = sqrtf(dx * dx + dy * dy) / radius;
            if (distance > 1) {
                continue;
            }
            int alpha = (int)((0.25f + 0.75f * powf(distance, 6)) * (1 - powf(distance, 24)) * BOKEH_ALPHA);
            int x = center_x + dx;
            int y = center_y + dy;
            if (x >= 0 && x < w && y >= 0 && y < h) {
                px[y * w + x] = gfx_blend(px[y * w + x], WHITE, alpha);
            }
        }
    }
}

static void draw_sky_bokeh(uint32_t *px, int w, int h)
{
    srand(BOKEH_SEED);
    for (int disc = 0; disc < BOKEH_COUNT; disc += 1) {
        int center_x = rand() % w;
        int center_y = rand() % (h * 2 / 3);
        int radius = BOKEH_MIN_RADIUS + rand() % BOKEH_RADIUS_RANGE;
        draw_bokeh_disc(px, w, h, center_x, center_y, radius);
    }
}

void aero_background(uint32_t *px, int w, int h, uint32_t tint)
{
    color_t tint_color = color_from_rgb(tint);
    const float sun_x = w * SUN_X_RATIO;
    const float sun_y = h * SUN_Y_RATIO;
    for (int y = 0; y < h; y += 1) {
        for (int x = 0; x < w; x += 1) {
            color_t pixel = sky_gradient((float)y / h);
            if (tint) {
                pixel = color_lerp(pixel, tint_color, TINT_STRENGTH);
            }
            pixel = add_sun_light(pixel, x - sun_x, y - sun_y, w);
            pixel = add_aurora(pixel, x, y, h);
            pixel = add_back_hill(pixel, x, y, h);
            pixel = add_front_hill(pixel, x, y, h, sun_x, w);
            px[y * w + x] = rgb(pixel.r, pixel.g, pixel.b);
        }
    }
    gfx_target(px, w, h);
    draw_sky_bokeh(px, w, h);
}

/* Color and alpha of one soap bubble pixel, at (fx, fy) in -1..1 from the center. */
static void bubble_pixel(float fx, float fy, int radius, color_t *color, float *alpha)
{
    float distance = sqrtf(fx * fx + fy * fy);
    color->r = 0.85f;
    color->g = 0.97f;
    color->b = 1.0f;
    *alpha = 0;
    if (distance > BUBBLE_EDGE) {
        return;
    }
    float soft_edge = clamp01((BUBBLE_EDGE - distance) * radius);
    float opacity = (0.10f + 0.65f * powf(distance, 5)) * soft_edge; /* rim stronger than the middle */
    /* light iridescence on the rim */
    color->r = 0.80f + 0.2f * sinf(fx * 3 + 1);
    color->g = 0.92f + 0.08f * sinf(fy * 3);
    color->b = 1.0f;
    /* main reflection, upper left */
    float main_x = (fx + 0.38f) / 0.36f;
    float main_y = (fy + 0.42f) / 0.22f;
    float main_distance = main_x * main_x + main_y * main_y;
    if (main_distance < 1) {
        float strength = 1 - main_distance;
        opacity = opacity + (1 - opacity) * strength * 0.9f;
        color->r = lerpf(color->r, 1, strength);
        color->g = lerpf(color->g, 1, strength);
    }
    /* small reflection, lower right */
    float small_x = (fx - 0.45f) / 0.15f;
    float small_y = (fy - 0.5f) / 0.1f;
    float small_distance = small_x * small_x + small_y * small_y;
    if (small_distance < 1) {
        opacity = opacity + (1 - opacity) * (1 - small_distance) * 0.5f;
    }
    *alpha = opacity;
}

static void build_bubble(bubble_sprite_t *sprite, int radius)
{
    sprite->r = radius;
    sprite->size = radius * 2 + 1;
    size_t pixel_count = (size_t)sprite->size * (size_t)sprite->size;
    sprite->color = malloc(pixel_count * sizeof(uint32_t));
    sprite->alpha = malloc(pixel_count);
    for (int y = 0; y < sprite->size; y += 1) {
        for (int x = 0; x < sprite->size; x += 1) {
            float fx = (x - radius) / (float)radius;
            float fy = (y - radius) / (float)radius;
            color_t color;
            float alpha;
            bubble_pixel(fx, fy, radius, &color, &alpha);
            sprite->color[y * sprite->size + x] = rgb(color.r, color.g, color.b);
            sprite->alpha[y * sprite->size + x] = (uint8_t)(clamp01(alpha) * 255);
        }
    }
}

void aero_init(int w, int h)
{
    field.screen_w = w;
    field.screen_h = h;
    build_bubble(&field.sprites[0], 7);
    build_bubble(&field.sprites[1], 12);
    build_bubble(&field.sprites[2], 19);
    srand(BUBBLE_SEED);
    for (int index = 0; index < BUBBLE_COUNT; index += 1) {
        bubble_t *bubble = &field.bubbles[index];
        bubble->x = rand() % w;
        bubble->y = rand() % h;
        bubble->size_class = index % BUBBLE_SIZES;
        bubble->speed = 6 + rand() % 10 + bubble->size_class * 3; /* px per second */
        bubble->phase = (rand() % BUBBLE_PHASE_STEPS) / 100.0f;
        bubble->sway = 6 + rand() % 14;
    }
}

static inline void blit_bubble(uint32_t *px, const bubble_sprite_t *sprite, int left, int top)
{
    const int screen_w = field.screen_w;
    const int screen_h = field.screen_h;
    const int size = sprite->size;
    for (int row = 0; row < size; row += 1) {
        int y = top + row;
        if ((unsigned)y >= (unsigned)screen_h) {
            continue;
        }
        for (int column = 0; column < size; column += 1) {
            int x = left + column;
            int alpha = sprite->alpha[row * size + column];
            if (!alpha || (unsigned)x >= (unsigned)screen_w) {
                continue;
            }
            uint32_t *pixel = &px[y * screen_w + x];
            *pixel = gfx_blend(*pixel, sprite->color[row * size + column], alpha);
        }
    }
}

void aero_bubbles(uint32_t *px, float secs)
{
    for (int index = 0; index < BUBBLE_COUNT; index += 1) {
        const bubble_t *bubble = &field.bubbles[index];
        const bubble_sprite_t *sprite = &field.sprites[bubble->size_class];
        float y = fmodf(bubble->y - secs * bubble->speed, field.screen_h + sprite->size * 2.0f);
        if (y < 0) {
            y += field.screen_h + sprite->size * 2.0f;
        }
        y -= sprite->size;
        int left = (int)(bubble->x + sinf(secs * 0.6f + bubble->phase) * bubble->sway) - sprite->r;
        int top = (int)y - sprite->r;
        blit_bubble(px, sprite, left, top);
    }
}

void aero_glass(int x, int y, int w, int h, int radius, int alpha)
{
    gfx_round_rect(x - 1, y - 1, w + 2, h + 2, radius + 1, 0x1a5f8f, alpha / 5);          /* thin blue outline */
    gfx_round_rect_grad(x, y, w, h, radius, WHITE, 0xe4f4ff, alpha);                      /* frosted glass */
    gfx_round_rect_grad(x + 2, y + 2, w - 4, h / 3, radius - 1, WHITE, WHITE, alpha / 2); /* shine at the top */
}

void aero_pill(int x, int y, int w, int h, int radius, uint32_t color, int alpha)
{
    gfx_round_rect(x + 2, y + 4, w, h, radius, 0x0a3a5a, alpha / 4); /* shadow (bluish, never black) */
    gfx_round_rect_grad(x, y, w, h, radius, gfx_blend(color, WHITE, 70), gfx_blend(color, DEEP_NAVY, 70), alpha);
    /* glass reflection on the upper half */
    gfx_round_rect_grad(x + 3, y + 2, w - 6, h * 46 / 100, radius - 2, gfx_blend(color, WHITE, 230),
                        gfx_blend(color, WHITE, 120), alpha * 170 / OPAQUE);
    /* bounced light at the bottom */
    gfx_round_rect_grad(x + radius / 2, y + h * 70 / 100, w - radius, h * 25 / 100, radius / 2,
                        gfx_blend(color, DEEP_NAVY, 40), gfx_blend(color, WHITE, 110), alpha * 120 / OPAQUE);
}

void aero_orb(int cx, int cy, int radius, uint32_t color, int alpha)
{
    aero_pill(cx - radius, cy - radius, radius * 2, radius * 2, radius, color, alpha);
}

void aero_text(const font_t *font, int x, int y, const char *text, uint32_t color, int alpha)
{
    /* light text gets a dark blue shadow, dark text a white halo */
    uint32_t shadow = (color & 0xff) > 0x80 ? 0x0a3050 : WHITE;
    gfx_text(font, x + 1, y + 1, text, shadow, alpha * 110 / OPAQUE);
    gfx_text(font, x, y, text, color, alpha);
}
