/* Game runner: a statically linked libretro core + the same platform as the menu.
 * Usage: nes-<arch> <game file> [saves folder]
 * Quit the game: hold SELECT + START for 1 s (Esc too on the PC). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"
#include "plat.h"

static input_t input;
static int pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static const char *saves_dir = ".";
static int audio_ok;

static void logcb(enum retro_log_level level, const char *fmt, ...)
{
    if (level < RETRO_LOG_WARN) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static bool environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixel_format = *(enum retro_pixel_format *)data;
        return pixel_format == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = saves_dir; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = logcb; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        /* core options tuned for the stick's ARM11 */
        struct retro_variable *v = data;
        static const char *OPTIONS[][2] = {
            {"fceumm_sndrate_hint", "32KHz"}, /* fewer samples to synthesize and send to audio */
            {"fceumm_sndquality", "Low"},
        };
        for (unsigned i = 0; i < sizeof OPTIONS / sizeof OPTIONS[0]; i++)
            if (!strcmp(v->key, OPTIONS[i][0])) { v->value = OPTIONS[i][1]; return true; }
        v->value = NULL;
        return false;
    }
    }
    return false;
}

/* instrumentation: where each frame's time goes */
static uint32_t ms_video, ms_audio, samples;

static void video(const void *px, unsigned w, unsigned h, size_t pitch)
{
    uint32_t t = plat_ms();
    if (px) plat_present_565(px, w, h, pitch / 2);
    ms_video += plat_ms() - t;
}

static void sample(int16_t l, int16_t r) { int16_t s[2] = {l, r}; if (audio_ok) plat_audio(s, 1); samples++; }
static size_t batch(const int16_t *s, size_t n)
{
    uint32_t t = plat_ms();
    if (audio_ok) plat_audio(s, n);
    ms_audio += plat_ms() - t;
    samples += n;
    return n;
}
/* with ONE controller on, it is player 1, whichever it is; with both, each one is its own */
static void poll_input(void)
{
    plat_input(&input);
    if (!input.present[0] && input.present[1]) {
        input.bt[0] |= input.bt[1];
        input.bt[1] = 0;
    }
}

static int16_t input_state(unsigned port, unsigned device, unsigned idx, unsigned id)
{
    if (port > 1 || device != RETRO_DEVICE_JOYPAD) return 0;
    static const uint32_t M[16] = {
        [RETRO_DEVICE_ID_JOYPAD_B] = BTN_B, [RETRO_DEVICE_ID_JOYPAD_Y] = BTN_Y,
        [RETRO_DEVICE_ID_JOYPAD_SELECT] = BTN_SELECT, [RETRO_DEVICE_ID_JOYPAD_START] = BTN_START,
        [RETRO_DEVICE_ID_JOYPAD_UP] = BTN_UP, [RETRO_DEVICE_ID_JOYPAD_DOWN] = BTN_DOWN,
        [RETRO_DEVICE_ID_JOYPAD_LEFT] = BTN_LEFT, [RETRO_DEVICE_ID_JOYPAD_RIGHT] = BTN_RIGHT,
        [RETRO_DEVICE_ID_JOYPAD_A] = BTN_A, [RETRO_DEVICE_ID_JOYPAD_X] = BTN_X,
        [RETRO_DEVICE_ID_JOYPAD_L] = BTN_L, [RETRO_DEVICE_ID_JOYPAD_R] = BTN_R,
    };
    return id < 16 && (input.bt[port] & M[id]) ? 1 : 0;
}

static void save_path(char *out, int max, const char *rom)
{
    const char *b = strrchr(rom, '/');
    b = b ? b + 1 : rom;
    snprintf(out, max, "%s/%s", saves_dir, b);
    char *p = strrchr(out, '.');
    if (p && p > out + strlen(saves_dir)) strcpy(p, ".srm");
    else strncat(out, ".srm", max - strlen(out) - 1);
}

static void sram(const char *rom, int writing)
{
    size_t n = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void *m = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!n || !m) return;
    char path[600];
    save_path(path, sizeof path, rom);
    FILE *f = fopen(path, writing ? "wb" : "rb");
    if (!f) return;
    if (writing) fwrite(m, 1, n, f);
    else if (fread(m, 1, n, f) != n) fprintf(stderr, "incomplete save: %s\n", path);
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s game [saves folder]\n", argv[0]); return 2; }
    if (argc > 2) saves_dir = argv[2];
    if (plat_init()) return 1;

    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample(sample);
    retro_set_audio_sample_batch(batch);
    retro_set_input_poll(poll_input);
    retro_set_input_state(input_state);
    retro_init();

    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "could not open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc(size);
    if (fread(data, 1, size, f) != (size_t)size) { fclose(f); return 1; }
    fclose(f);
    struct retro_game_info game = {argv[1], data, (size_t)size, NULL};
    if (!retro_load_game(&game)) { fprintf(stderr, "core refused the game\n"); return 1; }
    sram(argv[1], 0);

    struct retro_system_av_info av;
    retro_get_system_av_info(&av);
    audio_ok = !plat_audio_init((int)av.timing.sample_rate);
    plat_log("game: audio %d", audio_ok);
    plat_log("game: sample rate %d", (int)av.timing.sample_rate);

    double frame_ms = 1000.0 / av.timing.fps, next = plat_ms();
    uint32_t t_quit = 0, t_fps = plat_ms();
    int frames = 0;
    uint32_t ms_run = 0;
    for (;;) {
        uint32_t tr = plat_ms();
        retro_run();
        ms_run += plat_ms() - tr;
        frames++;
        uint32_t bt = input.bt[0] | input.bt[1], now = plat_ms();
        if (bt & BTN_QUIT) break;
        if ((bt & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) {
            if (!t_quit) t_quit = now;
            else if (now - t_quit > 1000) break;
        } else t_quit = 0;
        /* without blocking audio, hold the pacing on the clock */
        next += frame_ms;
        if (!audio_ok || plat_audio_headroom_ms() < 0) {
            int left = (int)(next - plat_ms());
            if (left > 0) plat_sleep(left);
            else if (left < -100) next = plat_ms();
        }
        if (now - t_fps >= 5000) {
            int dur = now - t_fps;
            plat_log("game: fps %d", frames * 1000 / dur);
            /* ms per frame: pure emulation, video (scale + flip) and audio wait */
            plat_log("game: ms/frame emulation (x10) %d", (int)(ms_run - ms_video - ms_audio) * 10 / frames);
            plat_log("game: ms/frame video (x10) %d", (int)ms_video * 10 / frames);
            plat_log("game: ms/frame audio (x10) %d", (int)ms_audio * 10 / frames);
            plat_log("game: audio samples per second %d", (int)(samples * 1000 / dur));
            frames = 0; t_fps = now; ms_run = ms_video = ms_audio = samples = 0;
        }
    }
    sram(argv[1], 1);
    retro_unload_game();
    retro_deinit();
    plat_shutdown();
    return 0;
}
