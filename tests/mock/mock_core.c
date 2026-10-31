/* Fake emulator core: no real system behind it. It draws a moving bar, plays a quiet tone, counts
 * frames and keeps 16 bytes of save RAM, enough to exercise the runner end to end.
 * Set MOCK_CORE_TRACE=file to append "reset" and "loaded <frame>" lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

#define FRAME_W 256
#define FRAME_H 240
#define BAR_W 32
#define BAR_COLOR 0xffe0
#define BACKGROUND_BLUE 0x001f
#define SAVE_RAM_SIZE 16
#define AUDIO_FRAMES 800 /* one video frame at 48 kHz and 60 fps */
#define TONE_PERIOD 100
#define TONE_LEVEL 500
#define STEREO 2

static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t input_poll;
static retro_environment_t env;
static uint16_t pixels[FRAME_W * FRAME_H];
static uint8_t save_ram[SAVE_RAM_SIZE];
static uint32_t frame;

static void trace(const char *event, uint32_t value)
{
    const char *path = getenv("MOCK_CORE_TRACE");
    if (!path) {
        return;
    }
    FILE *file = fopen(path, "a");
    if (file) {
        fprintf(file, "%s %u\n", event, value);
        fclose(file);
    }
}

void retro_set_environment(retro_environment_t cb)
{
    env = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    (void)cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    (void)cb;
}

void retro_init(void)
{
}

void retro_deinit(void)
{
}

void retro_unload_game(void)
{
}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof *info);
    info->library_name = "mock";
    info->library_version = "1";
    info->valid_extensions = "nes";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof *info);
    info->geometry.base_width = FRAME_W;
    info->geometry.base_height = FRAME_H;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 48000.0;
}

bool retro_load_game(const struct retro_game_info *game)
{
    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_RGB565;
    (void)game;

    return env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
}

static void draw_bar(void)
{
    for (int y = 0; y < FRAME_H; y += 1) {
        for (int x = 0; x < FRAME_W; x += 1) {
            pixels[y * FRAME_W + x] = (x + (int)frame) % FRAME_W < BAR_W ? BAR_COLOR : (uint16_t)(BACKGROUND_BLUE + y);
        }
    }
}

static void fill_tone(int16_t *tone)
{
    for (int audio_frame = 0; audio_frame < AUDIO_FRAMES; audio_frame += 1) {
        int16_t level = audio_frame % TONE_PERIOD < TONE_PERIOD / 2 ? TONE_LEVEL : -TONE_LEVEL;
        tone[audio_frame * STEREO] = level;
        tone[audio_frame * STEREO + 1] = level;
    }
}

void retro_run(void)
{
    static int16_t tone[AUDIO_FRAMES * STEREO];
    input_poll();
    frame += 1;
    save_ram[0] = (uint8_t)frame;
    draw_bar();
    fill_tone(tone);
    video(pixels, FRAME_W, FRAME_H, FRAME_W * sizeof pixels[0]);
    audio(tone, AUDIO_FRAMES);
}

void retro_reset(void)
{
    frame = 0;
    trace("reset", 0);
}

size_t retro_serialize_size(void)
{
    return sizeof frame;
}

bool retro_serialize(void *data, size_t size)
{
    if (size < sizeof frame) {
        return false;
    }
    memcpy(data, &frame, sizeof frame);

    return true;
}

bool retro_unserialize(const void *data, size_t size)
{
    if (size < sizeof frame) {
        return false;
    }
    memcpy(&frame, data, sizeof frame);
    trace("loaded", frame);

    return true;
}

void *retro_get_memory_data(unsigned id)
{
    return id == RETRO_MEMORY_SAVE_RAM ? save_ram : NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    return id == RETRO_MEMORY_SAVE_RAM ? sizeof save_ram : 0;
}
