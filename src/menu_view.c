/* Menu drawing: carousel, game panel, footer, notices and the controller diagnostics screen. */
#include "menu_view.h"
#include <stdio.h>
#include <string.h>
#include "aero.h"
#include "catalog.h"
#include "details.h"
#include "gfx.h"
#include "icons.h"
#include "menu_audio.h"

#define OPAQUE 255
#define PLAYERS 2
#define COLOR_WHITE 0xffffff
#define COLOR_TEXT 0x163a5c
#define COLOR_TEXT_MUTED 0x4a6f8f
#define COLOR_HINT_NEUTRAL 0x7f9bb3

#define PANEL_X 56
#define PANEL_Y 210
#define PANEL_W (SCREEN_W - 112)
#define PANEL_H 112
#define ROW_H 26
#define ROWS_DRAWN 6
#define LIST_W 288
#define LIST_TEXT_X (PANEL_X + 20)
#define COVER_X (PANEL_X + LIST_W + 18)
#define COVER_Y (PANEL_Y + (PANEL_H - COVER_H) / 2)
#define INFO_X (COVER_X + COVER_W + 12)
#define INFO_W (PANEL_X + PANEL_W - 12 - INFO_X)
#define INFO_LINE_STEP 16
#define LINE_TIGHTENING 2

#define CAROUSEL_REACH 3
#define CAROUSEL_WRAP_TURNS 100 /* keeps the rounded index positive */
#define CAROUSEL_MIN_SCALE 0.3f
#define CAROUSEL_MIN_ALPHA 20

#define DIAG_BUTTONS 12
#define DIAG_ROW_X 40
#define DIAG_PANEL_STEP 120
#define DIAG_ROW_STEP 32
#define DIAG_PILL_GAP 6

#define HINT_MIN_W 22
#define HINT_LABEL_GAP 6
#define HINT_NEXT_GAP 18

/* Compared whole with memcmp, so it holds only ints and has no padding. */
typedef struct {
    int system_index, sel, scroll_hundredths, player_present[PLAYERS], details_sel, favorites_version;
} static_layer_key_t;

static const char *const BUTTON_NAMES[DIAG_BUTTONS] = {"Up", "Down", "Left", "Right", "A",      "B",
                                                       "X",  "Y",    "L",    "R",     "Select", "Start"};

static uint32_t screen[SCREEN_W * SCREEN_H];
static uint32_t sky[SCREEN_W * SCREEN_H]; /* Aero background, built once at startup (expensive) */

/* Sky + system tint + controllers + name + panel (list, cover, details) + footer.
 * Rebuilt only when something in it changes; each frame adds just the bubbles and the carousel. */
static uint32_t static_layer[SCREEN_W * SCREEN_H];
static static_layer_key_t static_layer_key = {-1};

/* prebuilt background (generated on PC by tools/gen_audio.c); too slow to compute on the stick */
static void load_sky(void)
{
    FILE *file = menu_audio_open_asset("aero-background.bin");
    if (!file || fread(sky, 1, sizeof sky, file) != sizeof sky) {
        aero_background(sky, SCREEN_W, SCREEN_H, 0);
    }
    if (file) {
        fclose(file);
    }
}

void menu_view_init(void)
{
    gfx_target(screen, SCREEN_W, SCREEN_H);
    aero_init(SCREEN_W, SCREEN_H);
    for (int system_index = 0; system_index < catalog_system_count() && system_index < ICONS_MAX; system_index += 1) {
        const system_t *system = catalog_system(system_index);
        icons_build(system_index, system->dir, system->color);
    }
    gfx_target(screen, SCREEN_W, SCREEN_H);
    uint32_t started_at = plat_ms();
    load_sky();
    plat_log("menu: aero background built in %d ms", (int)(plat_ms() - started_at));
}

void menu_view_present(void)
{
    plat_present(screen);
}

