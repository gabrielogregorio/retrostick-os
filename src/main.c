/* Game stick menu: system carousel + game list + diagnostics screen.
 * Same base on PC (plat_sdl.c) and on the stick (plat_stick.c). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "plat.h"

#define MAX_GAMES 1000

typedef struct {
    const char *dir, *name, *short_name;
    uint32_t color;
    const char *ext[4];
    char **games;
    int n, sel;
    float scroll;
} system_t;

static system_t SYSTEMS[] = {
    {"nes", "Nintendo Entertainment System", "NES", 0xe0484a, {".nes", ".zip"}},
    {"snes", "Super Nintendo", "SNES", 0x7a5cf0, {".sfc", ".smc", ".zip"}},
    {"md", "Mega Drive", "MD", 0x2f8cf0, {".md", ".gen", ".bin", ".zip"}},
    {"sms", "Master System", "SMS", 0x23b5a3, {".sms", ".zip"}},
    {"gba", "Game Boy Advance", "GBA", 0xf09a2f, {".gba", ".zip"}},
    {"gb", "Game Boy / Color", "GB", 0x8bbf3a, {".gb", ".gbc", ".zip"}},
};
#define NUM_SYSTEMS ((int)(sizeof SYSTEMS / sizeof SYSTEMS[0]))

static uint32_t screen[SCREEN_W * SCREEN_H];

static int has_ext(const char *name, const char *const *ext)
{
    const char *p = strrchr(name, '.');
    if (!p) return 0;
    for (; *ext; ext++)
        if (!strcasecmp(p, *ext)) return 1;
    return 0;
}

static int cmp(const void *a, const void *b) { return strcasecmp(*(char **)a, *(char **)b); }

static void scan_system(system_t *s)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", plat_roms_dir(), s->dir);
    DIR *d = opendir(path);
    s->games = calloc(MAX_GAMES, sizeof(char *));
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && s->n < MAX_GAMES)
        if (e->d_name[0] != '.' && has_ext(e->d_name, s->ext)) s->games[s->n++] = strdup(e->d_name);
    closedir(d);
    qsort(s->games, s->n, sizeof(char *), cmp);
}

/* game name without extension and without "(USA)" etc. */
static void pretty_name(const char *file, char *out, int max)
{
    int j = 0;
    for (int i = 0; file[i] && j < max - 1; i++) {
        if (file[i] == '(' || file[i] == '[' || (file[i] == '.' && !strchr(file + i + 1, '.'))) break;
        out[j++] = file[i] == '_' ? ' ' : file[i];
    }
    while (j && out[j - 1] == ' ') j--;
    out[j] = 0;
}

/* approach at a speed independent of FPS: k = fraction per 16 ms */
static float dt_k = 0.22f;
static float approach(float a, float b) { return a + (b - a) * dt_k; }

static const char *BUTTON_NAMES[12] = {"Up", "Down", "Left", "Right", "A", "B", "X", "Y", "L", "R", "Select", "Start"};

static void draw_diagnostics(const input_t *e)
{
    gfx_gradient_v(0x1a2238, 0x0e1322);
    gfx_text(&font_big, 32, 50, "Controller diagnostics", 0xffffff, 255);
    gfx_text(&font_small, 32, 76, "Press buttons to see what arrives. SELECT + START goes back.", 0xa8b3cf, 255);
    for (int p = 0; p < 2; p++) {
        int y = 100 + p * 120;
        gfx_round_rect(24, y, SCREEN_W - 48, 108, 10, 0x2a3350, 220);
        char l[96];
        snprintf(l, sizeof l, "Player %d  %s", p + 1, e->present[p] ? "connected" : "no signal");
        gfx_text(&font_small, 40, y + 22, l, e->present[p] ? 0x7be08a : 0xe07b7b, 255);
        const uint8_t *c = e->raw[p];
        snprintf(l, sizeof l, "raw: %02X %02X %02X %02X %02X %02X %02X %02X %02X", c[0], c[1], c[2], c[3], c[4], c[5],
                 c[6], c[7], c[8]);
        gfx_text(&font_small, 300, y + 22, l, 0xd0d6e6, 255);
        int x = 40;
        for (int b = 0; b < 12; b++) {
            int on = e->bt[p] & (1u << b);
            int w = gfx_width(&font_small, BUTTON_NAMES[b]) + 16;
            gfx_round_rect(x, y + 44, w, 26, 8, on ? 0xf0c040 : 0x3a4566, 255);
            gfx_text(&font_small, x + 8, y + 62, BUTTON_NAMES[b], on ? 0x1a1a1a : 0xc0c8dc, 255);
            x += w + 6;
            if (x > SCREEN_W - 90 && b < 11) { x = 40; y += 32; }
        }
    }
}

