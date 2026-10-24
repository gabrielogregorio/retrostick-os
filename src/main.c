/* Game stick menu: system carousel + game list + diagnostics screen.
 * Same base on PC (plat_sdl.c) and on the stick (plat_stick.c). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aero.h"
#include "gfx.h"
#include "icons.h"
#include "music.h"
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
    {"fav", "Favorites", "FAV", 0xe8a020, {NULL}}, /* pseudo-system: games marked with Y, from any system */
    {"nes", "Nintendo Entertainment System", "NES", 0xe0484a, {".nes", ".zip"}},
    {"snes", "Super Nintendo", "SNES", 0x7a5cf0, {".sfc", ".smc", ".zip"}},
    {"md", "Mega Drive", "MD", 0x2f8cf0, {".md", ".gen", ".bin", ".zip"}},
    {"sms", "Master System", "SMS", 0x23b5a3, {".sms", ".zip"}},
    {"gba", "Game Boy Advance", "GBA", 0xf09a2f, {".gba", ".zip"}},
    {"gb", "Game Boy / Color", "GB", 0x8bbf3a, {".gb", ".gbc", ".zip"}},
    {"gg", "Game Gear", "GG", 0x5a6b8c, {".gg"}},
    {"a26", "Atari 2600", "2600", 0xb5783a, {".a26", ".bin"}},
    {"ngp", "Neo Geo Pocket", "NGP", 0x2f8fd8, {".ngc", ".ngp"}},
    {"ws", "WonderSwan", "WS", 0xc04a8a, {".ws", ".wsc"}},
};
#define NUM_SYSTEMS ((int)(sizeof SYSTEMS / sizeof SYSTEMS[0]))

static uint32_t screen[SCREEN_W * SCREEN_H];

/* ---------- favorites: retro/favorites.txt, one "system/file" line per game ---------- */
static int fav_version; /* changes on every edit (rebuilds the static screen) */
static int is_fav(const system_t *s) { return !strcmp(s->dir, "fav"); }

/* real folder and file of game i (in favorites the entry holds "system/file") */
static void real_game(const system_t *s, int i, char *dir, int dmax, const char **file)
{
    const char *j = s->games[i], *b = strchr(j, '/');
    if (is_fav(s) && b) { snprintf(dir, dmax, "%.*s", (int)(b - j), j); *file = b + 1; }
    else { snprintf(dir, dmax, "%s", s->dir); *file = j; }
}

static void fav_path(char *out, int max) { snprintf(out, max, "%s/../favorites.txt", plat_roms_dir()); }

static int has_ext(const char *name, const char *const *ext)
{
    const char *p = strrchr(name, '.');
    if (!p) return 0;
    for (; *ext; ext++)
        if (!strcasecmp(p, *ext)) return 1;
    return 0;
}

static int cmp(const void *a, const void *b) { return strcasecmp(*(char **)a, *(char **)b); }

static void load_favorites(system_t *s)
{
    if (!s->games) s->games = calloc(MAX_GAMES, sizeof(char *));
    for (int i = 0; i < s->n; i++) free(s->games[i]);
    s->n = 0;
    char path[600], l[600];
    fav_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (s->n < MAX_GAMES && fgets(l, sizeof l, f)) {
        l[strcspn(l, "\r\n")] = 0;
        if (!strchr(l, '/')) continue;
        char rom[1300];
        snprintf(rom, sizeof rom, "%s/%s", plat_roms_dir(), l);
        FILE *t = fopen(rom, "rb"); /* only what still exists on the SD card */
        if (!t) continue;
        fclose(t);
        s->games[s->n++] = strdup(l);
    }
    fclose(f);
    qsort(s->games, s->n, sizeof(char *), cmp);
}

static int is_favorite(const char *dir, const char *file)
{
    char k[600];
    snprintf(k, sizeof k, "%s/%s", dir, file);
    for (int i = 0; i < SYSTEMS[0].n; i++)
        if (!strcmp(SYSTEMS[0].games[i], k)) return 1;
    return 0;
}