static void draw_player_status(int player, const input_t *input, int y)
{
    char line[96];
    int present = input->present[player];
    snprintf(line, sizeof line, "Player %d  %s", player + 1, present ? "connected" : "no signal");
    gfx_text(&font_small, DIAG_ROW_X, y + 22, line, present ? 0x1a8a2a : 0xc03838, OPAQUE);
    const uint8_t *raw = input->raw[player];
    snprintf(line, sizeof line, "raw: %02X %02X %02X %02X %02X %02X %02X %02X %02X", raw[0], raw[1], raw[2], raw[3],
             raw[4], raw[5], raw[6], raw[7], raw[8]);
    gfx_text(&font_small, 300, y + 22, line, 0x24486a, OPAQUE);
}

static void draw_button_pills(uint32_t buttons, int y)
{
    int x = DIAG_ROW_X;
    for (int button_bit = 0; button_bit < DIAG_BUTTONS; button_bit += 1) {
        const char *name = BUTTON_NAMES[button_bit];
        int pressed = buttons & (1u << button_bit);
        int w = gfx_width(&font_small, name) + 16;
        aero_pill(x, y + 44, w, 24, 12, pressed ? 0xffc030 : 0x8fb4cf, OPAQUE);
        gfx_text(&font_small, x + 8, y + 61, name, pressed ? 0x3a2400 : 0x0e2a44, OPAQUE);
        x += w + DIAG_PILL_GAP;
        if (x > SCREEN_W - 90 && button_bit < DIAG_BUTTONS - 1) {
            x = DIAG_ROW_X;
            y += DIAG_ROW_STEP;
        }
    }
}

void menu_view_draw_diagnostics(const input_t *input, float seconds)
{
    memcpy(screen, sky, sizeof screen);
    aero_bubbles(screen, seconds);
    aero_text(&font_big, 32, 50, "Controller diagnostics", COLOR_WHITE, OPAQUE);
    aero_text(&font_small, 32, 76, "Press buttons to see what arrives. SELECT + START goes back.", COLOR_WHITE, OPAQUE);
    for (int player = 0; player < PLAYERS; player += 1) {
        int y = 100 + player * DIAG_PANEL_STEP;
        aero_glass(24, y, SCREEN_W - 48, 108, 14, 170);
        draw_player_status(player, input, y);
        draw_button_pills(input->bt[player], y);
    }
}

/* do not cut in the middle of a UTF-8 character */
static int previous_character(const char *text, int position)
{
    position -= 1;
    while (position > 0 && (text[position] & 0xc0) == 0x80) {
        position -= 1;
    }

    return position;
}

/* Trims the text with "..." to fit in max_width pixels. */
static const char *fit_text(const font_t *font, const char *text, int max_width)
{
    static char fitted[MENU_TEXT_LINE_SIZE];
    if (gfx_width(font, text) <= max_width) {
        return text;
    }
    int length = snprintf(fitted, sizeof fitted - MENU_ELLIPSIS_ROOM, "%s", text);
    while (length > 0) {
        length = previous_character(fitted, length);
        strcpy(fitted + length, "...");
        if (gfx_width(font, fitted) <= max_width) {
            break;
        }
    }

    return fitted;
}

/* Length of the start of text that fits in width pixels, ending on a word boundary. */
int menu_view_measure_line(const font_t *font, const char *text, int width, char *line)
{
    int word_start = 0, cut = 0;
    for (;;) {
        int word_end = word_start;
        while (text[word_end] && text[word_end] != ' ') {
            word_end += 1;
        }
        if (word_end >= MENU_TEXT_LINE_SIZE - MENU_ELLIPSIS_ROOM) {
            break;
        }
        memcpy(line, text, word_end);
        line[word_end] = 0;
        if (gfx_width(font, line) > width && word_start) {
            break;
        }
        word_start = word_end;
        cut = word_end;
        if (!text[word_end]) {
            break;
        }
        word_start += 1;
    }
    if (!cut) {
        cut = word_start ? word_start : (int)strlen(text);
    }
    if (cut > MENU_TEXT_LINE_MAX) {
        cut = MENU_TEXT_LINE_MAX;
    }

    return cut;
}

