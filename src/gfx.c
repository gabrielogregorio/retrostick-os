/* Software drawing on a 0x00RRGGBB buffer. No dependencies at all. */
#include "gfx.h"

#define OPAQUE 255
#define RED_BLUE_MASK 0xff00ff
#define GREEN_MASK 0x00ff00

#define FIRST_GLYPH 32
#define LAST_GLYPH 255
#define UNKNOWN_CHAR '?'
#define UTF8_TWO_BYTE_LEAD 0xc0
#define UTF8_THREE_BYTE_LEAD 0xe0
#define UTF8_CONTINUATION 0x80
#define UTF8_CONTINUATION_MASK 0xc0

#define STAR_VERTICES 10
#define STAR_FIRST_ANGLE (-1.5707963f) /* point up */
#define STAR_ANGLE_STEP 0.62831853f
#define STAR_INNER_RATIO 0.46f
#define STAR_SAMPLES_PER_AXIS 3
#define STAR_SAMPLES_PER_PIXEL 9

static struct {
    uint32_t *pixels;
    int width, height;
    int clip_top, clip_bottom; /* band of rows where drawing is allowed */
} canvas;

void gfx_target(uint32_t *px, int w, int h)
{
    canvas.pixels = px;
    canvas.width = w;
    canvas.height = h;
    canvas.clip_top = 0;
    canvas.clip_bottom = h;
}

void gfx_clip_y(int y0, int y1)
{
    canvas.clip_top = y0 < 0 ? 0 : y0;
    canvas.clip_bottom = y1 > canvas.height ? canvas.height : y1;
}

uint32_t gfx_blend(uint32_t from, uint32_t to, int weight)
{
    uint32_t red_blue = ((from & RED_BLUE_MASK) * (OPAQUE - weight) + (to & RED_BLUE_MASK) * weight) >> 8;
    uint32_t green = ((from & GREEN_MASK) * (OPAQUE - weight) + (to & GREEN_MASK) * weight) >> 8;

    return (red_blue & RED_BLUE_MASK) | (green & GREEN_MASK);
}

static inline void plot(int x, int y, uint32_t color, int alpha)
{
    if ((unsigned)x >= (unsigned)canvas.width || y < canvas.clip_top || y >= canvas.clip_bottom) {
        return;
    }
    uint32_t *pixel = &canvas.pixels[y * canvas.width + x];
    *pixel = alpha >= OPAQUE ? color : gfx_blend(*pixel, color, alpha);
}

void gfx_gradient_v(uint32_t top, uint32_t bottom)
{
    for (int y = 0; y < canvas.height; y += 1) {
        uint32_t color = gfx_blend(top, bottom, y * OPAQUE / (canvas.height - 1));
        uint32_t *row = &canvas.pixels[y * canvas.width];
        for (int x = 0; x < canvas.width; x += 1) {
            row[x] = color;
        }
    }
}

void gfx_rect(int x, int y, int w, int h, uint32_t color, int alpha)
{
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    int right = x + w > canvas.width ? canvas.width : x + w;
    int bottom = y + h > canvas.height ? canvas.height : y + h;
    for (int row = top; row < bottom; row += 1) {
        for (int column = left; column < right; column += 1) {
            plot(column, row, color, alpha);
        }
    }
}

void gfx_round_rect(int x, int y, int w, int h, int radius, uint32_t color, int alpha)
{
    gfx_round_rect_grad(x, y, w, h, radius, color, color, alpha);
}

static inline void hline(uint32_t *dst, int count, uint32_t color, int alpha)
{
    if (alpha >= OPAQUE) {
        for (int index = 0; index < count; index += 1) {
            dst[index] = color;
        }

        return;
    }
    uint32_t color_red_blue = (color & RED_BLUE_MASK) * alpha;
    uint32_t color_green = (color & GREEN_MASK) * alpha;
    int inverse = OPAQUE - alpha;
    for (int index = 0; index < count; index += 1) {
        uint32_t value = dst[index];
        uint32_t red_blue = ((value & RED_BLUE_MASK) * inverse + color_red_blue) >> 8;
        uint32_t green = ((value & GREEN_MASK) * inverse + color_green) >> 8;
        dst[index] = (red_blue & RED_BLUE_MASK) | (green & GREEN_MASK);
    }
}

/* How many rows row sits inside the top or bottom corner band: 0 on the straight middle. */
static inline int corner_depth(int row, int h, int radius)
{
    if (row < radius) {
        return radius - row;
    }
    if (row >= h - radius) {
        return row - (h - radius - 1);
    }

    return 0;
}

static inline void draw_corner_pixels(int x, int y, int w, int radius, int dy, uint32_t color, int alpha)
{
    int radius_squared = radius * radius;
    for (int column = 0; column < radius; column += 1) {
        int dx = radius - column;
        int distance_squared = dx * dx + dy * dy;
        int edge_alpha = alpha;
        if (distance_squared > radius_squared + radius) {
            continue;
        }
        if (distance_squared > radius_squared - radius) {
            edge_alpha = alpha * (radius_squared + radius - distance_squared) / (2 * radius);
        }
        plot(x + column, y, color, edge_alpha);
        plot(x + w - 1 - column, y, color, edge_alpha);
    }
}

