/* Exercises the static helpers of the menu, so it includes the source instead of linking it. */
#define main menu_main
#include "../../src/main.c"
#undef main
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

void plat_mock_set_roms(const char *dir);

static char root[256], roms[300];

static void write_file(const char *rel, const char *text)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s", roms, rel);
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void setup_tree(void)
{
    char dir[400];
    snprintf(root, sizeof root, "/tmp/retrostick-unit-XXXXXX");
    CHECK(mkdtemp(root) != NULL);
    snprintf(roms, sizeof roms, "%s/roms", root);
    mkdir(roms, 0755);
    snprintf(dir, sizeof dir, "%s/nes", roms);
    mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/nes/.meta", roms);
    mkdir(dir, 0755);
    write_file("nes/Beta (USA).nes", "x");
    write_file("nes/alpha.nes", "x");
    write_file("nes/notes.txt", "x");
    write_file("nes/.hidden.nes", "x");
    write_file("nes/.meta/alpha.txt", "1990\nAction\nMock Works\nA short text.\n");
    plat_mock_set_roms(roms);
}

static system_t *nes(void) { return &SYSTEMS[1]; }

static void test_pretty_name(void)
{
    char out[64];
    pretty_name("Super Game (USA) [!].nes", out, sizeof out);
    CHECK_STR(out, "Super Game");
    pretty_name("my_game_two.gba", out, sizeof out);
    CHECK_STR(out, "my game two");
    pretty_name("Dr. Who.nes", out, sizeof out);
    CHECK_STR(out, "Dr. Who");
    pretty_name("a very long name indeed.nes", out, 8);
    CHECK(strlen(out) < 8);
}

static void test_rom_basename(void)
{
    char out[64];
    rom_basename("Dr. Who (USA).nes", out, sizeof out);
    CHECK_STR(out, "Dr. Who (USA)");
    rom_basename("noextension", out, sizeof out);
    CHECK_STR(out, "noextension");
}

static void test_has_ext(void)
{
    const char *ext[] = {".nes", ".zip", NULL};
    CHECK(has_ext("game.NES", ext));
    CHECK(has_ext("game.zip", ext));
    CHECK(!has_ext("game.gba", ext));
    CHECK(!has_ext("game", ext));
}

static void test_scan_filters_and_sorts(void)
{
    scan_system(&SYSTEMS[0]);
    scan_system(nes());
    CHECK(nes()->n == 2);
    CHECK_STR(nes()->games[0], "alpha.nes");
    CHECK_STR(nes()->games[1], "Beta (USA).nes");
}

static void test_favorites_round_trip(void)
{
    CHECK(!is_favorite("nes", "alpha.nes"));
    CHECK(toggle_favorite("nes", "alpha.nes") == 1);
    CHECK(is_favorite("nes", "alpha.nes"));
    load_favorites(&SYSTEMS[0]);
    CHECK(SYSTEMS[0].n == 1);
    CHECK_STR(SYSTEMS[0].games[0], "nes/alpha.nes");

    char dir[16];
    const char *file;
    real_game(&SYSTEMS[0], 0, dir, sizeof dir, &file);
    CHECK_STR(dir, "nes");
    CHECK_STR(file, "alpha.nes");

    CHECK(toggle_favorite("nes", "alpha.nes") == 0);
    CHECK(!is_favorite("nes", "alpha.nes"));
    load_favorites(&SYSTEMS[0]);
    CHECK(SYSTEMS[0].n == 0);
}

static void test_favorite_of_a_removed_rom_is_skipped(void)
{
    char path[400];
    snprintf(path, sizeof path, "%s/favorites.txt", root);
    FILE *f = fopen(path, "w");
    fputs("nes/alpha.nes\nnes/gone.nes\nbad line\n", f);
    fclose(f);
    load_favorites(&SYSTEMS[0]);
    CHECK(SYSTEMS[0].n == 1);
    CHECK_STR(SYSTEMS[0].games[0], "nes/alpha.nes");
    snprintf(path, sizeof path, "%s/favorites.txt", root);
    f = fopen(path, "w");
    fclose(f);
    load_favorites(&SYSTEMS[0]);
}

static void test_state_round_trip(void)
{
    nes()->sel = 1;
    save_state(1);
    nes()->sel = 0;
    CHECK(load_state() == 1);
    CHECK(nes()->sel == 1);
}

static void test_state_ignores_bad_values(void)
{
    char path[400];
    snprintf(path, sizeof path, "%s/menu-state.txt", root);
    FILE *f = fopen(path, "w");
    fputs("system 999\nnes 999\n", f);
    fclose(f);
    nes()->sel = 0;
    CHECK(load_state() == 1);
    CHECK(nes()->sel == 0);
}

static void test_details_are_read_from_meta(void)
{
    game_details_t d;
    load_into(&d, 1, 0);
    CHECK_STR(d.year, "1990");
    CHECK_STR(d.genre, "Action");
    CHECK_STR(d.dev, "Mock Works");
    CHECK_STR(d.desc, "A short text.");
    CHECK(!d.has_cover);
    load_into(&d, 1, 1);
    CHECK_STR(d.year, "");
}

int main(void)
{
    setup_tree();
    test_pretty_name();
    test_rom_basename();
    test_has_ext();
    test_scan_filters_and_sorts();
    test_favorites_round_trip();
    test_favorite_of_a_removed_rom_is_skipped();
    test_state_round_trip();
    test_state_ignores_bad_values();
    test_details_are_read_from_meta();
    TEST_END();
}
