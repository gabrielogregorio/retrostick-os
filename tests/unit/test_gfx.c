#include <stdint.h>
#include "check.h"
#include "gfx.h"

#define W 32
#define H 16
#define GUARD 0xdeadbeef

static uint32_t mem[W * H + 8];
static uint32_t *px = mem + 4;

static void reset_canvas(void)
{
    for (int i = 0; i < W * H + 8; i += 1) {
        mem[i] = GUARD;
    }
    gfx_target(px, W, H);
    gfx_rect(0, 0, W, H, 0x000000, 255);
}

static int guards_intact(void)
{
    for (int i = 0; i < 4; i += 1) {
        if (mem[i] != GUARD || mem[W * H + 4 + i] != GUARD) {
            return 0;
        }
    }

    return 1;
}

static void test_blend(void)
{
    CHECK(gfx_blend(0x102030, 0xffffff, 0) == 0x0f1f2f || gfx_blend(0x102030, 0xffffff, 0) == 0x102030);
    uint32_t mid = gfx_blend(0x000000, 0xfefefe, 128);
    CHECK((mid & 0xff) >= 0x7c && (mid & 0xff) <= 0x80);
    CHECK((gfx_blend(0, 0xffffff, 255) & 0xff) >= 0xfd);
}

static void test_rect_is_clipped_to_the_canvas(void)
{
    reset_canvas();
    gfx_rect(-5, -5, 100, 100, 0xff0000, 255);
    CHECK(px[0] == 0xff0000);
    CHECK(px[W * H - 1] == 0xff0000);
    CHECK(guards_intact());
    reset_canvas();
    gfx_rect(W + 3, H + 3, 4, 4, 0xff0000, 255);
    CHECK(px[W * H - 1] == 0x000000);
    CHECK(guards_intact());
}

static void test_clip_y_limits_rows(void)
{
    reset_canvas();
    gfx_clip_y(4, 8);
    gfx_rect(0, 0, W, H, 0x00ff00, 255);
    gfx_clip_y(0, H);
    CHECK(px[3 * W] == 0x000000);
    CHECK(px[4 * W] == 0x00ff00);
    CHECK(px[7 * W] == 0x00ff00);
    CHECK(px[8 * W] == 0x000000);
}

static void test_round_rect_keeps_corners(void)
{
    reset_canvas();
    gfx_round_rect(2, 2, 20, 10, 5, 0xffffff, 255);
    CHECK(px[2 * W + 2] == 0x000000);
    CHECK((px[7 * W + 12] & 0xff) > 0xf0);
    CHECK(guards_intact());
}

static void test_text(void)
{
    reset_canvas();
    CHECK(gfx_width(&font_small, "") == 0);
    CHECK(gfx_width(&font_small, "AB") > gfx_width(&font_small, "A"));
    gfx_text(&font_small, 1, 14, "Hi", 0xffffff, 255);
    int lit = 0;
    for (int i = 0; i < W * H; i += 1) {
        lit += px[i] != 0;
    }
    CHECK(lit > 0);
    gfx_text(&font_big, 28, 15, "very long text past the edge", 0xffffff, 255);
    CHECK(guards_intact());
}

int main(void)
{
    test_blend();
    test_rect_is_clipped_to_the_canvas();
    test_clip_y_limits_rows();
    test_round_rect_keeps_corners();
    test_text();
    TEST_END();
}
