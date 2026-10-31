/* Platform stub for unit tests: nothing on screen, nothing on hardware. */
#include <stdio.h>
#include <stdlib.h>
#include "plat.h"

#define ROMS_DIR_SIZE 512
#define AUDIO_INIT_FAILED -1
#define QUEUE_UNKNOWN -1

static char roms_dir[ROMS_DIR_SIZE] = ".";

void plat_mock_set_roms(const char *dir)
{
    snprintf(roms_dir, sizeof roms_dir, "%s", dir);
}

const char *plat_roms_dir(void)
{
    return roms_dir;
}

int plat_init(void)
{
    return 0;
}

void plat_shutdown(void)
{
}

void plat_present(const uint32_t *screen)
{
}

void plat_input(input_t *input)
{
}

uint32_t plat_ms(void)
{
    return 0;
}

void plat_sleep(int ms)
{
}

void plat_log(const char *fmt, int value)
{
}

void plat_diagnostics(void)
{
}

int plat_audio_init(int rate)
{
    return AUDIO_INIT_FAILED;
}

int plat_audio_rate(void)
{
    return 0;
}

void plat_audio_close(void)
{
}

int plat_audio(const int16_t *samples, int frames)
{
    return frames;
}

int plat_audio_queue(void)
{
    return QUEUE_UNKNOWN;
}

int plat_launch(const char *prog, const char *game, const char *saves)
{
    return 0;
}

const char *plat_runner(const char *system_name)
{
    return NULL;
}