static void draw_menu(float carousel, const input_t *e, const char *notice, int notice_alpha)
{
    system_t *s = &SYSTEMS[(int)(carousel + 0.5f + NUM_SYSTEMS * 100) % NUM_SYSTEMS];
    uint32_t color = s->color;
    /* cached background: recomputes the gradient only when the system changes */
    static uint32_t background[SCREEN_W * SCREEN_H], background_color = 1;
    if (background_color != color) {
        gfx_target(background, SCREEN_W, SCREEN_H);
        gfx_gradient_v(gfx_blend(0x141a2e, color, 40), 0x0c101d);
        gfx_target(screen, SCREEN_W, SCREEN_H);
        background_color = color;
    }
    memcpy(screen, background, sizeof screen);

    /* top */
    gfx_text(&font_big, 28, 42, "GAME STICK", 0xffffff, 255);
    for (int p = 0; p < 2; p++) {
        int x = SCREEN_W - 110 + p * 44;
        gfx_round_rect(x, 20, 36, 24, 12, e->present[p] ? 0x7be08a : 0x3a4566, 255);
        char t[4] = {'P', '1' + p, 0};
        gfx_text(&font_small, x + 9, 38, t, e->present[p] ? 0x10301a : 0x8a94ad, 255);
    }

    /* system carousel */
    for (int k = -3; k <= 3; k++) {
        int i = (int)carousel + k;
        float d = i - carousel;
        int idx = ((i % NUM_SYSTEMS) + NUM_SYSTEMS) % NUM_SYSTEMS;
        system_t *c = &SYSTEMS[idx];
        float ad = d < 0 ? -d : d;
        int w = 150 - (int)(ad * 22), h = 92 - (int)(ad * 14);
        if (w < 60) continue;
        int cx = SCREEN_W / 2 + (int)(d * 176);
        int x = cx - w / 2, y = 120 - h / 2;
        int alpha = ad < 1 ? 255 - (int)(ad * 110) : 145 - (int)((ad - 1) * 50);
        if (alpha < 20) continue;
        gfx_round_rect(x + 3, y + 5, w, h, 14, 0x060812, alpha / 3);     /* shadow, never pure black */
        gfx_round_rect_grad(x, y, w, h, 14, gfx_blend(c->color, 0xffffff, 40), gfx_blend(c->color, 0x0c101d, 90), alpha);
        int tw = gfx_width(&font_big, c->short_name);
        gfx_text(&font_big, cx - tw / 2, y + h / 2 + 4, c->short_name, 0xffffff, alpha);
        char n[24];
        snprintf(n, sizeof n, "%d games", c->n);
        tw = gfx_width(&font_small, n);
        gfx_text(&font_small, cx - tw / 2, y + h - 12, n, 0xeef0f8, alpha * 3 / 4);
    }
    int tw = gfx_width(&font_small, s->name);
    gfx_text(&font_small, SCREEN_W / 2 - tw / 2, 188, s->name, 0xd8deee, 255);

    /* game list */
    int px = 60, py = 200, pw = SCREEN_W - 120, ph = 118, line = 26;
    gfx_round_rect(px, py, pw, ph, 12, 0x1c2440, 200);
    if (!s->n) {
        char m[96];
        snprintf(m, sizeof m, "No games. Copy them to roms/%s on the SD card.", s->dir);
        tw = gfx_width(&font_small, m);
        gfx_text(&font_small, SCREEN_W / 2 - tw / 2, py + ph / 2 + 5, m, 0x8a94ad, 255);
    } else {
        float top = s->scroll;
        int first = (int)top;
        for (int i = first; i < s->n && i < first + 6; i++) {
            int y = py + 8 + (int)((i - top) * line);
            if (y < py + 2 || y > py + ph - line) continue;
            if (i == s->sel) gfx_round_rect(px + 8, y, pw - 16, line - 2, 8, color, 235);
            char nm[128];
            pretty_name(s->games[i], nm, sizeof nm);
            gfx_text(&font_small, px + 20, y + 18, nm, i == s->sel ? 0xffffff : 0xb8c0d6, 255);
        }
    }

    /* footer */
    gfx_text(&font_small, 60, 344, "A  Play     L/R  System     SELECT  Diagnostics     Hold START  Quit", 0x8a94ad, 255);

    if (notice_alpha > 0) {
        int w = gfx_width(&font_small, notice) + 40;
        gfx_round_rect(SCREEN_W / 2 - w / 2, 150, w, 40, 12, 0x2a3350, notice_alpha * 240 / 255);
        gfx_text(&font_small, SCREEN_W / 2 - w / 2 + 20, 175, notice, 0xffffff, notice_alpha);
    }
}

