/* Menu audio: music and effects prebuilt in files (tools/gen_audio.c, folder retro/audio on the SD card).
 * The stick only reads and mixes: synthesizing live is too slow there and would drain the queue (choppy audio).
 * Without the files (PC preview without `make audio`), it falls back to live synthesis. */
#include "menu_audio.h"
#include <stdint.h>
#include <stdlib.h>
#include "music.h"
#include "plat.h"

#define MENU_RATE 48000   /* same as the games; HDMI accepts it for sure */
#define QUEUE_FRAMES 4000 /* ~83 ms in the device queue (fits in 32 x 1 KB) */
#define BYTES_PER_FRAME 4 /* 16-bit stereo */
#define ACTIVE_EFFECTS 4
#define FREE_VOICE -1
#define ASSET_PATH_SIZE 600

/* time the music takes to go from silent to full; reaching the target within one batch, ~30 ms, would be a hard cut */
#define VOLUME_RAMP_SECONDS 0.4f

/* the device ring is 32 x 1 KB */
#define SILENCE_CHUNK_FRAMES 1024
#define RING_FRAMES 8192
#define SILENCE_TIMEOUT_MS 400

typedef struct {
    int16_t *samples;
    int frames;
} effect_clip_t;

typedef struct {
    int clip, position;
} effect_voice_t;

static int output_open, from_file;
static FILE *music_intro, *music_loop, *music_current;
static effect_clip_t clips[SFX_COUNT];
static effect_voice_t voices[ACTIVE_EFFECTS];
static float music_gain = 0, target_gain = 1;

FILE *menu_audio_open_asset(const char *name)
{
    char path[ASSET_PATH_SIZE];
    snprintf(path, sizeof path, "%s/../audio/%s", plat_roms_dir(), name);
    FILE *file = fopen(path, "rb");
    if (file) {
        return file;
    }
    snprintf(path, sizeof path, "out/audio/%s", name);

    return fopen(path, "rb");
}

static void load_clip(effect_clip_t *clip, const char *name)
{
    FILE *file = menu_audio_open_asset(name);
    if (!file) {
        return;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    clip->samples = malloc(size);
    if (clip->samples && fread(clip->samples, 1, size, file) == (size_t)size) {
        clip->frames = size / BYTES_PER_FRAME;
    }
    fclose(file);
}

static void load_files(void)
{
    /* in SFX_* enum order */
    static const char *const CLIP_FILES[SFX_COUNT] = {"sfx-v.pcm", "sfx-h.pcm", "sfx-a.pcm", "sfx-b.pcm",
                                                      "sfx-f.pcm", "sfx-d.pcm", "sfx-e.pcm"};
    music_intro = menu_audio_open_asset("music-intro.pcm");
    music_loop = menu_audio_open_asset("music-loop.pcm");
    for (int effect = 0; effect < SFX_COUNT; effect += 1) {
        load_clip(&clips[effect], CLIP_FILES[effect]);
    }
    for (int voice = 0; voice < ACTIVE_EFFECTS; voice += 1) {
        voices[voice].position = FREE_VOICE;
    }
    from_file =
        music_intro && music_loop && clips[SFX_MOVE].frames && clips[SFX_SYSTEM].frames && clips[SFX_OPEN].frames;
    music_current = music_intro;
    plat_log("menu: audio from file %d", from_file);
}

void menu_audio_init(void)
{
    music_init(MENU_RATE);
    load_files();
    const char *music = getenv("STICK_MUSIC");
    output_open = !(music && *music == '0') && !plat_audio_init(MENU_RATE);
}

static int free_voice(void)
{
    for (int voice = 0; voice < ACTIVE_EFFECTS; voice += 1) {
        if (voices[voice].position < 0) {
            return voice;
        }
    }

    return 0;
}

void menu_audio_play(int effect)
{
    if (!from_file) {
        music_effect(effect);

        return;
    }
    if (!clips[effect].frames) {
        return; /* SD card without a file for this effect */
    }
    int voice = free_voice();
    voices[voice].clip = effect;
    voices[voice].position = 0;
}

void menu_audio_set_volume(float volume)
{
    if (from_file) {
        target_gain = volume;

        return;
    }
    music_volume(volume);
}

static inline float limit_step(float step, float max_step)
{
    if (step > max_step) {
        return max_step;
    }
    if (step < -max_step) {
        return -max_step;
    }

    return step;
}

static inline int16_t saturate(int sample)
{
    if (sample > INT16_MAX) {
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        return INT16_MIN;
    }

    return sample;
}

/* The intro once, then the loop forever. */
static void read_music(int16_t *buffer, int frames)
{
    int frames_read = 0;
    while (frames_read < frames) {
        size_t chunk = fread(buffer + frames_read * 2, BYTES_PER_FRAME, frames - frames_read, music_current);
        frames_read += chunk;
        if (frames_read < frames) {
            music_current = music_loop;
            fseek(music_loop, 0, SEEK_SET);
        }
    }
}

/* Applies the volume ramp and sums the effects with saturation. */
static void mix_frames(int16_t *buffer, int frames)
{
    const float max_step = 1.0f / (MENU_RATE * VOLUME_RAMP_SECONDS);
    for (int frame = 0; frame < frames; frame += 1) {
        music_gain += limit_step(target_gain - music_gain, max_step);
        int left = (int)(buffer[frame * 2] * music_gain), right = (int)(buffer[frame * 2 + 1] * music_gain);
        for (int voice = 0; voice < ACTIVE_EFFECTS; voice += 1) {
            if (voices[voice].position < 0) {
                continue;
            }
            const effect_clip_t *clip = &clips[voices[voice].clip];
            left += clip->samples[voices[voice].position * 2];
            right += clip->samples[voices[voice].position * 2 + 1];
            voices[voice].position += 1;
            if (voices[voice].position >= clip->frames) {
                voices[voice].position = FREE_VOICE;
            }
        }
        buffer[frame * 2] = saturate(left);
        buffer[frame * 2 + 1] = saturate(right);
    }
}

void menu_audio_feed(void)
{
    if (!output_open) {
        return;
    }
    static int16_t buffer[QUEUE_FRAMES * 2];
    int queued = plat_audio_queue();
    if (queued < 0) {
        queued = 0;
    }
    int needed = QUEUE_FRAMES - queued;
    if (needed <= 0) {
        return;
    }
    if (!from_file) {
        music_render(buffer, needed);
        plat_audio(buffer, needed);

        return;
    }
    read_music(buffer, needed);
    mix_frames(buffer, needed);
    plat_audio(buffer, needed);
}

/* Before closing, the device's whole ring gets silence: if the queue ran dry, DMA could repeat old leftovers
 * (the pop when opening certain games). Closes with the queue still full of silence. */
void menu_audio_release(void)
{
    if (!output_open) {
        return;
    }
    static int16_t silence[SILENCE_CHUNK_FRAMES * 2];
    int frames_sent = 0;
    uint32_t started_at = plat_ms();
    while (frames_sent < RING_FRAMES + SILENCE_CHUNK_FRAMES && plat_ms() - started_at < SILENCE_TIMEOUT_MS) {
        int accepted = plat_audio(silence, SILENCE_CHUNK_FRAMES);
        if (accepted > 0) {
            frames_sent += accepted;
        }
    }
    plat_audio_close();
}

void menu_audio_reopen(void)
{
    if (output_open) {
        output_open = !plat_audio_init(MENU_RATE);
    }
}
