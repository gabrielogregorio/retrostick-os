/* Systems table, game lists, favorites, display names and the saved menu position. */
#include "catalog.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "plat.h"

#define MAX_GAMES 1000
#define NES_SYSTEM 1
#define PATH_SIZE 600
#define ROMS_DIR_PATH_SIZE 512
#define ROM_PATH_SIZE 1300 /* roms folder plus one whole favorites line */
#define FAVORITE_KEY_SIZE 640
#define STATE_NAME_SIZE 32
#define STATE_ENTRY_FORMAT "%31s %d" /* the width is STATE_NAME_SIZE - 1 */

static system_t systems[] = {
    {"fav", "Favorites", "FAV", 0xe8a020, {NULL}},
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
#define SYSTEM_COUNT ((int)(sizeof systems / sizeof systems[0]))

static int favorites_version;

int catalog_system_count(void)
{
    return SYSTEM_COUNT;
}

system_t *catalog_system(int system_index)
{
    return &systems[system_index];
}

int catalog_is_favorites(const system_t *system)
{
    return !strcmp(system->dir, "fav");
}

const char *catalog_short_name(const char *dir)
{
    for (int system_index = FAVORITES_SYSTEM + 1; system_index < SYSTEM_COUNT; system_index += 1) {
        if (!strcmp(systems[system_index].dir, dir)) {
            return systems[system_index].short_name;
        }
    }

    return dir;
}

void catalog_game_location(const system_t *system, int game_index, char *dir, int dir_size, const char **file)
{
    const char *entry = system->games[game_index];
    const char *slash = strchr(entry, '/');
    if (catalog_is_favorites(system) && slash) {
        snprintf(dir, dir_size, "%.*s", (int)(slash - entry), entry);
        *file = slash + 1;

        return;
    }
    snprintf(dir, dir_size, "%s", system->dir);
    *file = entry;
}

static void card_file_path(char *out, int max, const char *name)
{
    snprintf(out, max, "%s/../%s", plat_roms_dir(), name);
}

int catalog_has_extension(const char *name, const char *const *extensions)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return 0;
    }
    for (; *extensions; extensions += 1) {
        if (!strcasecmp(dot, *extensions)) {
            return 1;
        }
    }

    return 0;
}

static int compare_names(const void *left, const void *right)
{
    return strcasecmp(*(char *const *)left, *(char *const *)right);
}

static void sort_games(system_t *system)
{
    qsort(system->games, system->game_count, sizeof(char *), compare_names);
}

static int rom_exists(const char *entry)
{
    char rom[ROM_PATH_SIZE];
    snprintf(rom, sizeof rom, "%s/%s", plat_roms_dir(), entry);
    FILE *file = fopen(rom, "rb");
    if (!file) {
        return 0;
    }
    fclose(file);

    return 1;
}

/* retro/favorites.txt holds one "system/file" line per game; only what is still on the SD card is listed. */
static void load_favorites(system_t *system)
{
    if (!system->games) {
        system->games = calloc(MAX_GAMES, sizeof(char *));
    }
    for (int game_index = 0; game_index < system->game_count; game_index += 1) {
        free(system->games[game_index]);
    }
    system->game_count = 0;

    char path[PATH_SIZE], line[PATH_SIZE];
    card_file_path(path, sizeof path, "favorites.txt");
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    while (system->game_count < MAX_GAMES && fgets(line, sizeof line, file)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strchr(line, '/') || !rom_exists(line)) {
            continue;
        }
        system->games[system->game_count] = strdup(line);
        system->game_count += 1;
    }
    fclose(file);
    sort_games(system);
}

static int find_favorite(const char *key)
{
    const system_t *favorites = &systems[FAVORITES_SYSTEM];
    for (int game_index = favorites->game_count - 1; game_index >= 0; game_index -= 1) {
        if (!strcmp(favorites->games[game_index], key)) {
            return game_index;
        }
    }

    return -1;
}

int catalog_is_favorite(const char *dir, const char *file)
{
    char key[FAVORITE_KEY_SIZE];
    snprintf(key, sizeof key, "%s/%s", dir, file);

    return find_favorite(key) >= 0;
}