/* Wraps the text into at most max_lines lines of width w; the last one gets "..." if text is left over. */
static void wrapped_text(const font_t *font, int x, int y, int w, int max_lines, const char *text, uint32_t color)
{
    char line[MENU_TEXT_LINE_SIZE];
    for (int line_index = 0; line_index < max_lines && *text; line_index += 1) {
        while (*text == ' ') {
            text += 1;
        }
        int cut = menu_view_measure_line(font, text, w, line);
        memcpy(line, text, cut);
        line[cut] = 0;
        text += cut;
        if (line_index == max_lines - 1 && *text) {
            gfx_text(font, x, y, fit_text(font, strcat(line, " ..."), w), color, OPAQUE);
        } else {
            gfx_text(font, x, y, line, color, OPAQUE);
        }
        y += font->line - LINE_TIGHTENING;
    }
}

/* Round footer button + caption; returns where the next one starts. */
static int hint(int x, const char *button, uint32_t color, const char *caption)
{
    int w = gfx_width(&font_small, button);
    int button_w = w + 14 < HINT_MIN_W ? HINT_MIN_W : w + 14;
    aero_pill(x, 330, button_w, 22, 11, color, OPAQUE);
    gfx_text(&font_small, x + (button_w - w) / 2, 346, button, COLOR_WHITE, OPAQUE);
    aero_text(&font_small, x + button_w + HINT_LABEL_GAP, 346, caption, COLOR_WHITE, OPAQUE);

    return x + button_w + HINT_LABEL_GAP + gfx_width(&font_small, caption) + HINT_NEXT_GAP;
}

static void draw_footer(const system_t *system)
{
    int x = 24;
    x = hint(x, "A", 0x2fbf3f, "Play");
    x = hint(x, "Y", 0xe8a020, catalog_is_favorites(system) ? "Remove" : "Favorite");
    x = hint(x, "L R", COLOR_HINT_NEUTRAL, "System");
    x = hint(x, "SELECT", COLOR_HINT_NEUTRAL, "Controls");
    hint(x, "START", 0xe0503c, "quit");
}

static void draw_controllers(const input_t *input)
{
    for (int player = 0; player < PLAYERS; player += 1) {
        int cx = SCREEN_W - 70 + player * 34;
        aero_orb(cx, 28, 13, input->present[player] ? 0x3cc84a : 0x9aaabb, OPAQUE);
        char label[4] = {'P', '1' + player, 0};
        gfx_text(&font_small, cx - gfx_width(&font_small, label) / 2, 33, label, COLOR_WHITE, OPAQUE);
    }
}

static void draw_system_title(const system_t *system)
{
    char text[160];
    snprintf(text, sizeof text, "%s", system->name);
    int w = gfx_width(&font_big, text);
    aero_text(&font_big, SCREEN_W / 2 - w / 2, 186, text, COLOR_WHITE, OPAQUE);
    snprintf(text, sizeof text, system->game_count == 1 ? "1 game" : "%d games", system->game_count);
    w = gfx_width(&font_small, text);
    aero_text(&font_small, SCREEN_W / 2 - w / 2, 202, text, COLOR_WHITE, 230);
}

static void draw_empty_message(const system_t *system)
{
    char message[96];
    if (catalog_is_favorites(system)) {
        snprintf(message, sizeof message, "No favorites yet: press Y on a game.");
    } else {
        snprintf(message, sizeof message, "No games. Copy them to roms/%s on the SD card.", system->dir);
    }
    int w = gfx_width(&font_small, message);
    gfx_text(&font_small, SCREEN_W / 2 - w / 2, PANEL_Y + PANEL_H / 2 + 5, message, 0x3a5a78, OPAQUE);
}

/* On the right of a row: in favorites, the system tag; in systems, the star if it is a favorite.
 * Returns the width taken from the title. */
