/* Systems table, game lists, favorites, display names and the saved menu position. */
#pragma once
#include <stdint.h>

#define FAVORITES_SYSTEM 0 /* pseudo-system: games marked with Y, from any system */
#define SYSTEM_DIR_SIZE 16 /* longest roms/<system> folder name, with its terminator */
#define SYSTEM_EXTENSIONS_MAX 4

typedef struct {
    const char *dir, *name, *short_name;
    uint32_t color;
    const char *extensions[SYSTEM_EXTENSIONS_MAX + 1]; /* NULL-terminated */
    char **games;
    int game_count, sel;
    float scroll;
} system_t;

int catalog_system_count(void);
system_t *catalog_system(int system_index);
int catalog_is_favorites(const system_t *system);
const char *catalog_short_name(const char *dir); /* tag of a system folder; the folder itself if unknown */

void catalog_scan_system(system_t *system);
int catalog_has_extension(const char *name, const char *const *extensions); /* extensions ends in NULL */

/* Real folder and file of a game (in favorites the entry holds "system/file"). */
void catalog_game_location(const system_t *system, int game_index, char *dir, int dir_size, const char **file);
void catalog_pretty_name(const char *file, char *out, int max); /* no extension and no "(USA)" etc. */

int catalog_is_favorite(const char *dir, const char *file);
int catalog_toggle_favorite(const char *dir, const char *file); /* returns 1 if it ended up a favorite */
int catalog_favorites_version(void);                            /* changes on every edit */

/* The menu exits to open the game and comes back through these. */
void catalog_save_state(int system_index);
int catalog_load_state(void); /* returns the chosen system */