/* marks/unmarks and rewrites the whole file; returns 1 if it ended up a favorite */
static int toggle_favorite(const char *dir, const char *file)
{
    char k[640], path[600];
    snprintf(k, sizeof k, "%s/%s", dir, file);
    system_t *f = &SYSTEMS[0];
    int found = -1;
    for (int i = 0; i < f->n; i++)
        if (!strcmp(f->games[i], k)) found = i;
    if (found >= 0) {
        free(f->games[found]);
        memmove(&f->games[found], &f->games[found + 1], (f->n - found - 1) * sizeof(char *));
        f->n--;
    } else if (f->n < MAX_GAMES) {
        f->games[f->n++] = strdup(k);
        qsort(f->games, f->n, sizeof(char *), cmp);
    }
    fav_path(path, sizeof path);
    FILE *o = fopen(path, "w");
    if (o) {
        for (int i = 0; i < f->n; i++) fprintf(o, "%s\n", f->games[i]);
        fclose(o);
    }
    if (f->sel >= f->n) f->sel = f->n ? f->n - 1 : 0;
    fav_version++;
    return found < 0;
}

static void scan_system(system_t *s)
{
    if (is_fav(s)) { load_favorites(s); return; }
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

static uint32_t sky[SCREEN_W * SCREEN_H]; /* Aero background, built once at startup (expensive) */

static void draw_diagnostics(const input_t *e, float secs)
{
    memcpy(screen, sky, sizeof screen);
    aero_bubbles(screen, secs);
    aero_text(&font_big, 32, 50, "Controller diagnostics", 0xffffff, 255);
    aero_text(&font_small, 32, 76, "Press buttons to see what arrives. SELECT + START goes back.", 0xffffff, 255);
    for (int p = 0; p < 2; p++) {
        int y = 100 + p * 120;
        aero_glass(24, y, SCREEN_W - 48, 108, 14, 170);
        char l[96];
        snprintf(l, sizeof l, "Player %d  %s", p + 1, e->present[p] ? "connected" : "no signal");
        gfx_text(&font_small, 40, y + 22, l, e->present[p] ? 0x1a8a2a : 0xc03838, 255);
        const uint8_t *c = e->raw[p];
        snprintf(l, sizeof l, "raw: %02X %02X %02X %02X %02X %02X %02X %02X %02X", c[0], c[1], c[2], c[3], c[4], c[5],
                 c[6], c[7], c[8]);
        gfx_text(&font_small, 300, y + 22, l, 0x24486a, 255);
        int x = 40;
        for (int b = 0; b < 12; b++) {
            int on = e->bt[p] & (1u << b);
            int w = gfx_width(&font_small, BUTTON_NAMES[b]) + 16;
            aero_pill(x, y + 44, w, 24, 12, on ? 0xffc030 : 0x8fb4cf, 255);
            gfx_text(&font_small, x + 8, y + 61, BUTTON_NAMES[b], on ? 0x3a2400 : 0x0e2a44, 255);
            x += w + 6;
            if (x > SCREEN_W - 90 && b < 11) { x = 40; y += 32; }
        }
    }
}

/* ---------- game details: cover + year/genre/developer/description (generated by tools/gen_covers.py) ---------- */
#define COVER_W 60 /* tools/gen_covers.py writes this size */
#define COVER_H 86
typedef struct {
    int sys, sel, has_cover;
    uint32_t cover[COVER_W * COVER_H];
    char year[8], genre[48], dev[64], desc[700];
} game_details_t;
/* cache of 4 game details: the current one and its preloaded neighbors (the cover shows up instantly, no flicker) */
static game_details_t details_cache[4] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
static game_details_t *cur_details = &details_cache[0];
#define details (*cur_details)
static void invalidate_details(void) { for (int i = 0; i < 4; i++) details_cache[i].sys = details_cache[i].sel = -1; }

static void rom_basename(const char *file, char *out, int max)
{
    snprintf(out, max, "%s", file);
    char *p = strrchr(out, '.');
    if (p) *p = 0;
}

static void read_line(FILE *f, char *out, int max)
{
    out[0] = 0;
    if (!fgets(out, max, f)) return;
    out[strcspn(out, "\r\n")] = 0;
}

static void load_into(game_details_t *fx, int si, int sel);

/* find in the cache (or load into a free slot) without changing the current details */
static game_details_t *find_details(int si, int sel)
{
    static unsigned usage[4], tick;
    int free_slot = -1;
    for (int i = 0; i < 4; i++) {
        if (details_cache[i].sys == si && details_cache[i].sel == sel) { usage[i] = ++tick; return &details_cache[i]; }
        if (&details_cache[i] != cur_details && (free_slot < 0 || usage[i] < usage[free_slot])) free_slot = i;
    }
    load_into(&details_cache[free_slot], si, sel);
    usage[free_slot] = ++tick;
    return &details_cache[free_slot];
}

static void load_details(int si, int sel) { cur_details = find_details(si, sel); }

static void load_into(game_details_t *fx, int si, int sel)
{
#undef details
#define details (*fx)
    details.sys = si; details.sel = sel; details.has_cover = 0;
    details.year[0] = details.genre[0] = details.dev[0] = details.desc[0] = 0;
    system_t *s = &SYSTEMS[si];
    if (sel < 0 || sel >= s->n) return;
    char base[300], path[700], dir[16];
    const char *file;
    real_game(s, sel, dir, sizeof dir, &file);
    rom_basename(file, base, sizeof base);
    snprintf(path, sizeof path, "%s/%s/.meta/%s.txt", plat_roms_dir(), dir, base);
    FILE *f = fopen(path, "r");
    if (f) {
        read_line(f, details.year, sizeof details.year);
        read_line(f, details.genre, sizeof details.genre);
        read_line(f, details.dev, sizeof details.dev);
        read_line(f, details.desc, sizeof details.desc);
        fclose(f);
    }
    /* cover: 'CAP1', u16 w, u16 h, 16 RGB565 colors, 4-bit pixels */
    snprintf(path, sizeof path, "%s/%s/.meta/%s.cap", plat_roms_dir(), dir, base);
    f = fopen(path, "rb");
    if (!f) return;
    uint8_t hdr[8 + 32], px[COVER_W * COVER_H / 2];
    if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr && !memcmp(hdr, "CAP1", 4) && (hdr[4] | hdr[5] << 8) == COVER_W &&
        (hdr[6] | hdr[7] << 8) == COVER_H && fread(px, 1, sizeof px, f) == sizeof px) {
        uint32_t pal[16];
        for (int i = 0; i < 16; i++) {
            uint32_t c = hdr[8 + i * 2] | hdr[9 + i * 2] << 8;
            uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            pal[i] = (r << 19 | (r >> 2) << 16) | (g << 10 | (g >> 4) << 8) | (b << 3 | b >> 2);
        }
        for (int i = 0; i < COVER_W * COVER_H / 2; i++) {
            details.cover[i * 2] = pal[px[i] >> 4];
            details.cover[i * 2 + 1] = pal[px[i] & 15];
        }
        details.has_cover = 1;
    }
    fclose(f);
#undef details
#define details (*cur_details)
}

