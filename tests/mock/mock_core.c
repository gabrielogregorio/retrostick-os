/* Fake emulator core: no real system behind it. It draws a moving bar, plays a quiet tone, counts
 * frames and keeps 16 bytes of save RAM, enough to exercise src/runner.c end to end.
 * Set MOCK_CORE_TRACE=file to append "reset" and "loaded <frame>" lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

#define W 256
#define H 240

static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t input_poll;
static retro_environment_t env;
static uint16_t pixels[W * H];
static uint8_t save_ram[16];
static uint32_t frame;

static void trace(const char *what, uint32_t value)
{
    const char *path = getenv("MOCK_CORE_TRACE");
    if (!path) {
        return;
    }
    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s %u\n", what, value);
        fclose(f);
    }
}

void retro_set_environment(retro_environment_t cb) { env = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll = cb; }
void retro_set_input_state(retro_input_state_t cb) { (void)cb; }
void retro_init(void) {}
void retro_deinit(void) {}
void retro_unload_game(void) {}

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
    info->geometry.base_width = W;
    info->geometry.base_height = H;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 48000.0;
}

bool retro_load_game(const struct retro_game_info *game)
{
    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_RGB565;
    (void)game;

    return env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
}

void retro_run(void)
{
    static int16_t tone[800 * 2];
    input_poll();
    frame += 1;
    save_ram[0] = (uint8_t)frame;
    for (int y = 0; y < H; y += 1) {
        for (int x = 0; x < W; x += 1) {
            pixels[y * W + x] = (x + (int)frame) % W < 32 ? 0xffe0 : (uint16_t)(0x001f + y);
        }
    }
    for (int i = 0; i < 800; i += 1) {
        tone[i * 2] = tone[i * 2 + 1] = i % 100 < 50 ? 500 : -500;
    }
    video(pixels, W, H, W * 2);
    audio(tone, 800);
}

void retro_reset(void)
{
    frame = 0;
    trace("reset", 0);
}

size_t retro_serialize_size(void) { return sizeof frame; }

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

void *retro_get_memory_data(unsigned id) { return id == RETRO_MEMORY_SAVE_RAM ? save_ram : NULL; }
size_t retro_get_memory_size(unsigned id) { return id == RETRO_MEMORY_SAVE_RAM ? sizeof save_ram : 0; }
