#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "catalog.h"
#include "check.h"
#include "menu_view.h"

#define MEGA_DRIVE_INDEX 3
#define DETAILS_TEXT_SIZE 700
#define NARROW_WIDTH 100
#include "details.h"

#define NES_SYSTEM 1
#define DIR_MODE 0755

void plat_mock_set_roms(const char *dir);

static char root[256], roms[300];

static void write_file(const char *relative_path, const char *text)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s", roms, relative_path);
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    if (file) {
        fputs(text, file);
        fclose(file);
    }
}

static void write_root_file(const char *name, const char *text)
{
    char path[400];
    snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *file = fopen(path, "w");
    fputs(text, file);
    fclose(file);
}

static void setup_tree(void)
{
    char dir[400];
    snprintf(root, sizeof root, "/tmp/retrostick-unit-XXXXXX");
    CHECK(mkdtemp(root) == root);
    snprintf(roms, sizeof roms, "%s/roms", root);
    mkdir(roms, DIR_MODE);
    snprintf(dir, sizeof dir, "%s/nes", roms);
    mkdir(dir, DIR_MODE);
    snprintf(dir, sizeof dir, "%s/nes/.meta", roms);
    mkdir(dir, DIR_MODE);
    write_file("nes/Beta (USA).nes", "x");
    write_file("nes/alpha.nes", "x");
    write_file("nes/notes.txt", "x");
    write_file("nes/.hidden.nes", "x");
    write_file("nes/.meta/alpha.txt", "1990\nAction\nMock Works\nA short text.\n");
    plat_mock_set_roms(roms);
}

static system_t *nes(void)
{
    return catalog_system(NES_SYSTEM);
}

static system_t *favorites(void)
{
    return catalog_system(FAVORITES_SYSTEM);
}

static void test_pretty_name(void)
{
    char out[64];
    catalog_pretty_name("Super Game (USA) [!].nes", out, sizeof out);
    CHECK_STR(out, "Super Game");
    catalog_pretty_name("my_game_two.gba", out, sizeof out);
    CHECK_STR(out, "my game two");
    catalog_pretty_name("Dr. Who.nes", out, sizeof out);
    CHECK_STR(out, "Dr. Who");
    catalog_pretty_name("a very long name indeed.nes", out, 8);
    CHECK_STR(out, "a very");
}

static void test_rom_basename(void)
{
    char out[64];
    details_rom_basename("Dr. Who (USA).nes", out, sizeof out);
    CHECK_STR(out, "Dr. Who (USA)");
    details_rom_basename("noextension", out, sizeof out);
    CHECK_STR(out, "noextension");
}

static void test_has_extension(void)
{
    const char *extensions[] = {".nes", ".zip", NULL};
    CHECK(catalog_has_extension("game.NES", extensions) == 1);
    CHECK(catalog_has_extension("game.zip", extensions) == 1);
    CHECK(catalog_has_extension("game.gba", extensions) == 0);
    CHECK(catalog_has_extension("game", extensions) == 0);
}

static void test_scan_filters_and_sorts(void)
{
    catalog_scan_system(favorites());
    catalog_scan_system(nes());
    CHECK(nes()->game_count == 2);
    CHECK_STR(nes()->games[0], "alpha.nes");
    CHECK_STR(nes()->games[1], "Beta (USA).nes");
}

static void test_scan_survives_a_foreign_file_in_a_system_with_all_extensions(void)
{
    char dir[400];
    snprintf(dir, sizeof dir, "%s/md", roms);
    mkdir(dir, DIR_MODE);
    write_file("md/game.md", "x");
    write_file("md/notes.txt", "x");
    system_t *mega_drive = catalog_system(MEGA_DRIVE_INDEX);
    catalog_scan_system(mega_drive);
    CHECK(mega_drive->game_count == 1);
    CHECK_STR(mega_drive->games[0], "game.md");
}

static void test_a_word_longer_than_the_line_buffer_is_cut(void)
{
    char text[DETAILS_TEXT_SIZE], line[MENU_TEXT_LINE_SIZE];
    memset(text, 'x', sizeof text - 1);
    text[sizeof text - 1] = 0;
    CHECK(menu_view_measure_line(&font_tiny, text, NARROW_WIDTH, line) == MENU_TEXT_LINE_MAX);
}

static void test_favorites_round_trip(void)
{
    CHECK(catalog_is_favorite("nes", "alpha.nes") == 0);
    CHECK(catalog_toggle_favorite("nes", "alpha.nes") == 1);
    CHECK(catalog_is_favorite("nes", "alpha.nes") == 1);
    catalog_scan_system(favorites());
    CHECK(favorites()->game_count == 1);
    CHECK_STR(favorites()->games[0], "nes/alpha.nes");

    char dir[SYSTEM_DIR_SIZE];
    const char *file;
    catalog_game_location(favorites(), 0, dir, sizeof dir, &file);
    CHECK_STR(dir, "nes");
    CHECK_STR(file, "alpha.nes");

    CHECK(catalog_toggle_favorite("nes", "alpha.nes") == 0);
    CHECK(catalog_is_favorite("nes", "alpha.nes") == 0);
    catalog_scan_system(favorites());
    CHECK(favorites()->game_count == 0);
}

static void test_favorite_of_a_removed_rom_is_skipped(void)
{
    write_root_file("favorites.txt", "nes/alpha.nes\nnes/gone.nes\nbad line\n");
    catalog_scan_system(favorites());
    CHECK(favorites()->game_count == 1);
    CHECK_STR(favorites()->games[0], "nes/alpha.nes");
    write_root_file("favorites.txt", "");
    catalog_scan_system(favorites());
}

static void test_state_round_trip(void)
{
    nes()->sel = 1;
    catalog_save_state(NES_SYSTEM);
    nes()->sel = 0;
    CHECK(catalog_load_state() == NES_SYSTEM);
    CHECK(nes()->sel == 1);
}

static void test_state_ignores_bad_values(void)
{
    write_root_file("menu-state.txt", "system 999\nnes 999\n");
    nes()->sel = 0;
    CHECK(catalog_load_state() == NES_SYSTEM);
    CHECK(nes()->sel == 0);
}

static void test_details_are_read_from_meta(void)
{
    const game_details_t *details = details_find(NES_SYSTEM, 0);
    CHECK_STR(details->year, "1990");
    CHECK_STR(details->genre, "Action");
    CHECK_STR(details->dev, "Mock Works");
    CHECK_STR(details->desc, "A short text.");
    CHECK(details->has_cover == 0);
    details = details_find(NES_SYSTEM, 1);
    CHECK_STR(details->year, "");
}

int main(void)
{
    setup_tree();
    test_pretty_name();
    test_rom_basename();
    test_has_extension();
    test_scan_filters_and_sorts();
    test_scan_survives_a_foreign_file_in_a_system_with_all_extensions();
    test_a_word_longer_than_the_line_buffer_is_cut();
    test_favorites_round_trip();
    test_favorite_of_a_removed_rom_is_skipped();
    test_state_round_trip();
    test_state_ignores_bad_values();
    test_details_are_read_from_meta();
    TEST_END();
}
