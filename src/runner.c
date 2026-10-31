/* Game runner: a statically linked libretro core + the same platform as the menu.
 * Usage: nes-<arch> <game file> [saves folder]
 * Quit the game: hold SELECT + START for 1 s (Esc too on the PC), or MODE > Quit game.
 * MODE opens the pause menu. This file holds the libretro environment, video, input and the main loop. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "runner_internal.h"

#define PLAYERS 2
#define NO_PLAYER (-1)
#define JOYPAD_IDS 16
#define BYTES_PER_PIXEL 2
#define LAST_FRAME_MAX (512 * 480)
#define MIN_RETRACE_FPS 58 /* outside this range a core cannot follow the TV (WonderSwan 75.47 Hz runs slow) */
#define MAX_RETRACE_FPS 62
#define TV_FRAME_US 16684 /* 59.94 Hz */
#define MAX_LATE_FRAMES 3
#define QUIT_HOLD_MS 1000
#define STATS_PERIOD_MS 5000
#define BUSY_WAIT_MAX_US 12000 /* usleep lasts at least ~10 ms (HZ=100), the rest is a busy wait */
#define CLOCK_RESYNC_US (-100000)

/* Core options tuned for the stick's ARM11. */
static const struct {
    const char *key;
    const char *value;
} CORE_OPTIONS[] = {
    {"fceumm_sndrate_hint", "32KHz"}, /* fewer samples to synthesize and send to audio */
    {"fceumm_sndquality", "Low"},
    /* automatic frame skip: the core stops drawing when the audio queue runs low */
    {"snes9x2002_frameskip", "auto"},
    {"gpsp_frameskip", "auto"},
    {"picodrive_frameskip", "auto"},
};

static const uint32_t JOYPAD_BUTTONS[JOYPAD_IDS] = {
    [RETRO_DEVICE_ID_JOYPAD_B] = BTN_B,           [RETRO_DEVICE_ID_JOYPAD_Y] = BTN_Y,
    [RETRO_DEVICE_ID_JOYPAD_SELECT] = BTN_SELECT, [RETRO_DEVICE_ID_JOYPAD_START] = BTN_START,
    [RETRO_DEVICE_ID_JOYPAD_UP] = BTN_UP,         [RETRO_DEVICE_ID_JOYPAD_DOWN] = BTN_DOWN,
    [RETRO_DEVICE_ID_JOYPAD_LEFT] = BTN_LEFT,     [RETRO_DEVICE_ID_JOYPAD_RIGHT] = BTN_RIGHT,
    [RETRO_DEVICE_ID_JOYPAD_A] = BTN_A,           [RETRO_DEVICE_ID_JOYPAD_X] = BTN_X,
    [RETRO_DEVICE_ID_JOYPAD_L] = BTN_L,           [RETRO_DEVICE_ID_JOYPAD_R] = BTN_R,
};

static input_t input;
static int drew; /* the core sent an image this frame; when skipping it may not even call video */
static int consecutive_skips;

/* Copy of the last drawn frame for the pause menu: the core's buffer may change later. */
static uint16_t last_frame[LAST_FRAME_MAX];
static int last_frame_w, last_frame_h;

static struct {
    uint32_t frame_us;
    uint32_t next_frame_us; /* used when pacing by the clock */
    uint32_t reference_us;  /* when this frame should start; 0 restarts the reference */
    uint32_t reference_period_us;
    uint32_t quit_held_since_ms;
} timing;

/* where each frame's time goes */
static struct {
    uint32_t since_ms;
    uint32_t run_ms;
    uint32_t video_ms;
    uint32_t skipped_frames;
    int frames;
} stats;

static void log_core_message(enum retro_log_level level, const char *format, ...)
{
    if (level < RETRO_LOG_WARN) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
}

static bool get_core_option(struct retro_variable *variable)
{
    for (unsigned option_index = 0; option_index < sizeof CORE_OPTIONS / sizeof CORE_OPTIONS[0]; option_index += 1) {
        if (!strcmp(variable->key, CORE_OPTIONS[option_index].key)) {
            variable->value = CORE_OPTIONS[option_index].value;

            return true;
        }
    }
    variable->value = NULL;

    return false;
}

static bool environment(unsigned command, void *data)
{
    switch (command) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = runner_saves_dir(); return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = log_core_message; return true;
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK: {
        const struct retro_audio_buffer_status_callback *status = data;
        runner_audio_set_status_callback(status ? status->callback : NULL);

        return true;
    }
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY: return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: return get_core_option(data);
    }

    return false;
}

