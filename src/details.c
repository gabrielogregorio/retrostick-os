/* Game details cache and covers, read from .meta/<game>.txt and .meta/<game>.cap. */
#include "details.h"
#include <stdio.h>
#include <string.h>
#include "catalog.h"
#include "plat.h"

/* the current game and its preloaded neighbors: the cover shows up instantly, no flicker */
#define CACHE_SLOTS 4
#define NO_GAME -1
#define BASENAME_SIZE 300
#define META_PATH_SIZE 700

/* cover file: 'CAP1', u16 w, u16 h, 16 RGB565 colors, 4-bit pixels */
#define CAP_MAGIC "CAP1"
#define CAP_MAGIC_SIZE 4
#define CAP_WIDTH_OFFSET 4
#define CAP_HEIGHT_OFFSET 6
#define CAP_PALETTE_OFFSET 8
#define CAP_PALETTE_COLORS 16
#define CAP_HEADER_SIZE (CAP_PALETTE_OFFSET + CAP_PALETTE_COLORS * 2)
#define CAP_PIXEL_BYTES (COVER_W * COVER_H / 2)

static game_details_t cache[CACHE_SLOTS] = {
    {NO_GAME, NO_GAME},
    {NO_GAME, NO_GAME},
    {NO_GAME, NO_GAME},
    {NO_GAME, NO_GAME},
};
static game_details_t *current = &cache[0];
static unsigned slot_used_at[CACHE_SLOTS], use_tick;

const game_details_t *details_current(void)
{
    return current;
}

void details_invalidate(void)
{
    for (int slot = 0; slot < CACHE_SLOTS; slot += 1) {
        cache[slot].system_index = NO_GAME;
        cache[slot].sel = NO_GAME;
    }
}

void details_rom_basename(const char *file, char *out, int max)
{
    snprintf(out, max, "%s", file);
    char *dot = strrchr(out, '.');
    if (dot) {
        *dot = 0;
    }
}

static void read_line(FILE *file, char *out, int max)
{
    out[0] = 0;
    if (!fgets(out, max, file)) {
        return;
    }
    out[strcspn(out, "\r\n")] = 0;
}

static void read_text_fields(game_details_t *details, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    read_line(file, details->year, sizeof details->year);
    read_line(file, details->genre, sizeof details->genre);
    read_line(file, details->dev, sizeof details->dev);
    read_line(file, details->desc, sizeof details->desc);
    fclose(file);
}

static uint32_t read_u16_le(const uint8_t *bytes)
{
    return bytes[0] | bytes[1] << 8;
}

static uint32_t rgb565_to_rgb888(uint32_t color)
{
    uint32_t r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;

    return (r << 19 | (r >> 2) << 16) | (g << 10 | (g >> 4) << 8) | (b << 3 | b >> 2);
}

static int read_cover_data(FILE *file, uint8_t *header, uint8_t *pixels)
{
    return fread(header, 1, CAP_HEADER_SIZE, file) == CAP_HEADER_SIZE && !memcmp(header, CAP_MAGIC, CAP_MAGIC_SIZE) &&
           read_u16_le(header + CAP_WIDTH_OFFSET) == COVER_W && read_u16_le(header + CAP_HEIGHT_OFFSET) == COVER_H &&
           fread(pixels, 1, CAP_PIXEL_BYTES, file) == CAP_PIXEL_BYTES;
}

static void decode_cover(game_details_t *details, const uint8_t *header, const uint8_t *pixels)
{
    uint32_t palette[CAP_PALETTE_COLORS];
    for (int color = 0; color < CAP_PALETTE_COLORS; color += 1) {
        palette[color] = rgb565_to_rgb888(read_u16_le(header + CAP_PALETTE_OFFSET + color * 2));
    }
    for (int byte_index = 0; byte_index < CAP_PIXEL_BYTES; byte_index += 1) {
        details->cover[byte_index * 2] = palette[pixels[byte_index] >> 4];
        details->cover[byte_index * 2 + 1] = palette[pixels[byte_index] & 15];
    }
    details->has_cover = 1;
}

static void read_cover(game_details_t *details, const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return;
    }
    uint8_t header[CAP_HEADER_SIZE], pixels[CAP_PIXEL_BYTES];
    if (read_cover_data(file, header, pixels)) {
        decode_cover(details, header, pixels);
    }
    fclose(file);
}

static void load_into(game_details_t *details, int system_index, int sel)
{
    details->system_index = system_index;
    details->sel = sel;
    details->has_cover = 0;
    details->year[0] = details->genre[0] = details->dev[0] = details->desc[0] = 0;
    const system_t *system = catalog_system(system_index);
    if (sel < 0 || sel >= system->game_count) {
        return;
    }
    char base[BASENAME_SIZE], path[META_PATH_SIZE], dir[SYSTEM_DIR_SIZE];
    const char *file;
    catalog_game_location(system, sel, dir, sizeof dir, &file);
    details_rom_basename(file, base, sizeof base);
    snprintf(path, sizeof path, "%s/%s/.meta/%s.txt", plat_roms_dir(), dir, base);
    read_text_fields(details, path);
    snprintf(path, sizeof path, "%s/%s/.meta/%s.cap", plat_roms_dir(), dir, base);
    read_cover(details, path);
}

static void mark_slot_used(int slot)
{
    use_tick += 1;
    slot_used_at[slot] = use_tick;
}

static int cached_slot(int system_index, int sel)
{
    for (int slot = 0; slot < CACHE_SLOTS; slot += 1) {
        if (cache[slot].system_index == system_index && cache[slot].sel == sel) {
            return slot;
        }
    }

    return -1;
}

/* The current details stay in place: the screen may still be showing them. */
static int least_recently_used_slot(void)
{
    int oldest = -1;
    for (int slot = 0; slot < CACHE_SLOTS; slot += 1) {
        if (&cache[slot] != current && (oldest < 0 || slot_used_at[slot] < slot_used_at[oldest])) {
            oldest = slot;
        }
    }

    return oldest;
}

static game_details_t *find_details(int system_index, int sel)
{
    int slot = cached_slot(system_index, sel);
    if (slot < 0) {
        slot = least_recently_used_slot();
        load_into(&cache[slot], system_index, sel);
    }
    mark_slot_used(slot);

    return &cache[slot];
}

const game_details_t *details_find(int system_index, int sel)
{
    return find_details(system_index, sel);
}

void details_select(int system_index, int sel)
{
    current = find_details(system_index, sel);
}
