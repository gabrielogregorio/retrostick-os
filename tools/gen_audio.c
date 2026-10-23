/* Generates on the PC the files the stick menu only reads (nothing is synthesized or computed there):
 *   music-intro.pcm  first pass of the music (instruments come in one by one)
 *   music-loop.pcm   third pass, joins onto itself seamlessly (the echo coming from the 2nd pass is identical)
 *   sfx-h.pcm      switch system (horizontal)        sfx-v.pcm  move in the list (vertical)
 *   sfx-a.pcm      open / click                      sfx-b.pcm  back
 *   aero-background.bin    sky of the Aero theme, 640x360 0x00RRGGBB (too slow to compute on the stick)
 * Audio: 32 kHz, stereo, 16-bit little-endian, no header.
 * Usage: out/gen-audio <output dir> */
#include <stdio.h>
#include <string.h>
#include "../src/aero.h"
#include "../src/music.h"
#include "../src/plat.h"

#define SAMPLE_RATE 32000
#define BPM 88

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

static int16_t buf[SAMPLE_RATE * 50 * 2];

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <dir>\n", argv[0]); return 2; }
    const char *dir = argv[1];
    /* music: three 16-bar passes; records the 1st and the 3rd */
    music_init(SAMPLE_RATE);
    double wrap = 256.0 * SAMPLE_RATE * 60.0 / BPM / 4; /* audio frames per pass (256 16th notes) */
    long done = 0;
    for (int v = 0; v < 3; v++) {
        long end = (long)((v + 1) * wrap + 0.5), n = end - done;
        music_render(buf, (int)n);
        done = end;
        if (v == 0 && write_file(dir, "music-intro.pcm", buf, n * 4)) return 1;
        if (v == 2 && write_file(dir, "music-loop.pcm", buf, n * 4)) return 1;
    }
    /* effects: one at a time, no music, fixed length and a soft 120 ms fade-out (no click at the cut) */
    static const struct { int sfx; const char *name; int ms; } EFFECTS[] = {
        {SFX_SYSTEM, "sfx-h.pcm", 900}, {SFX_MOVE, "sfx-v.pcm", 350}, {SFX_OPEN, "sfx-a.pcm", 1200},
        {SFX_BACK, "sfx-b.pcm", 600}};
    for (int i = 0; i < 4; i++) {
        music_init(SAMPLE_RATE);
        music_effects_only(1);
        music_effect(EFFECTS[i].sfx);
        int n = SAMPLE_RATE * EFFECTS[i].ms / 1000, fade = SAMPLE_RATE * 120 / 1000;
        music_render(buf, n);
        for (int k = n - fade; k < n; k++)
            for (int c = 0; c < 2; c++) buf[k * 2 + c] = (int16_t)(buf[k * 2 + c] * (n - k) / fade);
        if (write_file(dir, EFFECTS[i].name, buf, n * 4)) return 1;
    }
    /* background */
    static uint32_t sky[SCREEN_W * SCREEN_H];
    aero_background(sky, SCREEN_W, SCREEN_H, 0);
    return write_file(dir, "aero-background.bin", sky, sizeof sky);
}
