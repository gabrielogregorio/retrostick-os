/* Runner saves in the saves folder: <game>.srm (battery), .st1 and .st2 (slots), .sta (automatic on
 * quit) and .last (which state was written last, because the stick has no clock). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "plat.h"
#include "runner_internal.h"

#define SAVE_PATH_MAX 600

static const char *rom_path;
static const char *saves_folder;

void runner_saves_init(const char *rom, const char *saves_dir)
{
    rom_path = rom;
    saves_folder = saves_dir;
}

const char *runner_saves_dir(void)
{
    return saves_folder;
}

static void sram_path(char *path)
{
    const char *slash = strrchr(rom_path, '/');
    const char *rom_name = slash ? slash + 1 : rom_path;
    snprintf(path, SAVE_PATH_MAX, "%s/%s", saves_folder, rom_name);
    char *dot = strrchr(path, '.');
    if (dot && dot > path + strlen(saves_folder)) {
        strcpy(dot, ".srm");
    } else {
        strncat(path, ".srm", SAVE_PATH_MAX - strlen(path) - 1);
    }
}

static void state_path(char *path, const char *extension)
{
    sram_path(path);
    char *dot = strrchr(path, '.');
    if (dot) {
        strcpy(dot, extension);
    }
}

static void slot_path(char *path, char slot)
{
    char extension[] = {'.', 's', 't', slot, '\0'};
    state_path(path, extension);
}

static void transfer_sram(bool writing)
{
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void *memory = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!size || !memory) {
        return;
    }
    char path[SAVE_PATH_MAX];
    sram_path(path);
    FILE *file = fopen(path, writing ? "wb" : "rb");
    if (!file) {
        return;
    }
    if (writing) {
        fwrite(memory, 1, size, file);
    } else if (fread(memory, 1, size, file) != size) {
        fprintf(stderr, "incomplete save: %s\n", path);
    }
    fclose(file);
}

void runner_sram_load(void)
{
    transfer_sram(false);
}

void runner_sram_save(void)
{
    transfer_sram(true);
}

static void sync_and_close(FILE *file)
{
    fflush(file);
    fsync(fileno(file));
    fclose(file);
}

static bool write_state_file(char slot, const void *state, size_t size)
{
    char path[SAVE_PATH_MAX];
    slot_path(path, slot);
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    bool written = fwrite(state, 1, size, file) == size;
    sync_and_close(file);

    return written;
}

static void record_last_slot(char slot)
{
    char path[SAVE_PATH_MAX];
    state_path(path, ".last");
    FILE *file = fopen(path, "w");
    if (!file) {
        return;
    }
    fputc(slot, file);
    sync_and_close(file);
}

int runner_save_state(char slot)
{
    size_t size = retro_serialize_size();
    void *state = size ? malloc(size) : NULL;
    if (!state || !retro_serialize(state, size)) {
        free(state);

        return -1;
    }
    bool written = write_state_file(slot, state, size);
    free(state);
    if (!written) {
        return -1;
    }
    record_last_slot(slot);
    plat_log("pause: state saved, slot %d", slot);

    return 0;
}

int runner_load_state(char slot)
{
    char path[SAVE_PATH_MAX];
    slot_path(path, slot);
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    void *state = size > 0 ? malloc(size) : NULL;
    int loaded = state && fread(state, 1, size, file) == (size_t)size && retro_unserialize(state, size);
    fclose(file);
    free(state);
    plat_log("pause: state loaded %d", loaded);

    return loaded ? 0 : -1;
}

bool runner_slot_exists(char slot)
{
    char path[SAVE_PATH_MAX];
    slot_path(path, slot);
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    fclose(file);

    return true;
}

char runner_last_slot(void)
{
    char path[SAVE_PATH_MAX];
    state_path(path, ".last");
    FILE *file = fopen(path, "r");
    if (!file) {
        return SLOT_NONE;
    }
    int slot = fgetc(file);
    fclose(file);
    if (slot == SLOT_1 || slot == SLOT_2 || slot == SLOT_AUTO) {
        return (char)slot;
    }

    return SLOT_NONE;
}