/* trims the text with "..." to fit in max pixels */
static const char *fit_text(const font_t *f, const char *t, int max)
{
    static char b[256];
    if (gfx_width(f, t) <= max) return t;
    int n = snprintf(b, sizeof b - 4, "%s", t);
    while (n > 0) {
        do n--; while (n > 0 && (b[n] & 0xc0) == 0x80); /* do not cut in the middle of a UTF-8 character */
        strcpy(b + n, "...");
        if (gfx_width(f, b) <= max) break;
    }
    return b;
}

/* wraps the text into at most max_lines lines of width w; the last one gets "..." if text is left over */
static void wrapped_text(const font_t *f, int x, int y, int w, int max_lines, const char *t, uint32_t color)
{
    char line[256];
    for (int l = 0; l < max_lines && *t; l++) {
        while (*t == ' ') t++;
        int n = 0, cut = 0;
        for (;;) {
            int m = n;
            while (t[m] && t[m] != ' ') m++;
            if (m >= (int)sizeof line - 4) break;
            memcpy(line, t, m); line[m] = 0;
            if (gfx_width(f, line) > w && n) break;
            n = m; cut = m;
            if (!t[m]) break;
            n++;
        }
        if (!cut) cut = n ? n : (int)strlen(t);
        memcpy(line, t, cut); line[cut] = 0;
        t += cut;
        if (l == max_lines - 1 && *t) gfx_text(f, x, y, fit_text(f, strcat(line, " ..."), w), color, 255);
        else gfx_text(f, x, y, line, color, 255);
        y += f->line - 2;
    }
}