static inline void keep_last_frame(const void *pixels, unsigned w, unsigned h, size_t pitch)
{
    if (w * h > LAST_FRAME_MAX) {
        return;
    }
    for (unsigned y = 0; y < h; y += 1) {
        memcpy(last_frame + y * w, (const uint8_t *)pixels + y * pitch, w * BYTES_PER_PIXEL);
    }
    last_frame_w = w;
    last_frame_h = h;
}

/* A skipped frame (null pixels or no call) neither draws nor waits for the TV. */
static void video(const void *pixels, unsigned w, unsigned h, size_t pitch)
{
    uint32_t started_ms = plat_ms();
    if (pixels) {
        drew = 1;
        plat_present_565(pixels, w, h, pitch / BYTES_PER_PIXEL);
        keep_last_frame(pixels, w, h, pitch);
    }
    stats.video_ms += plat_ms() - started_ms;
}

const uint16_t *runner_last_frame(int *width, int *height)
{
    *width = last_frame_w;
    *height = last_frame_h;

    return last_frame_w ? last_frame : NULL;
}

static int first_player_pressing(void)
{
    for (int player = 0; player < PLAYERS; player += 1) {
        if (input.bt[player]) {
            return player;
        }
    }

    return NO_PLAYER;
}

/* Player 1 = the first controller to press any button in game; the other is player 2.
 * This holds until the game quits: turning the other controller on or off midway swaps nobody,
 * so the controller in use can never become 2 and seem dead. */
static void poll_input(void)
{
    static int primary = NO_PLAYER;
    plat_input(&input);
    if (primary == NO_PLAYER) {
        primary = first_player_pressing();
    }
    if (primary == 1) {
        uint32_t first_controller = input.bt[0];
        input.bt[0] = input.bt[1];
        input.bt[1] = first_controller;
    }
}

static uint32_t held_buttons(void)
{
    return input.bt[0] | input.bt[1];
}

uint32_t runner_poll_buttons(void)
{
    poll_input();

    return held_buttons();
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port >= PLAYERS || device != RETRO_DEVICE_JOYPAD) {
        return 0;
    }

    return id < JOYPAD_IDS && (input.bt[port] & JOYPAD_BUTTONS[id]) ? 1 : 0;
}

static void reset_stats(uint32_t now)
{
    stats.since_ms = now;
    stats.run_ms = 0;
    stats.video_ms = 0;
    stats.skipped_frames = 0;
    stats.frames = 0;
}

static void log_stats(uint32_t now)
{
    int duration_ms = now - stats.since_ms;
    int frames = stats.frames;
    uint32_t audio_ms = runner_audio_ms();
    plat_log("game: fps %d", frames * 1000 / duration_ms);
    /* ms per frame: pure emulation, video (scale + flip) and audio wait */
    plat_log("game: ms/frame emulation (x10) %d", (int)(stats.run_ms - stats.video_ms - audio_ms) * 10 / frames);
    plat_log("game: ms/frame video (x10) %d", (int)stats.video_ms * 10 / frames);
    plat_log("game: ms/frame audio (x10) %d", (int)audio_ms * 10 / frames);
    plat_log("game: audio samples per second %d", (int)(runner_audio_frames_received() * 1000 / duration_ms));
    runner_audio_log_queue_stats();
    plat_log("game: skipped frames %d", (int)stats.skipped_frames);
    runner_audio_log_level_stats_and_reset();
    reset_stats(now);
}

static void install_callbacks(void)
{
    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample(runner_audio_sample);
    retro_set_audio_sample_batch(runner_audio_batch);
    retro_set_input_poll(poll_input);
    retro_set_input_state(input_state);
}

static bool load_game(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "could not open %s\n", path);

        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    struct retro_system_info system_info;
    retro_get_system_info(&system_info);
    void *data = NULL;
    if (system_info.need_fullpath) {
        size = 0; /* the core reads by path (gpsp pages the ROM): the whole file would not fit the stick's 55 MB */
    } else {
        data = malloc(size);
        if (!data || fread(data, 1, size, file) != (size_t)size) {
            fclose(file);

            return false;
        }
    }
    fclose(file);
    plat_log("game: rom loaded in memory (bytes) %d", (int)size);
    static struct retro_game_info game; /* outlives this call: a core may keep the pointer */
    game = (struct retro_game_info){path, data, (size_t)size, NULL};
    if (!retro_load_game(&game)) {
        fprintf(stderr, "core refused the game\n");

        return false;
    }

    return true;
}

