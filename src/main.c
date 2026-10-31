/* Stick menu: startup, the input loop and the game launch sequence. Same base on PC and on the stick. */
#include <stdio.h>
#include "catalog.h"
#include "details.h"
#include "menu_audio.h"
#include "menu_view.h"
#include "music.h"
#include "plat.h"

#define NOTICE_SIZE 160
#define NAME_SIZE 128
#define PATH_SIZE 600

#define FRAME_MS 16.0f
#define MAX_FRAME_STEPS 6.0f
#define SMOOTHING_PER_FRAME 0.22f

#define START_QUIT_MS 3000
#define RETURN_START_GRACE_MS 2000
#define REPEAT_DELAY_MS 450
#define REPEAT_INTERVAL_MS 150
#define LAUNCH_DELAY_MS 800
#define NOTICE_HOLD_MS 1400
#define NOTICE_FULL_ALPHA 255
#define FPS_LOG_MS 5000
#define DRAW_LOG_FRAMES 30

#define LIST_VISIBLE_ROWS 4
#define LIST_ROWS_ABOVE_SEL 2

#define BUTTONS_VERTICAL (BTN_UP | BTN_DOWN)
#define BUTTONS_FACE_OTHER (BTN_B | BTN_X | BTN_Y)
#define BUTTONS_LEAVE_DIAGNOSTICS (BTN_SELECT | BTN_START)

static struct {
    input_t input;
    int system_index, diagnostics_open, frames;
    float carousel, smoothing;
    char notice[NOTICE_SIZE];
    uint32_t started_at, launch_at, notice_at, repeat_at, start_held_since, last_frame_at, fps_window_start;
    uint32_t previous_buttons;
} menu = {.smoothing = SMOOTHING_PER_FRAME};

/* approach at a speed independent of FPS */
static float approach(float value, float target)
{
    return value + (target - value) * menu.smoothing;
}

/* where the list scroll rests so the marked game stays in view */
static float scroll_target(const system_t *system)
{
    if (system->game_count <= LIST_VISIBLE_ROWS) {
        return 0;
    }
    float target = system->sel - LIST_ROWS_ABOVE_SEL < 0 ? 0 : system->sel - LIST_ROWS_ABOVE_SEL;
    if (target > system->game_count - LIST_VISIBLE_ROWS) {
        target = system->game_count - LIST_VISIBLE_ROWS;
    }

    return target;
}

static void scan_games(void)
{
    for (int system_index = 0; system_index < catalog_system_count(); system_index += 1) {
        catalog_scan_system(catalog_system(system_index));
    }
}

/* the list starts already scrolled to the marked game */
static void restore_position(void)
{
    menu.system_index = catalog_load_state();
    menu.carousel = menu.system_index;
    for (int system_index = 0; system_index < catalog_system_count(); system_index += 1) {
        system_t *system = catalog_system(system_index);
        system->scroll = scroll_target(system);
    }
}

static void init_menu(void)
{
    scan_games();
    menu_view_init();
    menu_audio_init();
    menu.started_at = plat_ms();
    restore_position();
    menu.last_frame_at = plat_ms();
    menu.fps_window_start = menu.last_frame_at;
}

static void set_notice(const char *format, const char *text, uint32_t now)
{
    snprintf(menu.notice, sizeof menu.notice, format, text);
    menu.notice_at = now;
}

static void toggle_selected_favorite(const system_t *system, uint32_t now)
{
    char dir[SYSTEM_DIR_SIZE], file_copy[PATH_SIZE], title[NAME_SIZE];
    const char *file;
    catalog_game_location(system, system->sel, dir, sizeof dir, &file);
    snprintf(file_copy, sizeof file_copy, "%s", file); /* toggle changes the favorites list (file may point into it) */
    catalog_pretty_name(file_copy, title, sizeof title);
    int added = catalog_toggle_favorite(dir, file_copy);
    set_notice(added ? "%s added to favorites" : "%s removed from favorites", title, now);
    details_invalidate(); /* the favorites list may have changed order */
    menu_audio_play(added ? SFX_FAVORITE : SFX_UNFAVORITE);
}

/* order: the music ramps down (0.4 s), the (soft) enter sound plays, the message appears and only then the game
 * loads */
static void begin_launch(const system_t *system, uint32_t now)
{
    char dir[SYSTEM_DIR_SIZE], title[NAME_SIZE];
    const char *file;
    catalog_game_location(system, system->sel, dir, sizeof dir, &file);
    catalog_pretty_name(file, title, sizeof title);
    set_notice("Starting %s", title, now);
    menu_audio_set_volume(0);
    menu_audio_play(SFX_ENTER);
    menu.launch_at = now + LAUNCH_DELAY_MS;
}