/* round footer button + caption */
static int hint(int x, const char *bt, uint32_t color, const char *txt)
{
    int w = gfx_width(&font_small, bt);
    int bw = w + 14 < 22 ? 22 : w + 14;
    aero_pill(x, 330, bw, 22, 11, color, 255);
    gfx_text(&font_small, x + (bw - w) / 2, 346, bt, 0xffffff, 255);
    aero_text(&font_small, x + bw + 6, 346, txt, 0xffffff, 255);
    return x + bw + 6 + gfx_width(&font_small, txt) + 18;
}

static void draw_static(system_t *s, const input_t *e);
static uint32_t *static_target;
static void draw_menu(float carousel, const input_t *e, const char *notice, int notice_alpha, float secs)
{
    system_t *s = &SYSTEMS[(int)(carousel + 0.5f + NUM_SYSTEMS * 100) % NUM_SYSTEMS];
    /* Cached static screen: sky + system tint + controllers + name + panel (list, cover, details) + footer.
     * Rebuilt only when something in it changes; each frame adds just the bubbles and the carousel. */
    static uint32_t static_layer[SCREEN_W * SCREEN_H];
    static int key[7] = {-1};
    int k[7] = {(int)(s - SYSTEMS), s->sel, (int)(s->scroll * 100), e->present[0], e->present[1], details.sel, fav_version};
    if (memcmp(k, key, sizeof k)) {
        memcpy(key, k, sizeof k);
        memcpy(static_layer, sky, sizeof static_layer);
        gfx_target(static_layer, SCREEN_W, SCREEN_H);
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, s->color, 22);
        static_target = static_layer;
        draw_static(s, e);
        gfx_target(screen, SCREEN_W, SCREEN_H);
        key[5] = details.sel; /* the details may have just been loaded */
    }
    memcpy(screen, static_layer, sizeof screen);
    aero_bubbles(screen, secs);

    /* carousel: glass bubbles with the console inside, floating */
    for (int k2 = -3; k2 <= 3; k2++) {
        int i = (int)carousel + k2;
        float d = i - carousel;
        int idx = ((i % NUM_SYSTEMS) + NUM_SYSTEMS) % NUM_SYSTEMS;
        float ad = d < 0 ? -d : d;
        float scale = ad < 1 ? 1.0f - ad * 0.32f : 0.68f - (ad - 1) * 0.2f;
        if (scale < 0.3f) continue;
        int alpha = ad < 1 ? 255 - (int)(ad * 60) : 195 - (int)((ad - 1) * 90);
        if (alpha < 20) continue;
        int cx = SCREEN_W / 2 + (int)(d * (ad < 1 ? 130 : 130 + (ad - 1) * -18));
        int cy = 100 + (int)(ad * 6) + (int)(__builtin_sinf(secs * 1.3f + idx * 1.7f) * 3);
        icons_draw(screen, SCREEN_W, SCREEN_H, idx, cx, cy, scale, alpha);
    }

    if (notice_alpha > 0) {
        int w = gfx_width(&font_small, notice) + 48;
        aero_glass(SCREEN_W / 2 - w / 2, 146, w, 44, 18, notice_alpha * 235 / 255);
        gfx_text(&font_small, SCREEN_W / 2 - w / 2 + 24, 173, notice, 0x163a5c, notice_alpha);
    }
}