static void remove_favorite(system_t *favorites, int game_index)
{
    int following = favorites->game_count - game_index - 1;
    free(favorites->games[game_index]);
    memmove(&favorites->games[game_index], &favorites->games[game_index + 1], following * sizeof(char *));
    favorites->game_count -= 1;
}

static void add_favorite(system_t *favorites, const char *key)
{
    if (favorites->game_count >= MAX_GAMES) {
        return;
    }
    favorites->games[favorites->game_count] = strdup(key);
    favorites->game_count += 1;
    sort_games(favorites);
}

static void write_favorites(const system_t *favorites)
{
    char path[PATH_SIZE];
    card_file_path(path, sizeof path, "favorites.txt");
    FILE *file = fopen(path, "w");
    if (!file) {
        return;
    }
    for (int game_index = 0; game_index < favorites->game_count; game_index += 1) {
        fprintf(file, "%s\n", favorites->games[game_index]);
    }
    fclose(file);
}

int catalog_toggle_favorite(const char *dir, const char *file)
{
    char key[FAVORITE_KEY_SIZE];
    snprintf(key, sizeof key, "%s/%s", dir, file);
    system_t *favorites = &systems[FAVORITES_SYSTEM];
    int found = find_favorite(key);
    if (found >= 0) {
        remove_favorite(favorites, found);
    } else {
        add_favorite(favorites, key);
    }
    write_favorites(favorites);
    if (favorites->sel >= favorites->game_count) {
        favorites->sel = favorites->game_count ? favorites->game_count - 1 : 0;
    }
    favorites_version += 1;

    return found < 0;
}

void catalog_scan_system(system_t *system)
{
    if (catalog_is_favorites(system)) {
        load_favorites(system);

        return;
    }
    char path[ROMS_DIR_PATH_SIZE];
    snprintf(path, sizeof path, "%s/%s", plat_roms_dir(), system->dir);
    DIR *folder = opendir(path);
    system->games = calloc(MAX_GAMES, sizeof(char *));
    if (!folder) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(folder)) && system->game_count < MAX_GAMES) {
        if (entry->d_name[0] != '.' && catalog_has_extension(entry->d_name, system->extensions)) {
            system->games[system->game_count] = strdup(entry->d_name);
            system->game_count += 1;
        }
    }
    closedir(folder);
    sort_games(system);
}

static int ends_the_name(const char *file, int position)
{
    char letter = file[position];

    return letter == '(' || letter == '[' || (letter == '.' && !strchr(file + position + 1, '.'));
}

void catalog_pretty_name(const char *file, char *out, int max)
{
    int length = 0;
    for (int position = 0; file[position] && length < max - 1; position += 1) {
        if (ends_the_name(file, position)) {
            break;
        }
        out[length] = file[position] == '_' ? ' ' : file[position];
        length += 1;
    }
    while (length && out[length - 1] == ' ') {
        length -= 1;
    }
    out[length] = 0;
}

int catalog_favorites_version(void)
{
    return favorites_version;
}

void catalog_save_state(int system_index)
{
    char path[PATH_SIZE];
    card_file_path(path, sizeof path, "menu-state.txt");
    FILE *file = fopen(path, "w");
    if (!file) {
        return;
    }
    fprintf(file, "system %d\n", system_index);
    for (int index = 0; index < SYSTEM_COUNT; index += 1) {
        fprintf(file, "%s %d\n", systems[index].dir, systems[index].sel);
    }
    fclose(file);
}

static void restore_system_sel(const char *name, int sel)
{
    for (int index = 0; index < SYSTEM_COUNT; index += 1) {
        if (!strcmp(systems[index].dir, name) && sel >= 0 && sel < systems[index].game_count) {
            systems[index].sel = sel;
        }
    }
}

int catalog_load_state(void)
{
    char path[PATH_SIZE], name[STATE_NAME_SIZE];
    int chosen = NES_SYSTEM, value;
    card_file_path(path, sizeof path, "menu-state.txt");
    FILE *file = fopen(path, "r");
    if (!file) {
        return chosen;
    }
    if (fscanf(file, "system %d", &value) == 1 && value >= 0 && value < SYSTEM_COUNT) {
        chosen = value;
    }
    while (fscanf(file, STATE_ENTRY_FORMAT, name, &value) == 2) {
        restore_system_sel(name, value);
    }
    fclose(file);

    return chosen;
}
