#include <stdint.h>
#include "check.h"
#include "gfx.h"

#define CANVAS_W 32
#define CANVAS_H 16
#define CANVAS_PIXELS (CANVAS_W * CANVAS_H)
#define GUARD_PIXELS 4
#define GUARD 0xdeadbeef
#define BLACK 0x000000
#define WHITE 0xffffff
#define RED 0xff0000
#define GREEN 0x00ff00
#define OPAQUE 255
/* A shaped rectangle takes every row through gfx_blend, which loses one level to its shift. */
#define RED_THROUGH_GRADIENT 0xfe0000
#define GREEN_THROUGH_GRADIENT 0x00fe00
#define WHITE_THROUGH_GRADIENT 0xfefefe

static uint32_t memory[CANVAS_PIXELS + 2 * GUARD_PIXELS];
static uint32_t *const canvas = memory + GUARD_PIXELS;

static void reset_canvas(void)
{
    for (size_t index = 0; index < sizeof memory / sizeof memory[0]; index += 1) {
        memory[index] = GUARD;
    }
    gfx_target(canvas, CANVAS_W, CANVAS_H);
    gfx_rect(0, 0, CANVAS_W, CANVAS_H, BLACK, OPAQUE);
}

static int guards_intact(void)
{
    for (int index = 0; index < GUARD_PIXELS; index += 1) {
        if (memory[index] != GUARD || memory[CANVAS_PIXELS + GUARD_PIXELS + index] != GUARD) {
            return 0;
        }
    }

    return 1;
}

static uint32_t pixel(int x, int y)
{
    return canvas[y * CANVAS_W + x];
}

static int lit_pixels(void)
{
    int lit = 0;
    for (int index = 0; index < CANVAS_PIXELS; index += 1) {
        lit += canvas[index] != BLACK;
    }

    return lit;
}

static void test_blend_at_weight_zero_keeps_the_first_color_truncated_by_the_shift(void)
{
    CHECK(gfx_blend(0x102030, WHITE, 0) == 0x0f1f2f);
    CHECK(gfx_blend(BLACK, WHITE, 0) == BLACK);
}

static void test_blend_at_full_weight_reaches_the_second_color_minus_the_shift_loss(void)
{
    CHECK(gfx_blend(BLACK, WHITE, OPAQUE) == 0xfefefe);
    CHECK(gfx_blend(BLACK, 0x804020, OPAQUE) == 0x7f3f1f);
}

static void test_blend_at_the_middle_weight_lands_between_both_colors(void)
{
    CHECK(gfx_blend(BLACK, 0xfefefe, 128) == 0x7f7f7f);
    CHECK(gfx_blend(WHITE, BLACK, 127) == 0x7f7f7f);
}

static void test_blend_keeps_the_channels_apart(void)
{
    CHECK(gfx_blend(RED, BLACK, 0) == 0xfe0000);
    CHECK(gfx_blend(BLACK, GREEN, OPAQUE) == 0x00fe00);
    CHECK(gfx_blend(BLACK, 0x0000ff, OPAQUE) == 0x0000fe);
}

static void test_rect_is_clipped_to_the_canvas(void)
{
    reset_canvas();
    gfx_rect(-5, -5, 100, 100, RED, OPAQUE);
    CHECK(canvas[0] == RED);
    CHECK(canvas[CANVAS_PIXELS - 1] == RED);
    CHECK(guards_intact());
}

static void test_rect_past_the_canvas_draws_nothing(void)
{
    reset_canvas();
    gfx_rect(CANVAS_W + 3, CANVAS_H + 3, 4, 4, RED, OPAQUE);
    CHECK(canvas[CANVAS_PIXELS - 1] == BLACK);
    CHECK(lit_pixels() == 0);
    CHECK(guards_intact());
}

static void test_rect_fills_exactly_its_edges(void)
{
    reset_canvas();
    gfx_rect(2, 3, 4, 2, RED, OPAQUE);
    CHECK(pixel(2, 3) == RED);
    CHECK(pixel(5, 4) == RED);
    CHECK(pixel(1, 3) == BLACK);
    CHECK(pixel(6, 3) == BLACK);
    CHECK(pixel(2, 2) == BLACK);
    CHECK(pixel(2, 5) == BLACK);
    CHECK(lit_pixels() == 8);
}

static void test_rect_on_the_last_pixel_touches_only_that_pixel(void)
{
    reset_canvas();
    gfx_rect(CANVAS_W - 1, CANVAS_H - 1, 1, 1, RED, OPAQUE);
    CHECK(pixel(CANVAS_W - 1, CANVAS_H - 1) == RED);
    CHECK(lit_pixels() == 1);
    CHECK(guards_intact());
}

