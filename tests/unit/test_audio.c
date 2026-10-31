#include <stdlib.h>
#include "chip.h"
#include "check.h"
#include "music.h"

#define RATE 48000
#define CHANNELS 2
#define BYTES_PER_FRAME 4
#define SILENCE_FRAMES 2400
#define INTRO_FRAMES 153600
#define LOOP_FRAMES 2457600

static long energy(const int16_t *samples, long count)
{
    long sum = 0;
    for (long index = 0; index < count; index += 1) {
        sum += labs((long)samples[index]);
    }

    return sum;
}

static int16_t *render_loop(long *frames)
{
    chip_init(RATE);
    long intro_frames = chip_intro_frames();
    long loop_frames = chip_loop_frames();
    int16_t *intro = malloc(intro_frames * BYTES_PER_FRAME);
    int16_t *loop = malloc(loop_frames * BYTES_PER_FRAME);
    chip_intro(intro);
    chip_loop(loop);
    free(intro);
    *frames = loop_frames;

    return loop;
}

static void test_chip_is_deterministic(void)
{
    long first_frames;
    long second_frames;
    int16_t *first = render_loop(&first_frames);
    int16_t *second = render_loop(&second_frames);
    CHECK(first_frames == second_frames);
    CHECK(first_frames > RATE);
    CHECK(memcmp(first, second, first_frames * BYTES_PER_FRAME) == 0);
    CHECK(energy(first, first_frames * CHANNELS) > 0);
    free(first);
    free(second);
}

static void test_chip_lengths_match_2_and_32_bars_at_150_bpm(void)
{
    chip_init(RATE);
    CHECK(chip_intro_frames() == INTRO_FRAMES);
    CHECK(chip_loop_frames() == LOOP_FRAMES);
}

static void test_chip_writes_both_channels_with_the_same_sample(void)
{
    long frames;
    int16_t *loop = render_loop(&frames);
    int mismatches = 0;
    for (long frame = 0; frame < frames; frame += 1) {
        mismatches += loop[frame * 2] != loop[frame * 2 + 1];
    }
    CHECK(mismatches == 0);
    free(loop);
}

static void test_every_effect_makes_sound(void)
{
    static int16_t stereo[RATE * CHANNELS];
    for (int effect = 0; effect < SFX_COUNT; effect += 1) {
        music_init(RATE);
        music_effects_only(1);
        music_effect(effect);
        music_render(stereo, RATE / 2);
        CHECK(energy(stereo, RATE) > 0);
    }
}

static void test_silence_without_effects(void)
{
    static int16_t stereo[SILENCE_FRAMES * CHANNELS];
    music_init(RATE);
    music_effects_only(1);
    music_render(stereo, SILENCE_FRAMES);
    CHECK(energy(stereo, SILENCE_FRAMES * CHANNELS) == 0);
}

static void test_init_clears_the_echo_of_the_previous_effect(void)
{
    static int16_t stereo[SILENCE_FRAMES * CHANNELS];
    music_init(RATE);
    music_effects_only(1);
    music_effect(SFX_SYSTEM);
    music_render(stereo, SILENCE_FRAMES);
    music_init(RATE);
    music_effects_only(1);
    music_render(stereo, SILENCE_FRAMES);
    CHECK(energy(stereo, SILENCE_FRAMES * CHANNELS) == 0);
}

static void test_rendering_zero_frames_leaves_the_buffer_alone(void)
{
    int16_t stereo[CHANNELS] = {1234, -1234};
    music_init(RATE);
    music_render(stereo, 0);
    CHECK(stereo[0] == 1234);
    CHECK(stereo[1] == -1234);
}

static void test_music_plays_when_the_sequencer_runs(void)
{
    static int16_t stereo[RATE * CHANNELS];
    music_init(RATE);
    music_effects_only(0);
    music_volume(1);
    music_render(stereo, RATE);
    CHECK(energy(stereo, RATE * CHANNELS) > 0);
}

int main(void)
{
    test_chip_is_deterministic();
    test_chip_lengths_match_2_and_32_bars_at_150_bpm();
    test_chip_writes_both_channels_with_the_same_sample();
    test_every_effect_makes_sound();
    test_silence_without_effects();
    test_init_clears_the_echo_of_the_previous_effect();
    test_rendering_zero_frames_leaves_the_buffer_alone();
    test_music_plays_when_the_sequencer_runs();
    TEST_END();
}
