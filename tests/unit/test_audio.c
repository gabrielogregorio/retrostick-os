#include <stdlib.h>
#include "chip.h"
#include "check.h"
#include "music.h"

#define RATE 48000

static long energy(const int16_t *s, long n)
{
    long sum = 0;
    for (long i = 0; i < n; i += 1) {
        sum += labs((long)s[i]);
    }

    return sum;
}

static int16_t *render_loop(long *frames)
{
    chip_init(RATE);
    long intro = chip_intro_frames(), loop = chip_loop_frames();
    int16_t *a = malloc(intro * 4), *b = malloc(loop * 4);
    chip_intro(a);
    chip_loop(b);
    free(a);
    *frames = loop;

    return b;
}

static void test_chip_is_deterministic(void)
{
    long n1, n2;
    int16_t *a = render_loop(&n1), *b = render_loop(&n2);
    CHECK(n1 == n2);
    CHECK(n1 > RATE);
    CHECK(memcmp(a, b, n1 * 4) == 0);
    CHECK(energy(a, n1 * 2) > 0);
    free(a);
    free(b);
}

static void test_every_effect_makes_sound(void)
{
    static int16_t buf[RATE * 2];
    for (int i = 0; i < SFX_COUNT; i += 1) {
        music_init(RATE);
        music_effects_only(1);
        music_effect(i);
        music_render(buf, RATE / 2);
        CHECK(energy(buf, RATE) > 0);
    }
}

static void test_silence_without_effects(void)
{
    static int16_t buf[4800];
    music_init(RATE);
    music_effects_only(1);
    music_render(buf, 2400);
    CHECK(energy(buf, 4800) == 0);
}

int main(void)
{
    test_chip_is_deterministic();
    test_every_effect_makes_sound();
    test_silence_without_effects();
    TEST_END();
}