/* Pacing: TV retrace (stick), blocking audio (PC) or clock (if neither). */
static void start_audio_and_pacing(void)
{
    struct retro_system_av_info av;
    retro_get_system_av_info(&av);
    runner_audio_open(av.timing.sample_rate);
    if (av.timing.fps < MIN_RETRACE_FPS || av.timing.fps > MAX_RETRACE_FPS) {
        plat_disable_retrace();
    }
    int pacing = plat_pacing();
    plat_log("game: core frames per second (x100) %d", (int)(av.timing.fps * 100));
    runner_audio_set_rate_control(pacing != PACE_AUDIO);
    plat_log("game: pacing %d (0 clock, 1 TV retrace, 2 audio)", pacing);
    timing.frame_us = (uint32_t)(1000000.0 / av.timing.fps);
    timing.next_frame_us = plat_us();
    /* reference period for lateness: on retrace, never faster than the TV */
    timing.reference_period_us = pacing == PACE_VIDEO && timing.frame_us < TV_FRAME_US ? TV_FRAME_US : timing.frame_us;
    reset_stats(plat_ms());
}

/* Being late against the reference clock makes the core skip drawing. The audio queue alone is not a
 * usable signal: an underrun is plugged with silence, so it never fires. */
static void update_frame_skip_hint(void)
{
    uint32_t now_us = plat_us();
    if (!timing.reference_us) {
        timing.reference_us = now_us;
    }
    int delay_us = (int)(now_us - timing.reference_us);
    if (delay_us > (int)timing.reference_period_us * MAX_LATE_FRAMES) {
        timing.reference_us = now_us;
        delay_us = 0;
    }
    runner_audio_report_status(delay_us, consecutive_skips);
    timing.reference_us += timing.reference_period_us;
}

static void run_frame(void)
{
    uint32_t started_ms = plat_ms();
    update_frame_skip_hint();
    drew = 0;
    retro_run();
    consecutive_skips = drew ? 0 : consecutive_skips + 1;
    stats.skipped_frames += !drew;
    runner_audio_send();
    stats.run_ms += plat_ms() - started_ms;
    stats.frames += 1;
}

/* The game stopped: the reference clock restarts with no burst of skips, and the stopped time does
 * not count toward the logged fps. */
static void resume_after_pause(void)
{
    timing.reference_us = 0;
    timing.next_frame_us = plat_us();
    timing.quit_held_since_ms = 0;
    reset_stats(plat_ms());
    runner_audio_reset_frame_stats();
}

static bool quit_combo_held_long_enough(uint32_t buttons, uint32_t now)
{
    if ((buttons & (BTN_SELECT | BTN_START)) != (BTN_SELECT | BTN_START)) {
        timing.quit_held_since_ms = 0;

        return false;
    }
    if (!timing.quit_held_since_ms) {
        timing.quit_held_since_ms = now;

        return false;
    }

    return now - timing.quit_held_since_ms > QUIT_HOLD_MS;
}

/* Nothing held the pacing, or the retrace stopped working. */
static void wait_on_clock(void)
{
    timing.next_frame_us += timing.frame_us;
    if (plat_pacing() != PACE_CLOCK) {
        return;
    }
    int left_us = (int)(timing.next_frame_us - plat_us());
    if (left_us > BUSY_WAIT_MAX_US) {
        plat_sleep(1);
    }
    do {
        left_us = (int)(timing.next_frame_us - plat_us());
    } while (left_us > 0);
    if (left_us < CLOCK_RESYNC_US) {
        timing.next_frame_us = plat_us();
    }
}

static void run_until_quit(void)
{
    uint32_t previous_buttons = 0;
    for (;;) {
        run_frame();
        uint32_t buttons = held_buttons();
        uint32_t now = plat_ms();
        if (buttons & BTN_QUIT) {
            return;
        }
        if (buttons & BTN_MODE & ~previous_buttons) {
            if (runner_pause_menu()) {
                return;
            }
            resume_after_pause();
            previous_buttons = BTN_MODE;
            continue;
        }
        previous_buttons = buttons;
        if (quit_combo_held_long_enough(buttons, now)) {
            return;
        }
        wait_on_clock();
        if (now - stats.since_ms >= STATS_PERIOD_MS) {
            log_stats(now);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s game [saves folder]\n", argv[0]);

        return 2;
    }
    runner_saves_init(argv[1], argc > 2 ? argv[2] : ".");
    if (plat_init()) {
        return 1;
    }
    install_callbacks();
    retro_init();
    if (!load_game(argv[1])) {
        return 1;
    }
    runner_sram_load();
    start_audio_and_pacing();
    run_until_quit();
    runner_sram_save();
    runner_save_state(SLOT_AUTO);
    retro_unload_game();
    retro_deinit();
    plat_shutdown();

    return 0;
}
