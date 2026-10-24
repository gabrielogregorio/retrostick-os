/* Generates on the PC the files the stick menu only reads (nothing is synthesized or computed there):
 *   music-intro.pcm  intro of the 8-bit music (bass and drums)
 *   music-loop.pcm   32 bars that join onto themselves
 *   sfx-h.pcm      switch system (horizontal)        sfx-v.pcm  move in the list (vertical)
 *   sfx-a.pcm      open / click                      sfx-b.pcm  back
 *   sfx-f.pcm      favorite                          sfx-d.pcm  unfavorite
 *   aero-background.bin    sky of the Aero theme, 640x360 0x00RRGGBB (too slow to compute on the stick)
 * Audio: 48 kHz, stereo, 16-bit little-endian, no header.
 * Usage: out/gen-audio <output dir> */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/aero.h"
#include "../src/chip.h"
#include "../src/music.h"
#include "../src/plat.h"

#define SAMPLE_RATE 48000

static int write_file(const char *dir, const char *name, const void *d, size_t n)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(d, 1, n, f) != n) { perror(path); return 1; }
    fclose(f);
    printf("%-18s %8zu bytes\n", name, n);
    return 0;
}

static int16_t buf[SAMPLE_RATE * 60 * 2]; /* the 8-bit loop lasts 51.2 s */

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <dir>\n", argv[0]); return 2; }
    const char *dir = argv[1];
    /* 8-bit music (src/chip.c): intro + 32-bar loop, at a loop RMS of ~1680 in 16 bits to sit level with
     * the core audio. Rule: never above the loudest console. */
    chip_init(SAMPLE_RATE);
    long ni = chip_intro_frames(), nl = chip_loop_frames();
    static int16_t intro[SAMPLE_RATE * 10 * 2];
    chip_intro(intro);
    chip_loop(buf);
    double sum = 0;
    for (long k = 0; k < nl * 2; k++) sum += (double)buf[k] * buf[k];
    double gain = 1680.0 / sqrt(sum / (nl * 2));
    for (long k = 0; k < nl * 2; k++) buf[k] = (int16_t)(buf[k] * gain);
    for (long k = 0; k < ni * 2; k++) intro[k] = (int16_t)(intro[k] * gain);
    printf("8-bit music: gain %.3f\n", gain);
    if (write_file(dir, "music-intro.pcm", intro, ni * 4) || write_file(dir, "music-loop.pcm", buf, nl * 4)) return 1;
    /* effects: one at a time, no music, fixed length and a soft 120 ms fade-out (no click at the cut) */
    /* gain: per-effect levels, balanced against the game audio.
     * Effects have no echo (src/music.c): a bell echo would come back 500 ms later like a second hit. */
    /* final gain on everything; click, back and unfavorite are low square waves: high bells would be annoying */
    static const struct { int sfx; const char *name; int ms; float gain; } EFFECTS[] = {
        {SFX_SYSTEM, "sfx-h.pcm", 400, 0.63f}, {SFX_MOVE, "sfx-v.pcm", 350, 0.88f},
        {SFX_OPEN, "sfx-a.pcm", 250, 0.63f}, {SFX_BACK, "sfx-b.pcm", 250, 0.63f},
        {SFX_FAVORITE, "sfx-f.pcm", 450, 0.63f}, {SFX_UNFAVORITE, "sfx-d.pcm", 400, 0.63f}};
    for (int i = 0; i < SFX_COUNT; i++) {
        music_init(SAMPLE_RATE);
        music_effects_only(1);
        music_effect(EFFECTS[i].sfx);
        int n = SAMPLE_RATE * EFFECTS[i].ms / 1000, fade = SAMPLE_RATE * 120 / 1000;
        music_render(buf, n);
        for (int k = 0; k < n * 2; k++) buf[k] = (int16_t)(buf[k] * EFFECTS[i].gain);
        for (int k = n - fade; k < n; k++)
            for (int c = 0; c < 2; c++) buf[k * 2 + c] = (int16_t)(buf[k * 2 + c] * (n - k) / fade);
        if (write_file(dir, EFFECTS[i].name, buf, n * 4)) return 1;
    }
    /* background */
    static uint32_t sky[SCREEN_W * SCREEN_H];
    aero_background(sky, SCREEN_W, SCREEN_H, 0);
    return write_file(dir, "aero-background.bin", sky, sizeof sky);
}