/* static part of the menu (drawn on the current gfx target) */
static void draw_static(system_t *s, const input_t *e)
{
    /* top: just the controllers */
    for (int p = 0; p < 2; p++) {
        int cx = SCREEN_W - 70 + p * 34;
        aero_orb(cx, 28, 13, e->present[p] ? 0x3cc84a : 0x9aaabb, 255);
        char t[4] = {'P', '1' + p, 0};
        gfx_text(&font_small, cx - gfx_width(&font_small, t) / 2, 33, t, 0xffffff, 255);
    }

    {
        char n[160];
        snprintf(n, sizeof n, "%s", s->name);
        int tw = gfx_width(&font_big, n);
        aero_text(&font_big, SCREEN_W / 2 - tw / 2, 186, n, 0xffffff, 255);
        snprintf(n, sizeof n, s->n == 1 ? "1 game" : "%d games", s->n);
        tw = gfx_width(&font_small, n);
        aero_text(&font_small, SCREEN_W / 2 - tw / 2, 202, n, 0xffffff, 230);
    }
    int tw;

    /* game list on a glass panel: list on the left, cover and details on the right */
    int px = 56, py = 210, pw = SCREEN_W - 112, ph = 112, line = 26;
    aero_glass(px, py, pw, ph, 16, 150);
    if (!s->n) {
        char m[96];
        if (is_fav(s)) snprintf(m, sizeof m, "No favorites yet: press Y on a game.");
        else snprintf(m, sizeof m, "No games. Copy them to roms/%s on the SD card.", s->dir);
        tw = gfx_width(&font_small, m);
        gfx_text(&font_small, SCREEN_W / 2 - tw / 2, py + ph / 2 + 5, m, 0x3a5a78, 255);
    } else {
        int lw = 288; /* list column width */
        float top = s->scroll;
        int first = (int)top;
        /* rows entering and leaving at the edge appear clipped, so they do not vanish and "flicker" while scrolling */
        gfx_clip_y(py + 3, py + ph - 3);
        for (int i = first; i < s->n && i < first + 6; i++) {
            int y = py + 8 + (int)((i - top) * line);
            if (y < py - line || y > py + ph) continue;
            int selected = i == s->sel;
            if (selected) aero_pill(px + 8, y, lw, line - 2, 12, 0x1ea8f0, 255);
            char nm[128], dir[16];
            const char *file;
            real_game(s, i, dir, sizeof dir, &file);
            pretty_name(file, nm, sizeof nm);
            /* on the right: in favorites, the system tag; in systems, the star if it is a favorite */
            int dw = 0;
            if (is_fav(s)) {
                const char *tag = dir;
                for (int q = 1; q < NUM_SYSTEMS; q++) if (!strcmp(SYSTEMS[q].dir, dir)) tag = SYSTEMS[q].short_name;
                dw = gfx_width(&font_tiny, tag) + 10;
                gfx_text(&font_tiny, px + 8 + lw - dw, y + 17, tag, selected ? 0xe8f6ff : 0x4a6f8f, 255);
            } else if (is_favorite(dir, file)) {
                dw = 22;
                gfx_star(px + lw - 6, y + 12, 8, 0xfff6b0, 0xe89a10, 255);
            }
            const char *t = fit_text(&font_small, nm, lw - 24 - dw);
            if (selected) aero_text(&font_small, px + 20, y + 18, t, 0xffffff, 255);
            else gfx_text(&font_small, px + 20, y + 18, t, 0x163a5c, 255);
        }
        gfx_clip_y(0, SCREEN_H);
        /* cover */
        int cx = px + lw + 18, cy = py + (ph - COVER_H) / 2;
        load_details(s - SYSTEMS, s->sel);
        gfx_round_rect(cx - 2, cy - 2, COVER_W + 4, COVER_H + 4, 4, 0xffffff, 230);
        if (details.has_cover) {
            for (int y = 0; y < COVER_H; y++) memcpy(&static_target[(cy + y) * SCREEN_W + cx], &details.cover[y * COVER_W], COVER_W * 4);
        } else {
            aero_pill(cx, cy, COVER_W, COVER_H, 4, s->color, 255);
            int w2 = gfx_width(&font_big, s->short_name);
            aero_text(&font_big, cx + (COVER_W - w2) / 2, cy + COVER_H / 2 + 9, s->short_name, 0xffffff, 255);
        }
        gfx_round_rect_grad(cx, cy, COVER_W, COVER_H * 2 / 5, 3, 0xffffff, 0xffffff, 40); /* glass shine on the cover */
        /* details */
        int fx = cx + COVER_W + 12, fw = px + pw - 12 - fx, fy = py + 20;
        char l2[120];
        if (details.genre[0]) { gfx_text(&font_small, fx, fy, fit_text(&font_small, details.genre, fw), 0x0e5a96, 255); fy += 16; }
        snprintf(l2, sizeof l2, "%s%s%s", details.year, details.year[0] && details.dev[0] ? "  ·  " : "", details.dev);
        if (l2[0]) { gfx_text(&font_tiny, fx, fy, fit_text(&font_tiny, l2, fw), 0x4a6f8f, 255); fy += 16; }
        if (details.desc[0]) wrapped_text(&font_tiny, fx, fy, fw, (py + ph - 6 - fy) / (font_tiny.line - 2) + 1, details.desc, 0x163a5c);
    }

    /* footer */
    int x = 24;
    x = hint(x, "A", 0x2fbf3f, "Play");
    x = hint(x, "Y", 0xe8a020, is_fav(s) ? "Remove" : "Favorite");
    x = hint(x, "L R", 0x7f9bb3, "System");
    x = hint(x, "SELECT", 0x7f9bb3, "Controls");
    hint(x, "START", 0xe0503c, "quit");
}

