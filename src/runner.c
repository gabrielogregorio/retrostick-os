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

/* audio: the core hands over the frame's audio here; send_audio() sends it all at the end of the frame */
#define AUDIO_MAX 4096
static int16_t audio[(AUDIO_MAX + 1) * 2]; /* audio[0..1] = last audio frame of the previous batch */
static int naudio;

static void sample(int16_t l, int16_t r)
{
    if (naudio < AUDIO_MAX) { audio[2 + naudio * 2] = l; audio[3 + naudio * 2] = r; naudio++; }
    samples++;
}
static size_t batch(const int16_t *s, size_t n)
{
    size_t k = n < (size_t)(AUDIO_MAX - naudio) ? n : (size_t)(AUDIO_MAX - naudio);
    memcpy(audio + 2 + naudio * 2, s, k * 4);
    naudio += k;
    samples += n;
    return n;
}

/* Dynamic rate control (same as RetroArch): the TV retrace (59.94 or 60 Hz) does not match
 * the core clock (NES 60.1 Hz), so the frame's audio is stretched or shrunk by up to
 * DRC_MAX depending on whether the device queue is below or above target. Linear resampling. */
#define QUEUE_TARGET 2000 /* audio frames (~42 ms at 48 kHz); settles slightly below (proportional control) */
#define DRC_MAX 0.008
static uint32_t phase; /* fractional position (16.16) inside the batch, carried between frames */
static int rate_control, underruns, dropped, queue_sum, queue_count;
static double ratio_sum;

static void send_audio(void)
{
    static int16_t out[(AUDIO_MAX * 11 / 10 + 8) * 2];
    static int first = 1;
    int n = naudio;
    naudio = 0;
    if (!audio_ok || !n) return;
    uint32_t t = plat_ms();
    double ratio = 1.0; /* output frames per input frame */
    if (rate_control) {
        int queue = plat_audio_queue();
        if (queue >= 0) {
            if (first || queue < QUEUE_TARGET / 4) {
                /* start or underrun: fill the queue with silence to avoid crackling */
                static int16_t zero[QUEUE_TARGET * 2];
                if (!first) underruns++;
                plat_audio(zero, QUEUE_TARGET - queue > 0 ? QUEUE_TARGET - queue : 0);
                queue = plat_audio_queue();
                first = 0;
            }
            double err = (double)(QUEUE_TARGET - queue) / QUEUE_TARGET;
            if (err > 1) err = 1;
            if (err < -1) err = -1;
            ratio = 1.0 + DRC_MAX * err;
            queue_sum += queue; queue_count++; ratio_sum += ratio;
        }
    }
    int m = 0;
    if (ratio == 1.0) {
        memcpy(out, audio + 2, n * 4);
        m = n;
    } else {
        /* audio[0..1] is the last frame of the previous batch: interpolates from audio[i] to audio[i+1] */
        uint32_t step = (uint32_t)(65536.0 / ratio), pos = phase;
        while ((int)(pos >> 16) < n) {
            int i = pos >> 16, f = (pos & 0xffff) >> 1; /* 15 bits: difference x fraction fits in an int */
            out[m * 2] = audio[i * 2] + (((audio[i * 2 + 2] - audio[i * 2]) * f) >> 15);
            out[m * 2 + 1] = audio[i * 2 + 1] + (((audio[i * 2 + 3] - audio[i * 2 + 1]) * f) >> 15);
            m++;
            pos += step;
        }
        phase = pos - ((uint32_t)n << 16);
    }
    audio[0] = audio[n * 2]; audio[1] = audio[n * 2 + 1];
    int sent = plat_audio(out, m);
    if (sent < m) dropped += m - sent;
    ms_audio += plat_ms() - t;
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

    /* pacing: TV retrace (stick), blocking audio (PC) or clock (if neither) */
    int pacing = plat_pacing();
    rate_control = pacing != PACE_AUDIO;
    plat_log("game: pacing %d (0 clock, 1 TV retrace, 2 audio)", pacing);
    uint32_t frame_us = (uint32_t)(1000000.0 / av.timing.fps), next = plat_us();
    uint32_t t_quit = 0, t_fps = plat_ms();
    int frames = 0;
    uint32_t ms_run = 0;
    for (;;) {
        uint32_t tr = plat_ms();
        retro_run();
        send_audio();
        ms_run += plat_ms() - tr;
        frames++;
        uint32_t bt = input.bt[0] | input.bt[1], now = plat_ms();
        if (bt & BTN_QUIT) break;
        if ((bt & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) {
            if (!t_quit) t_quit = now;
            else if (now - t_quit > 1000) break;
        } else t_quit = 0;
        /* nothing held the pacing (or the retrace stopped working): wait on the clock.
         * usleep here lasts at least ~10 ms (HZ=100), so the rest is a busy wait. */
        next += frame_us;
        if (plat_pacing() == PACE_CLOCK) {
            int left = (int)(next - plat_us());
            if (left > 12000) plat_sleep(1);
            while ((left = (int)(next - plat_us())) > 0) {}
            if (left < -100000) next = plat_us();
        }
        if (now - t_fps >= 5000) {
            int dur = now - t_fps;
            plat_log("game: fps %d", frames * 1000 / dur);
            /* ms per frame: pure emulation, video (scale + flip) and audio wait */
            plat_log("game: ms/frame emulation (x10) %d", (int)(ms_run - ms_video - ms_audio) * 10 / frames);
            plat_log("game: ms/frame video (x10) %d", (int)ms_video * 10 / frames);
            plat_log("game: ms/frame audio (x10) %d", (int)ms_audio * 10 / frames);
            plat_log("game: audio samples per second %d", (int)(samples * 1000 / dur));
            if (queue_count) {
                plat_log("audio: average queue %d frames", queue_sum / queue_count);
                plat_log("audio: average ratio (x100000) %d", (int)(ratio_sum / queue_count * 100000));
            }
            plat_log("audio: underruns %d", underruns);
            plat_log("audio: dropped frames %d", dropped);
            frames = 0; t_fps = now; ms_run = ms_video = ms_audio = samples = 0;
            queue_sum = queue_count = underruns = dropped = 0; ratio_sum = 0;
        }
    }
    sram(argv[1], 1);
    retro_unload_game();
    retro_deinit();
    plat_shutdown();
    return 0;
}