static void reset_timers_after_game(void)
{
    menu.notice_at = 0;
    menu.previous_buttons = BTN_A | BTN_START | BTN_SELECT;
    menu.start_held_since = plat_ms() + RETURN_START_GRACE_MS;
    menu.last_frame_at = plat_ms();
}

/* on the stick the menu exits for good to open the game (all memory goes to the game) */
static void run_game(const char *runner, const char *dir, const char *file)
{
    menu_audio_release();
    catalog_save_state(menu.system_index);
    char path[PATH_SIZE], saves[PATH_SIZE];
    snprintf(path, sizeof path, "%s/%s/%s", plat_roms_dir(), dir, file);
    snprintf(saves, sizeof saves, "%s/../saves", plat_roms_dir());
    plat_log("menu: opening game", 0);
    int exit_code = plat_launch(runner, path, saves);
    plat_log("menu: game ended, code %d", exit_code);
    reset_timers_after_game(); /* START may still be held */
    menu_audio_reopen();
    menu_audio_set_volume(1);
}

static void launch_game(const system_t *system, uint32_t now)
{
    char dir[SYSTEM_DIR_SIZE];
    const char *file;
    catalog_game_location(system, system->sel, dir, sizeof dir, &file);
    printf("LAUNCH %s %s/%s/%s\n", dir, plat_roms_dir(), dir, file);
    fflush(stdout);
    const char *runner = plat_runner(dir);
    if (runner) {
        run_game(runner, dir, file);

        return;
    }
    set_notice("Emulator for %s not installed yet", dir, now);
    menu_audio_set_volume(1);
}

static void change_system(uint32_t pressed)
{
    int system_count = catalog_system_count();
    if (pressed & (BTN_RIGHT | BTN_R)) {
        menu.system_index = (menu.system_index + 1) % system_count;
        menu_audio_play(SFX_SYSTEM);
    }
    if (pressed & (BTN_LEFT | BTN_L)) {
        menu.system_index = (menu.system_index + system_count - 1) % system_count;
        menu_audio_play(SFX_SYSTEM);
    }
}

/* list wraps: up from the first goes to the last, down from the last goes back to the first */
static void move_selection(system_t *system, uint32_t pressed)
{
    if (!system->game_count) {
        return;
    }
    if (pressed & BTN_DOWN) {
        system->sel = system->sel < system->game_count - 1 ? system->sel + 1 : 0;
        menu_audio_play(SFX_MOVE);
    }
    if (pressed & BTN_UP) {
        system->sel = system->sel > 0 ? system->sel - 1 : system->game_count - 1;
        menu_audio_play(SFX_MOVE);
    }
}

static void browse(system_t *system, uint32_t bt, uint32_t pressed, uint32_t now)
{
    change_system(pressed);
    move_selection(system, pressed);
    if (pressed & BTN_SELECT) {
        menu.diagnostics_open = 1;
        menu_audio_play(SFX_OPEN);
    }
    if (pressed & BTN_Y && system->game_count) {
        toggle_selected_favorite(system, now);
    }
    /* A opens the game only on its own: the right analog stick turns into A/B/X/Y, and on a diagonal sends A+B
     * together */
    if (pressed & BTN_A && !(bt & BUTTONS_FACE_OTHER) && system->game_count) {
        begin_launch(system, now);
    }
}

static void handle_buttons(uint32_t bt, uint32_t pressed, uint32_t now)
{
    system_t *system = catalog_system(menu.system_index);
    if (menu.diagnostics_open) {
        if ((bt & BUTTONS_LEAVE_DIAGNOSTICS) == BUTTONS_LEAVE_DIAGNOSTICS) {
            menu.diagnostics_open = 0;
            menu_audio_play(SFX_BACK);
        }

        return;
    }
    if (!menu.launch_at) {
        browse(system, bt, pressed, now);

        return;
    }
    if ((int32_t)(now - menu.launch_at) >= 0) {
        menu.launch_at = 0;
        launch_game(system, now);
    }
}

static void update_smoothing(uint32_t now)
{
    float frame_steps = (now - menu.last_frame_at) / FRAME_MS;
    menu.last_frame_at = now;
    if (frame_steps > MAX_FRAME_STEPS) {
        frame_steps = MAX_FRAME_STEPS;
    }
    menu.smoothing = 1 - __builtin_powf(1 - SMOOTHING_PER_FRAME, frame_steps);
}