/* menu audio: music and effects prebuilt in files (tools/gen_audio.c, folder retro/audio on the SD card).
 * The stick only reads and mixes: synthesizing live is too slow there and would drain the queue (choppy audio).
 * Without the files (PC preview without `make audio`), it falls back to live synthesis. */
#define MENU_RATE 48000 /* same as the games; HDMI accepts it for sure */
#define MENU_QUEUE 4000 /* ~83 ms in the device queue (fits in 32 x 1 KB) */
static int audio_ok, audio_from_file;
static FILE *f_intro, *f_loop, *f_current;
static struct { int16_t *d; int n; } sfx[4];      /* SFX_MOVE, SFX_SYSTEM, SFX_OPEN, SFX_BACK */
static struct { int id, pos; } playing[4];        /* effects sounding now (pos < 0 = free) */
static float music_vol = 0, target_vol = 1;

static FILE *open_audio(const char *name)
{
    char path[600];
    const char *dirs[] = {"%s/../audio/%s", "out/audio/%s"};
    snprintf(path, sizeof path, dirs[0], plat_roms_dir(), name);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(path, sizeof path, dirs[1], name); f = fopen(path, "rb"); }
    return f;
}

static void load_audio(void)
{
    static const char *SFX_FILES[4] = {"sfx-v.pcm", "sfx-h.pcm", "sfx-a.pcm", "sfx-b.pcm"};
    f_intro = open_audio("music-intro.pcm");
    f_loop = open_audio("music-loop.pcm");
    for (int i = 0; i < 4; i++) {
        FILE *f = open_audio(SFX_FILES[i]);
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long t = ftell(f);
        fseek(f, 0, SEEK_SET);
        sfx[i].d = malloc(t);
        if (sfx[i].d && fread(sfx[i].d, 1, t, f) == (size_t)t) sfx[i].n = t / 4;
        fclose(f);
    }
    for (int i = 0; i < 4; i++) playing[i].pos = -1;
    audio_from_file = f_intro && f_loop && sfx[0].n && sfx[1].n && sfx[2].n;
    f_current = f_intro;
    plat_log("menu: audio from file %d", audio_from_file);
}

static void play_sfx(int which)
{
    if (!audio_from_file) { music_effect(which); return; }
    int k = 0;
    for (int i = 0; i < 4; i++) if (playing[i].pos < 0) { k = i; break; }
    playing[k].id = which; playing[k].pos = 0;
}

static void set_music_volume(float v) { if (audio_from_file) target_vol = v; else music_volume(v); }

static void feed_audio(void)
{
    if (!audio_ok) return;
    static int16_t buf[MENU_QUEUE * 2];
    int queued = plat_audio_queue();
    if (queued < 0) queued = 0;
    int needed = MENU_QUEUE - queued;
    if (needed <= 0) return;
    if (!audio_from_file) { music_render(buf, needed); plat_audio(buf, needed); return; }
    /* music from file: the intro once, then the loop forever */
    int got = 0;
    while (got < needed) {
        size_t r = fread(buf + got * 2, 4, needed - got, f_current);
        got += r;
        if (got < needed) { f_current = f_loop; fseek(f_loop, 0, SEEK_SET); }
    }
    /* volume (short ramp) + effects summed with saturation */
    float step = (target_vol - music_vol) / needed;
    for (int k = 0; k < needed; k++) {
        music_vol += step;
        int l = (int)(buf[k * 2] * music_vol), r = (int)(buf[k * 2 + 1] * music_vol);
        for (int i = 0; i < 4; i++) {
            if (playing[i].pos < 0) continue;
            const int16_t *e = sfx[playing[i].id].d;
            l += e[playing[i].pos * 2]; r += e[playing[i].pos * 2 + 1];
            if (++playing[i].pos >= sfx[playing[i].id].n) playing[i].pos = -1;
        }
        buf[k * 2] = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
        buf[k * 2 + 1] = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
    }
    plat_audio(buf, needed);
}