void gfx_round_rect_grad(int x, int y, int w, int h, int radius, uint32_t top, uint32_t bottom, int alpha)
{
    if (alpha <= 0) {
        return;
    }
    for (int row = 0; row < h; row += 1) {
        int screen_y = y + row;
        if (screen_y < canvas.clip_top || screen_y >= canvas.clip_bottom) {
            continue;
        }
        uint32_t color = h > 1 ? gfx_blend(top, bottom, row * OPAQUE / (h - 1)) : top;
        int dy = corner_depth(row, h, radius);
        int span_start = 0;
        int span_end = w;
        if (dy) {
            span_start = radius;
            span_end = w - radius;
            draw_corner_pixels(x, screen_y, w, radius, dy, color, alpha);
        }
        int left = x + span_start;
        int right = x + span_end;
        if (left < 0) {
            left = 0;
        }
        if (right > canvas.width) {
            right = canvas.width;
        }
        if (right > left) {
            hline(&canvas.pixels[screen_y * canvas.width + left], right - left, color, alpha);
        }
    }
}

/* UTF-8 to Latin-1: whatever does not fit becomes UNKNOWN_CHAR. */
static int next_char(const char **text)
{
    const unsigned char *cursor = (const unsigned char *)*text;
    int code = *cursor;
    cursor += 1;
    if (code >= UTF8_TWO_BYTE_LEAD && code < UTF8_THREE_BYTE_LEAD &&
        (*cursor & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION) {
        code = ((code & 0x1f) << 6) | (*cursor & 0x3f);
        cursor += 1;
    } else if (code >= UTF8_THREE_BYTE_LEAD) {
        while ((*cursor & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION) {
            cursor += 1;
        }
        code = UNKNOWN_CHAR;
    }
    *text = (const char *)cursor;

    return (code < FIRST_GLYPH || code > LAST_GLYPH) ? UNKNOWN_CHAR : code;
}

int gfx_width(const font_t *font, const char *utf8)
{
    int width = 0;
    while (*utf8) {
        width += font->g[next_char(&utf8) - FIRST_GLYPH].adv;
    }

    return width;
}

static inline void draw_glyph(const font_t *font, const glyph_t *glyph, int x, int y, uint32_t color, int alpha)
{
    const uint8_t *coverage = font->a + glyph->off;
    for (int row = 0; row < glyph->h; row += 1) {
        for (int column = 0; column < glyph->w; column += 1) {
            int value = coverage[row * glyph->w + column];
            if (value) {
                plot(x + glyph->x + column, y + glyph->y + row, color, value * alpha / OPAQUE);
            }
        }
    }
}

/* y is the text baseline */
int gfx_text(const font_t *font, int x, int y, const char *utf8, uint32_t color, int alpha)
{
    int top = y - font->ascent;
    while (*utf8) {
        const glyph_t *glyph = &font->g[next_char(&utf8) - FIRST_GLYPH];
        draw_glyph(font, glyph, x, top, color, alpha);
        x += glyph->adv;
    }

    return x;
}

static int inside_star(float x, float y, const float *vertex_x, const float *vertex_y)
{
    int inside = 0;
    int previous = STAR_VERTICES - 1;
    for (int vertex = 0; vertex < STAR_VERTICES; vertex += 1) {
        float rise = vertex_y[previous] - vertex_y[vertex];
        float run = vertex_x[previous] - vertex_x[vertex];
        if (((vertex_y[vertex] > y) != (vertex_y[previous] > y)) &&
            (x < run * (y - vertex_y[vertex]) / rise + vertex_x[vertex])) {
            inside = !inside;
        }
        previous = vertex;
    }

    return inside;
}

static int star_coverage(int x, int y, const float *vertex_x, const float *vertex_y)
{
    int covered = 0;
    for (int sample_y = 0; sample_y < STAR_SAMPLES_PER_AXIS; sample_y += 1) {
        for (int sample_x = 0; sample_x < STAR_SAMPLES_PER_AXIS; sample_x += 1) {
            float offset_x = (sample_x + 0.5f) / STAR_SAMPLES_PER_AXIS;
            float offset_y = (sample_y + 0.5f) / STAR_SAMPLES_PER_AXIS;
            covered += inside_star(x + offset_x, y + offset_y, vertex_x, vertex_y);
        }
    }

    return covered;
}

/* 5-pointed star, edge smoothed with 3x3 samples, top-to-bottom gradient. */
void gfx_star(int cx, int cy, int radius, uint32_t top, uint32_t bottom, int alpha)
{
    float vertex_x[STAR_VERTICES];
    float vertex_y[STAR_VERTICES];
    for (int vertex = 0; vertex < STAR_VERTICES; vertex += 1) {
        float angle = STAR_FIRST_ANGLE + vertex * STAR_ANGLE_STEP;
        float length = (vertex & 1) ? radius * STAR_INNER_RATIO : radius;
        vertex_x[vertex] = cx + length * __builtin_cosf(angle);
        vertex_y[vertex] = cy + length * __builtin_sinf(angle);
    }
    for (int y = cy - radius; y <= cy + radius; y += 1) {
        for (int x = cx - radius; x <= cx + radius; x += 1) {
            int covered = star_coverage(x, y, vertex_x, vertex_y);
            if (covered) {
                uint32_t color = gfx_blend(top, bottom, (y - cy + radius) * OPAQUE / (2 * radius + 1));
                plot(x, y, color, alpha * covered / STAR_SAMPLES_PER_PIXEL);
            }
        }
    }
}