/* holding START alone (no SELECT, which is the quit-game shortcut) exits the menu; on the stick it returns to the
 * 10 in 1 */
static int start_held_to_quit(uint32_t bt, uint32_t now)
{
    if (!(bt & BTN_START) || (bt & BTN_SELECT)) {
        menu.start_held_since = now;

        return 0;
    }
    if ((int32_t)(now - menu.start_held_since) > START_QUIT_MS) {
        printf("QUIT by START\n");

        return 1;
    }

    return 0;
}

/* adds the repeat while holding up/down */
static uint32_t apply_key_repeat(uint32_t bt, uint32_t pressed, uint32_t now)
{
    if (pressed & BUTTONS_VERTICAL) {
        menu.repeat_at = now + REPEAT_DELAY_MS;

        return pressed;
    }
    if ((bt & BUTTONS_VERTICAL) && (int32_t)(now - menu.repeat_at) > REPEAT_INTERVAL_MS) {
        menu.repeat_at = now;

        return pressed | (bt & BUTTONS_VERTICAL);
    }

    return pressed;
}

/* the carousel follows the index along the shortest path */
static void animate(void)
{
    int system_count = catalog_system_count();
    float behind = menu.system_index - menu.carousel;
    float target = menu.carousel + ((behind - system_count * (int)(behind / system_count)));
    while (target - menu.carousel > system_count / 2.0f) {
        target -= system_count;
    }
    while (menu.carousel - target > system_count / 2.0f) {
        target += system_count;
    }
    menu.carousel = approach(menu.carousel, target);
    for (int system_index = 0; system_index < system_count; system_index += 1) {
        system_t *system = catalog_system(system_index);
        system->scroll = approach(system->scroll, scroll_target(system));
    }
}

static int notice_alpha(uint32_t now)
{
    uint32_t age = now - menu.notice_at;
    int alpha = age < NOTICE_HOLD_MS ? NOTICE_FULL_ALPHA : NOTICE_FULL_ALPHA - (int)(age - NOTICE_HOLD_MS);
    if (alpha < 0 || !menu.notice_at) {
        return 0;
    }

    return alpha;
}

static void log_draw_time(uint32_t elapsed)
{
    static uint32_t draw_sum, draw_count;
    draw_sum += elapsed;
    draw_count += 1;
    if (draw_count == DRAW_LOG_FRAMES) {
        plat_log("ms per frame: draw %d", draw_sum / DRAW_LOG_FRAMES);
        draw_sum = 0;
        draw_count = 0;
    }
}

/* the bubbles always move: redraw every frame */
static void draw_frame(uint32_t now)
{
    float seconds = (now - menu.started_at) / 1000.0f;
    uint32_t draw_started_at = plat_ms();
    if (menu.diagnostics_open) {
        menu_view_draw_diagnostics(&menu.input, seconds);
    } else {
        menu_view_draw_menu(menu.carousel, &menu.input, menu.notice, notice_alpha(now), seconds);
    }
    log_draw_time(plat_ms() - draw_started_at);
    menu_view_present();
    menu.frames += 1;
}

/* preload the neighboring details (one per frame, only if not in the cache) */
static void preload_neighbors(void)
{
    const system_t *system = catalog_system(menu.system_index);
    if (system->game_count <= 1 || menu.diagnostics_open) {
        return;
    }
    details_find(menu.system_index, (system->sel + 1) % system->game_count);
    details_find(menu.system_index, (system->sel + system->game_count - 1) % system->game_count);
}

static void log_fps(uint32_t now)
{
    if (now - menu.fps_window_start < FPS_LOG_MS) {
        return;
    }
    plat_log("fps %d over 5 s", menu.frames * 1000 / (int)(now - menu.fps_window_start));
    menu.frames = 0;
    menu.fps_window_start = now;
}

/* returns 0 when the menu should exit */
static int run_frame(void)
{
    plat_input(&menu.input);
    uint32_t bt = menu.input.bt[0] | menu.input.bt[1], now = plat_ms();
    update_smoothing(now);
    if ((bt & BTN_QUIT) || start_held_to_quit(bt, now)) {
        return 0;
    }
    uint32_t pressed = apply_key_repeat(bt, bt & ~menu.previous_buttons, now);
    menu.previous_buttons = bt;
    menu_audio_feed();
    handle_buttons(bt, pressed, now);
    animate();
    draw_frame(now);
    preload_neighbors();
    log_fps(now);

    return 1;
}

int main(void)
{
    if (plat_init()) {
        return 1;
    }
    init_menu();
    int running = 1;
    while (running) {
        running = run_frame();
    }
    plat_shutdown();

    return 0;
}