static void test_rect_ending_at_the_left_edge_draws_nothing(void)
{
    reset_canvas();
    gfx_rect(-1, 0, 1, CANVAS_H, RED, OPAQUE);
    CHECK(lit_pixels() == 0);
}

static void test_rect_starting_at_the_right_edge_draws_nothing(void)
{
    reset_canvas();
    gfx_rect(CANVAS_W, 0, 1, CANVAS_H, RED, OPAQUE);
    CHECK(lit_pixels() == 0);
    CHECK(guards_intact());
}

static void test_rect_with_zero_size_draws_nothing(void)
{
    reset_canvas();
    gfx_rect(4, 4, 0, 5, RED, OPAQUE);
    gfx_rect(4, 4, 5, 0, RED, OPAQUE);
    CHECK(lit_pixels() == 0);
}

static void test_rect_blends_with_partial_alpha(void)
{
    reset_canvas();
    gfx_rect(0, 0, 1, 1, WHITE, 128);
    CHECK(pixel(0, 0) == 0x7f7f7f);
    gfx_rect(1, 0, 1, 1, WHITE, 0);
    CHECK(pixel(1, 0) == BLACK);
}

static void test_clip_y_limits_rows(void)
{
    reset_canvas();
    gfx_clip_y(4, 8);
    gfx_rect(0, 0, CANVAS_W, CANVAS_H, GREEN, OPAQUE);
    gfx_clip_y(0, CANVAS_H);
    CHECK(canvas[3 * CANVAS_W] == BLACK);
    CHECK(canvas[4 * CANVAS_W] == GREEN);
    CHECK(canvas[7 * CANVAS_W] == GREEN);
    CHECK(canvas[8 * CANVAS_W] == BLACK);
}

static void test_clip_y_is_limited_to_the_canvas(void)
{
    reset_canvas();
    gfx_clip_y(-10, CANVAS_H + 10);
    gfx_round_rect(0, -4, CANVAS_W, CANVAS_H + 8, 0, GREEN, OPAQUE);
    gfx_clip_y(0, CANVAS_H);
    CHECK(pixel(0, 0) == GREEN_THROUGH_GRADIENT);
    CHECK(pixel(CANVAS_W - 1, CANVAS_H - 1) == GREEN_THROUGH_GRADIENT);
    CHECK(guards_intact());
}

static void test_gfx_target_resets_the_clipping(void)
{
    reset_canvas();
    gfx_clip_y(4, 8);
    gfx_target(canvas, CANVAS_W, CANVAS_H);
    gfx_rect(0, 0, 1, 1, GREEN, OPAQUE);
    CHECK(pixel(0, 0) == GREEN);
}

static void test_gradient_runs_from_the_top_color_to_the_bottom_color(void)
{
    reset_canvas();
    gfx_gradient_v(RED, WHITE);
    CHECK(pixel(0, 0) == gfx_blend(RED, WHITE, 0));
    CHECK(pixel(CANVAS_W - 1, 0) == gfx_blend(RED, WHITE, 0));
    CHECK(pixel(0, CANVAS_H - 1) == gfx_blend(RED, WHITE, OPAQUE));
    CHECK(guards_intact());
}

static void test_round_rect_keeps_corners(void)
{
    reset_canvas();
    gfx_round_rect(2, 2, 20, 10, 5, WHITE, OPAQUE);
    CHECK(canvas[2 * CANVAS_W + 2] == BLACK);
    CHECK((canvas[7 * CANVAS_W + 12] & 0xff) > 0xf0);
    CHECK(guards_intact());
}

static void test_round_rect_with_no_radius_is_a_plain_rectangle(void)
{
    reset_canvas();
    gfx_round_rect(2, 2, 4, 3, 0, RED, OPAQUE);
    CHECK(pixel(2, 2) == RED_THROUGH_GRADIENT);
    CHECK(pixel(5, 4) == RED_THROUGH_GRADIENT);
    CHECK(lit_pixels() == 12);
}

static void test_round_rect_with_zero_alpha_draws_nothing(void)
{
    reset_canvas();
    gfx_round_rect(2, 2, 20, 10, 5, WHITE, 0);
    CHECK(lit_pixels() == 0);
}

static void test_round_rect_past_every_edge_stays_inside_the_canvas(void)
{
    reset_canvas();
    gfx_round_rect(-6, -6, CANVAS_W + 12, CANVAS_H + 12, 8, WHITE, OPAQUE);
    CHECK(pixel(CANVAS_W / 2, CANVAS_H / 2) == WHITE_THROUGH_GRADIENT);
    CHECK(guards_intact());
}

static void test_round_rect_grad_of_one_row_uses_the_top_color(void)
{
    reset_canvas();
    gfx_round_rect_grad(0, 0, 4, 1, 0, RED, GREEN, OPAQUE);
    CHECK(pixel(0, 0) == RED);
}