static int draw_row_mark(const system_t *system, const char *dir, const char *file, int y, int selected)
{
    if (catalog_is_favorites(system)) {
        const char *tag = catalog_short_name(dir);
        int w = gfx_width(&font_tiny, tag) + 10;
        gfx_text(&font_tiny, PANEL_X + 8 + LIST_W - w, y + 17, tag, selected ? 0xe8f6ff : COLOR_TEXT_MUTED, OPAQUE);

        return w;
    }
    if (catalog_is_favorite(dir, file)) {
        gfx_star(PANEL_X + LIST_W - 6, y + 12, 8, 0xfff6b0, 0xe89a10, OPAQUE);

        return 22;
    }

    return 0;
}

static void draw_game_row(const system_t *system, int game_index, int y)
{
    int selected = game_index == system->sel;
    if (selected) {
        aero_pill(PANEL_X + 8, y, LIST_W, ROW_H - 2, 12, 0x1ea8f0, OPAQUE);
    }
    char title[128], dir[SYSTEM_DIR_SIZE];
    const char *file;
    catalog_game_location(system, game_index, dir, sizeof dir, &file);
    catalog_pretty_name(file, title, sizeof title);
    int mark_w = draw_row_mark(system, dir, file, y, selected);
    const char *text = fit_text(&font_small, title, LIST_W - 24 - mark_w);
    if (selected) {
        aero_text(&font_small, LIST_TEXT_X, y + 18, text, COLOR_WHITE, OPAQUE);

        return;
    }
    gfx_text(&font_small, LIST_TEXT_X, y + 18, text, COLOR_TEXT, OPAQUE);
}

static void draw_game_list(const system_t *system)
{
    float top = system->scroll;
    int first = (int)top;
    /* rows entering and leaving at the edge appear clipped, so they do not vanish and "flicker" while scrolling */
    gfx_clip_y(PANEL_Y + 3, PANEL_Y + PANEL_H - 3);
    for (int game_index = first; game_index < system->game_count && game_index < first + ROWS_DRAWN; game_index += 1) {
        int y = PANEL_Y + 8 + (int)((game_index - top) * ROW_H);
        if (y < PANEL_Y - ROW_H || y > PANEL_Y + PANEL_H) {
            continue;
        }
        draw_game_row(system, game_index, y);
    }
    gfx_clip_y(0, SCREEN_H);
}

static void draw_cover(const system_t *system, const game_details_t *details)
{
    gfx_round_rect(COVER_X - 2, COVER_Y - 2, COVER_W + 4, COVER_H + 4, 4, COLOR_WHITE, 230);
    if (details->has_cover) {
        for (int y = 0; y < COVER_H; y += 1) {
            memcpy(&static_layer[(COVER_Y + y) * SCREEN_W + COVER_X], &details->cover[y * COVER_W],
                   COVER_W * sizeof(uint32_t));
        }
    } else {
        aero_pill(COVER_X, COVER_Y, COVER_W, COVER_H, 4, system->color, OPAQUE);
        int w = gfx_width(&font_big, system->short_name);
        aero_text(&font_big, COVER_X + (COVER_W - w) / 2, COVER_Y + COVER_H / 2 + 9, system->short_name, COLOR_WHITE,
                  OPAQUE);
    }
    gfx_round_rect_grad(COVER_X, COVER_Y, COVER_W, COVER_H * 2 / 5, 3, COLOR_WHITE, COLOR_WHITE, 40);
}

static void draw_game_info(const game_details_t *details)
{
    int y = PANEL_Y + 20;
    if (details->genre[0]) {
        gfx_text(&font_small, INFO_X, y, fit_text(&font_small, details->genre, INFO_W), 0x0e5a96, OPAQUE);
        y += INFO_LINE_STEP;
    }
    char credits[120];
    snprintf(credits, sizeof credits, "%s%s%s", details->year, details->year[0] && details->dev[0] ? "  ·  " : "",
             details->dev);
    if (credits[0]) {
        gfx_text(&font_tiny, INFO_X, y, fit_text(&font_tiny, credits, INFO_W), COLOR_TEXT_MUTED, OPAQUE);
        y += INFO_LINE_STEP;
    }
    if (details->desc[0]) {
        int max_lines = (PANEL_Y + PANEL_H - 6 - y) / (font_tiny.line - LINE_TIGHTENING) + 1;
        wrapped_text(&font_tiny, INFO_X, y, INFO_W, max_lines, details->desc, COLOR_TEXT);
    }
}