/* menu state (chosen system and marked game in each): the menu exits to open the game and comes back here */
static void state_path(char *out, int max) { snprintf(out, max, "%s/../menu-state.txt", plat_roms_dir()); }

static void save_state(int sel)
{
    char path[600];
    state_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "system %d\n", sel);
    for (int i = 0; i < NUM_SYSTEMS; i++) fprintf(f, "%s %d\n", SYSTEMS[i].dir, SYSTEMS[i].sel);
    fclose(f);
}

static int load_state(void)
{
    char path[600], d[32];
    int sel = 1, v; /* no state: starts on NES (0 is Favorites) */
    state_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return sel;
    if (fscanf(f, "system %d", &v) == 1 && v >= 0 && v < NUM_SYSTEMS) sel = v;
    while (fscanf(f, "%31s %d", d, &v) == 2)
        for (int i = 0; i < NUM_SYSTEMS; i++)
            if (!strcmp(SYSTEMS[i].dir, d) && v >= 0 && v < SYSTEMS[i].n) SYSTEMS[i].sel = v;
    fclose(f);
    return sel;
}

int main(void)
{
    if (plat_init()) return 1;
    gfx_target(screen, SCREEN_W, SCREEN_H);
    for (int i = 0; i < NUM_SYSTEMS; i++) scan_system(&SYSTEMS[i]);
    aero_init(SCREEN_W, SCREEN_H);
    for (int i = 0; i < NUM_SYSTEMS && i < ICONS_MAX; i++) icons_build(i, SYSTEMS[i].dir, SYSTEMS[i].color);
    gfx_target(screen, SCREEN_W, SCREEN_H);
    uint32_t tf = plat_ms();
    {
        /* prebuilt background (generated on PC by tools/gen_audio.c); too slow to compute on the stick */
        FILE *f = open_audio("aero-background.bin");
        if (!f || fread(sky, 1, sizeof sky, f) != sizeof sky) aero_background(sky, SCREEN_W, SCREEN_H, 0);
        if (f) fclose(f);
    }
    plat_log("menu: aero background built in %d ms", (int)(plat_ms() - tf));
    music_init(MENU_RATE);
    load_audio();
    const char *music = getenv("STICK_MUSIC");
    audio_ok = !(music && *music == '0') && !plat_audio_init(MENU_RATE);
    uint32_t t0 = plat_ms(), t_launch = 0;

    int sel = load_state(), diag = 0;
    float carousel = sel;
    for (int i = 0; i < NUM_SYSTEMS; i++) { /* list already scrolled to the marked game */
        float r = SYSTEMS[i].sel - 2 < 0 ? 0 : SYSTEMS[i].sel - 2;
        if (r > SYSTEMS[i].n - 4 && SYSTEMS[i].n > 4) r = SYSTEMS[i].n - 4;
        SYSTEMS[i].scroll = SYSTEMS[i].n <= 4 ? 0 : r;
    }
    uint32_t prev = 0, t_repeat = 0, t_notice = 0, t_start = 0, t_prev = plat_ms(), t_fps = t_prev;
    int frames = 0;
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

        feed_audio();
        system_t *s = &SYSTEMS[sel];
        if (diag) {
            if ((bt & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) { diag = 0; play_sfx(SFX_BACK); }
        } else if (!t_launch) {
            if (pressed & (BTN_RIGHT | BTN_R)) { sel = (sel + 1) % NUM_SYSTEMS; play_sfx(SFX_SYSTEM); }
            if (pressed & (BTN_LEFT | BTN_L)) { sel = (sel + NUM_SYSTEMS - 1) % NUM_SYSTEMS; play_sfx(SFX_SYSTEM); }
            /* list wraps: up from the first goes to the last, down from the last goes back to the first */
            if (pressed & BTN_DOWN && s->n) { s->sel = s->sel < s->n - 1 ? s->sel + 1 : 0; play_sfx(SFX_MOVE); }
            if (pressed & BTN_UP && s->n) { s->sel = s->sel > 0 ? s->sel - 1 : s->n - 1; play_sfx(SFX_MOVE); }
            if (pressed & BTN_SELECT) { diag = 1; play_sfx(SFX_OPEN); }
            if (pressed & BTN_Y && s->n) {
                char dir[16];
                const char *file;
                real_game(s, s->sel, dir, sizeof dir, &file);
                char file_copy[600];
                snprintf(file_copy, sizeof file_copy, "%s", file); /* toggle changes the favorites list (file may point into it) */
                char nm[128];
                pretty_name(file_copy, nm, sizeof nm);
                int added = toggle_favorite(dir, file_copy);
                snprintf(notice, sizeof notice, added ? "%s added to favorites" : "%s removed from favorites", nm);
                t_notice = now;
                invalidate_details(); /* the favorites list may have changed order */
                play_sfx(added ? SFX_OPEN : SFX_BACK);
            }
            /* A opens the game only on its own: the right analog stick turns into A/B/X/Y, and on a diagonal sends A+B together */
            if (pressed & BTN_A && !(bt & (BTN_B | BTN_X | BTN_Y)) && s->n) {
                char nm[128], dir[16];
                const char *file;
                real_game(s, s->sel, dir, sizeof dir, &file);
                pretty_name(file, nm, sizeof nm);
                snprintf(notice, sizeof notice, "Starting %s", nm);
                t_notice = now;
                play_sfx(SFX_OPEN);
                set_music_volume(0);
                t_launch = now + 700; /* let the effect play and the music fade before opening the game */
            }
        } else if ((int32_t)(now - t_launch) >= 0) {
            t_launch = 0;
            {
                char dir[16];
                const char *file;
                real_game(s, s->sel, dir, sizeof dir, &file);
                printf("LAUNCH %s %s/%s/%s\n", dir, plat_roms_dir(), dir, file);
                fflush(stdout);
                const char *r = plat_runner(dir);
                if (r) {
                    if (audio_ok) plat_audio_close(); /* the game needs /dev/dsp all to itself */
                    save_state(sel); /* on the stick the menu exits for good to open the game (all memory goes to the game) */
                    char path[600], saves[600];
                    snprintf(path, sizeof path, "%s/%s/%s", plat_roms_dir(), dir, file);
                    snprintf(saves, sizeof saves, "%s/../saves", plat_roms_dir());
                    plat_log("menu: opening game", 0);
                    int rc = plat_launch(r, path, saves);
                    plat_log("menu: game ended, code %d", rc);
                    /* back from the game: reset the timers (START may still be held) */
                    t_notice = 0; prev = BTN_A | BTN_START | BTN_SELECT;
                    t_start = plat_ms() + 2000; t_prev = plat_ms();
                    if (audio_ok) audio_ok = !plat_audio_init(MENU_RATE);
                    set_music_volume(1);
                } else {
                    snprintf(notice, sizeof notice, "Emulator for %s not installed yet", dir);
                    t_notice = now;
                    set_music_volume(1);
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
        /* the bubbles always move: redraw every frame */
        {
            uint32_t td = plat_ms();
            if (diag) draw_diagnostics(&e, (now - t0) / 1000.0f);
            else draw_menu(carousel, &e, notice, aa, (now - t0) / 1000.0f);
            static uint32_t draw_sum, draw_count;
            draw_sum += plat_ms() - td;
            if (++draw_count == 30) { plat_log("ms per frame: draw %d", draw_sum / 30); draw_sum = draw_count = 0; }
            plat_present(screen);
            frames++;
            /* preload the neighboring details (one per frame, only if not in the cache) */
            system_t *a = &SYSTEMS[sel];
            if (a->n > 1 && !diag) {
                find_details(sel, (a->sel + 1) % a->n);
                find_details(sel, (a->sel + a->n - 1) % a->n);
            }
        }
        if (now - t_fps >= 5000) {
            plat_log("fps %d over 5 s", frames * 1000 / (int)(now - t_fps));
            frames = 0; t_fps = now;
        }
    }
    plat_shutdown();
    return 0;
}