static void test_round_rect_grad_runs_from_top_to_bottom(void)
{
    reset_canvas();
    gfx_round_rect_grad(0, 0, 4, 4, 0, RED, WHITE, OPAQUE);
    CHECK(pixel(0, 0) == gfx_blend(RED, WHITE, 0));
    CHECK(pixel(0, 3) == gfx_blend(RED, WHITE, OPAQUE));
}

static void test_empty_text_has_zero_width(void)
{
    CHECK(gfx_width(&font_small, "") == 0);
}

static void test_longer_text_is_wider(void)
{
    CHECK(gfx_width(&font_small, "AB") > gfx_width(&font_small, "A"));
}

static void test_utf8_two_byte_char_measures_like_its_latin1_glyph(void)
{
    CHECK(gfx_width(&font_small, "\xc3\xa9") == font_small.g[0xe9 - 32].adv);
}

static void test_utf8_char_outside_latin1_measures_like_a_question_mark(void)
{
    CHECK(gfx_width(&font_small, "\xe2\x82\xac") == gfx_width(&font_small, "?"));
}

static void test_truncated_utf8_lead_byte_does_not_read_past_the_end(void)
{
    CHECK(gfx_width(&font_small, "\xc3") > 0);
}

static void test_text_returns_the_pen_after_the_last_glyph(void)
{
    reset_canvas();
    CHECK(gfx_text(&font_small, 3, 14, "Hi", WHITE, OPAQUE) == 3 + gfx_width(&font_small, "Hi"));
}

static void test_text_lights_pixels(void)
{
    reset_canvas();
    gfx_text(&font_small, 1, 14, "Hi", WHITE, OPAQUE);
    CHECK(lit_pixels() > 0);
}

static void test_text_past_the_edge_stays_inside_the_canvas(void)
{
    reset_canvas();
    gfx_text(&font_big, 28, 15, "very long text past the edge", WHITE, OPAQUE);
    CHECK(guards_intact());
}

static void test_star_fills_its_center_and_leaves_the_corners_empty(void)
{
    reset_canvas();
    gfx_star(8, 8, 7, WHITE, WHITE, OPAQUE);
    CHECK(pixel(8, 8) != BLACK);
    CHECK(pixel(1, 1) == BLACK);
    CHECK(pixel(15, 1) == BLACK);
    CHECK(guards_intact());
}

static void test_star_past_the_edge_stays_inside_the_canvas(void)
{
    reset_canvas();
    gfx_star(0, 0, 10, WHITE, WHITE, OPAQUE);
    gfx_star(CANVAS_W - 1, CANVAS_H - 1, 10, WHITE, WHITE, OPAQUE);
    CHECK(guards_intact());
}

int main(void)
{
    test_blend_at_weight_zero_keeps_the_first_color_truncated_by_the_shift();
    test_blend_at_full_weight_reaches_the_second_color_minus_the_shift_loss();
    test_blend_at_the_middle_weight_lands_between_both_colors();
    test_blend_keeps_the_channels_apart();
    test_rect_is_clipped_to_the_canvas();
    test_rect_past_the_canvas_draws_nothing();
    test_rect_fills_exactly_its_edges();
    test_rect_on_the_last_pixel_touches_only_that_pixel();
    test_rect_ending_at_the_left_edge_draws_nothing();
    test_rect_starting_at_the_right_edge_draws_nothing();
    test_rect_with_zero_size_draws_nothing();
    test_rect_blends_with_partial_alpha();
    test_clip_y_limits_rows();
    test_clip_y_is_limited_to_the_canvas();
    test_gfx_target_resets_the_clipping();
    test_gradient_runs_from_the_top_color_to_the_bottom_color();
    test_round_rect_keeps_corners();
    test_round_rect_with_no_radius_is_a_plain_rectangle();
    test_round_rect_with_zero_alpha_draws_nothing();
    test_round_rect_past_every_edge_stays_inside_the_canvas();
    test_round_rect_grad_of_one_row_uses_the_top_color();
    test_round_rect_grad_runs_from_top_to_bottom();
    test_empty_text_has_zero_width();
    test_longer_text_is_wider();
    test_utf8_two_byte_char_measures_like_its_latin1_glyph();
    test_utf8_char_outside_latin1_measures_like_a_question_mark();
    test_truncated_utf8_lead_byte_does_not_read_past_the_end();
    test_text_returns_the_pen_after_the_last_glyph();
    test_text_lights_pixels();
    test_text_past_the_edge_stays_inside_the_canvas();
    test_star_fills_its_center_and_leaves_the_corners_empty();
    test_star_past_the_edge_stays_inside_the_canvas();
    TEST_END();
}