/* Game list on a glass panel: list on the left, cover and details on the right. */
static void draw_game_panel(const system_t *system, int system_index)
{
    aero_glass(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 16, 150);
    if (!system->game_count) {
        draw_empty_message(system);

        return;
    }
    draw_game_list(system);
    details_select(system_index, system->sel);
    const game_details_t *details = details_current();
    draw_cover(system, details);
    draw_game_info(details);
}

static void rebuild_static_layer(const system_t *system, int system_index, const input_t *input)
{
    memcpy(static_layer, sky, sizeof static_layer);
    gfx_target(static_layer, SCREEN_W, SCREEN_H);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, system->color, 22);
    draw_controllers(input);
    draw_system_title(system);
    draw_game_panel(system, system_index);
    draw_footer(system);
    gfx_target(screen, SCREEN_W, SCREEN_H);
}

static void refresh_static_layer(int system_index, const input_t *input)
{
    const system_t *system = catalog_system(system_index);
    static_layer_key_t key = {
        system_index,
        system->sel,
        (int)(system->scroll * 100),
        {input->present[0], input->present[1]},
        details_current()->sel,
        catalog_favorites_version(),
    };
    if (!memcmp(&key, &static_layer_key, sizeof key)) {
        return;
    }
    static_layer_key = key;
    rebuild_static_layer(system, system_index, input);
    static_layer_key.details_sel = details_current()->sel; /* the details may have just been loaded */
}

static float icon_scale(float distance)
{
    if (distance < 1) {
        return 1.0f - distance * 0.32f;
    }

    return 0.68f - (distance - 1) * 0.2f;
}

static int icon_alpha(float distance)
{
    if (distance < 1) {
        return 255 - (int)(distance * 60);
    }

    return 195 - (int)((distance - 1) * 90);
}

static float icon_spacing(float distance)
{
    if (distance < 1) {
        return 130;
    }

    return 130 + (distance - 1) * -18;
}

/* Glass bubbles with the console inside, floating. */
static void draw_carousel(float carousel, float seconds)
{
    int system_count = catalog_system_count();
    for (int offset = -CAROUSEL_REACH; offset <= CAROUSEL_REACH; offset += 1) {
        int slot = (int)carousel + offset;
        float signed_distance = slot - carousel;
        int icon = ((slot % system_count) + system_count) % system_count;
        float distance = signed_distance < 0 ? -signed_distance : signed_distance;
        float scale = icon_scale(distance);
        if (scale < CAROUSEL_MIN_SCALE) {
            continue;
        }
        int alpha = icon_alpha(distance);
        if (alpha < CAROUSEL_MIN_ALPHA) {
            continue;
        }
        int cx = SCREEN_W / 2 + (int)(signed_distance * icon_spacing(distance));
        int cy = 100 + (int)(distance * 6) + (int)(__builtin_sinf(seconds * 1.3f + icon * 1.7f) * 3);
        icons_draw(screen, SCREEN_W, SCREEN_H, icon, cx, cy, scale, alpha);
    }
}

static void draw_notice(const char *notice, int alpha)
{
    int w = gfx_width(&font_small, notice) + 48;
    aero_glass(SCREEN_W / 2 - w / 2, 146, w, 44, 18, alpha * 235 / OPAQUE);
    gfx_text(&font_small, SCREEN_W / 2 - w / 2 + 24, 173, notice, COLOR_TEXT, alpha);
}

void menu_view_draw_menu(float carousel, const input_t *input, const char *notice, int notice_alpha, float seconds)
{
    int system_count = catalog_system_count();
    int system_index = (int)(carousel + 0.5f + system_count * CAROUSEL_WRAP_TURNS) % system_count;
    refresh_static_layer(system_index, input);
    memcpy(screen, static_layer, sizeof screen);
    aero_bubbles(screen, seconds);
    draw_carousel(carousel, seconds);
    if (notice_alpha > 0) {
        draw_notice(notice, notice_alpha);
    }
}
