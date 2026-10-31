/* Generates on the PC the files the stick menu only reads (nothing is synthesized or computed there):
 *   music-intro.pcm  intro of the 8-bit music (bass and drums)
 *   music-loop.pcm   32 bars that join onto themselves
 *   sfx-h.pcm      switch system (horizontal)        sfx-v.pcm  move in the list (vertical)
 *   sfx-a.pcm      open / click                      sfx-b.pcm  back
 *   sfx-f.pcm      favorite                          sfx-d.pcm  unfavorite
 *   sfx-e.pcm      open the game (soft)
 *   aero-background.bin    sky of the Aero theme, 640x360 0x00RRGGBB (too slow to compute on the stick)
 * Audio: 48 kHz, stereo, 16-bit little-endian, no header.
 * Usage: out/gen-audio <output dir> */
#include <math.h>
#include <stdio.h>
#include "../src/aero.h"
#include "../src/chip.h"
#include "../src/music.h"
#include "../src/plat.h"

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BYTES_PER_FRAME 4
#define MAX_LOOP_SECS 60 /* the 8-bit loop lasts 51.2 s */
#define MAX_INTRO_SECS 10
#define EFFECT_FADE_MS 120 /* soft fade-out: no click at the cut */

/* Loop RMS in 16 bits that sits level with the core audio. Rule: never above the loudest console. */
#define MUSIC_TARGET_RMS 1680.0

typedef struct {
    int sfx;
    const char *name;
    int ms;
    float gain;
} effect_file_t;

/* Fixed lengths and per-effect levels, balanced against the game audio.
 * Click, back and unfavorite are low square waves: high bells would be annoying. */
static const effect_file_t EFFECTS[SFX_COUNT] = {
    {SFX_SYSTEM, "sfx-h.pcm", 400, 0.63f},   {SFX_MOVE, "sfx-v.pcm", 350, 0.88f},
    {SFX_OPEN, "sfx-a.pcm", 250, 0.63f},     {SFX_BACK, "sfx-b.pcm", 250, 0.63f},
    {SFX_FAVORITE, "sfx-f.pcm", 450, 0.63f}, {SFX_UNFAVORITE, "sfx-d.pcm", 400, 0.63f},
    {SFX_ENTER, "sfx-e.pcm", 400, 0.5f}, /* open the game: quiet and soft */
};

static int16_t audio[SAMPLE_RATE * MAX_LOOP_SECS * CHANNELS];

static int write_file(const char *dir, const char *name, const void *data, size_t size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *file = fopen(path, "wb");
    if (!file || fwrite(data, 1, size, file) != size) {
        perror(path);

        return 1;
    }
    fclose(file);
    printf("%-18s %8zu bytes\n", name, size);

    return 0;
}

static double loudness_gain(const int16_t *samples, long count)
{
    double sum = 0;
    for (long index = 0; index < count; index += 1) {
        sum += (double)samples[index] * samples[index];
    }

    return MUSIC_TARGET_RMS / sqrt(sum / count);
}

static void apply_gain(int16_t *samples, long count, double gain)
{
    for (long index = 0; index < count; index += 1) {
        samples[index] = (int16_t)(samples[index] * gain);
    }
}

static int generate_music(const char *dir)
{
    static int16_t intro[SAMPLE_RATE * MAX_INTRO_SECS * CHANNELS];
    chip_init(SAMPLE_RATE);
    long intro_frames = chip_intro_frames();
    long loop_frames = chip_loop_frames();
    chip_intro(intro);
    chip_loop(audio);
    double gain = loudness_gain(audio, loop_frames * CHANNELS);
    apply_gain(audio, loop_frames * CHANNELS, gain);
    apply_gain(intro, intro_frames * CHANNELS, gain);
    printf("8-bit music: gain %.3f\n", gain);
    if (write_file(dir, "music-intro.pcm", intro, intro_frames * BYTES_PER_FRAME)) {
        return 1;
    }

    return write_file(dir, "music-loop.pcm", audio, loop_frames * BYTES_PER_FRAME);
}

static void fade_out(int16_t *stereo, int frames, int fade_frames)
{
    for (int frame = frames - fade_frames; frame < frames; frame += 1) {
        for (int channel = 0; channel < CHANNELS; channel += 1) {
            int16_t *sample = &stereo[frame * CHANNELS + channel];
            *sample = (int16_t)(*sample * (frames - frame) / fade_frames);
        }
    }
}

static int generate_effect(const char *dir, const effect_file_t *effect)
{
    int frames = SAMPLE_RATE * effect->ms / 1000;
    music_init(SAMPLE_RATE);
    music_effects_only(1);
    music_effect(effect->sfx);
    music_render(audio, frames);
    for (int index = 0; index < frames * CHANNELS; index += 1) {
        audio[index] = (int16_t)(audio[index] * effect->gain);
    }
    fade_out(audio, frames, SAMPLE_RATE * EFFECT_FADE_MS / 1000);

    return write_file(dir, effect->name, audio, frames * BYTES_PER_FRAME);
}

static int generate_effects(const char *dir)
{
    for (int index = 0; index < SFX_COUNT; index += 1) {
        if (generate_effect(dir, &EFFECTS[index])) {
            return 1;
        }
    }

    return 0;
}

static int generate_background(const char *dir)
{
    static uint32_t sky[SCREEN_W * SCREEN_H];
    aero_background(sky, SCREEN_W, SCREEN_H, 0);

    return write_file(dir, "aero-background.bin", sky, sizeof sky);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <dir>\n", argv[0]);

        return 2;
    }
    const char *dir = argv[1];
    if (generate_music(dir) || generate_effects(dir)) {
        return 1;
    }

    return generate_background(dir);
}