int main(void)
{
    if (plat_init()) return 1;
    gfx_target(screen, SCREEN_W, SCREEN_H);
    for (int i = 0; i < NUM_SYSTEMS; i++) scan_system(&SYSTEMS[i]);

    int sel = 0, diag = 0;
    float carousel = 0;
    uint32_t prev = 0, t_repeat = 0, t_notice = 0, t_start = 0, t_prev = plat_ms(), t_fps = t_prev;
    int frames = 0, idle = 0;
    float carousel_prev = -1, scroll_prev = -1;
    input_t prev_input;
    memset(&prev_input, 0, sizeof prev_input);
    char notice[160] = "";
    input_t e;

    for (;;) {
        plat_input(&e);
        uint32_t bt = e.bt[0] | e.bt[1], now = plat_ms();
        float dt = (now - t_prev) / 16.0f;
        t_prev = now;
        if (dt > 6) dt = 6;
        dt_k = 1 - __builtin_powf(1 - 0.22f, dt);
        if (bt & BTN_QUIT) break;
        /* holding START for 3 s exits the menu (on the stick it returns to the 10 in 1) */
        /* START alone (no SELECT, which is the quit-game shortcut) */
        if (!(bt & BTN_START) || (bt & BTN_SELECT)) t_start = now;
        else if ((int32_t)(now - t_start) > 3000) { printf("QUIT by START\n"); break; }
        uint32_t pressed = bt & ~prev;
        /* repeat while holding up/down */
        if ((bt & (BTN_UP | BTN_DOWN)) && !(pressed & (BTN_UP | BTN_DOWN))) {
            if ((int32_t)(now - t_repeat) > 150) { pressed |= bt & (BTN_UP | BTN_DOWN); t_repeat = now; }
        } else if (pressed & (BTN_UP | BTN_DOWN)) t_repeat = now + 450;
        prev = bt;

        system_t *s = &SYSTEMS[sel];
        if (diag) {
            if ((bt & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) diag = 0;
        } else {
            if (pressed & (BTN_RIGHT | BTN_R)) sel = (sel + 1) % NUM_SYSTEMS;
            if (pressed & (BTN_LEFT | BTN_L)) sel = (sel + NUM_SYSTEMS - 1) % NUM_SYSTEMS;
            if (pressed & BTN_DOWN && s->sel < s->n - 1) s->sel++;
            if (pressed & BTN_UP && s->sel > 0) s->sel--;
            if (pressed & BTN_SELECT) diag = 1;
            if (pressed & BTN_A && s->n) {
                char nm[128];
                pretty_name(s->games[s->sel], nm, sizeof nm);
                snprintf(notice, sizeof notice, "Starting %s", nm);
                t_notice = now;
                printf("LAUNCH %s %s/%s/%s\n", s->dir, plat_roms_dir(), s->dir, s->games[s->sel]);
                fflush(stdout);
                const char *r = plat_runner(s->dir);
                if (r) {
                    char path[600], saves[600];
                    snprintf(path, sizeof path, "%s/%s/%s", plat_roms_dir(), s->dir, s->games[s->sel]);
                    snprintf(saves, sizeof saves, "%s/../saves", plat_roms_dir());
                    plat_log("menu: opening game", 0);
                    int rc = plat_launch(r, path, saves);
                    plat_log("menu: game ended, code %d", rc);
                    /* back from the game: reset the timers (START may still be held) */
                    t_notice = 0; prev = BTN_A | BTN_START | BTN_SELECT; carousel_prev = -1;
                    t_start = plat_ms() + 2000; t_prev = plat_ms();
                } else {
                    snprintf(notice, sizeof notice, "Emulator for %s not installed yet", s->short_name);
                }
            }
        }

        /* animation: the carousel follows the index along the shortest path */
        float target = carousel + (((sel - carousel) - NUM_SYSTEMS * (int)((sel - carousel) / NUM_SYSTEMS)));
        while (target - carousel > NUM_SYSTEMS / 2.0f) target -= NUM_SYSTEMS;
        while (carousel - target > NUM_SYSTEMS / 2.0f) target += NUM_SYSTEMS;
        carousel = approach(carousel, target);
        for (int i = 0; i < NUM_SYSTEMS; i++) {
            system_t *c = &SYSTEMS[i];
            float r = c->sel - 2 < 0 ? 0 : c->sel - 2;
            if (r > c->n - 4 && c->n > 4) r = c->n - 4;
            if (c->n <= 4) r = 0;
            c->scroll = approach(c->scroll, r);
        }

        int aa = now - t_notice < 1400 ? 255 : 255 - (int)(now - t_notice - 1400);
        if (aa < 0 || !t_notice) aa = 0;
        /* redraw only if something moved (saves CPU on the stick) */
        float scroll = 0;
        for (int i = 0; i < NUM_SYSTEMS; i++) scroll += SYSTEMS[i].scroll + SYSTEMS[i].sel;
        int changed = diag || aa || pressed || __builtin_fabsf(carousel - carousel_prev) > 0.001f || __builtin_fabsf(scroll - scroll_prev) > 0.001f ||
                    memcmp(e.present, prev_input.present, sizeof e.present);
        carousel_prev = carousel; scroll_prev = scroll; prev_input = e;
        if (changed || idle++ > 60) {
            idle = 0;
            uint32_t td = plat_ms();
            if (diag) draw_diagnostics(&e);
            else draw_menu(carousel, &e, notice, aa);
            static uint32_t draw_sum, draw_count;
            draw_sum += plat_ms() - td;
            if (++draw_count == 30) { plat_log("ms per frame: draw %d", draw_sum / 30); draw_sum = draw_count = 0; }
            plat_present(screen);
            frames++;
        } else plat_sleep(16);
        if (now - t_fps >= 5000) {
            plat_log("fps %d over 5 s", frames * 1000 / (int)(now - t_fps));
            frames = 0; t_fps = now;
        }
    }
    plat_shutdown();
    return 0;
}
