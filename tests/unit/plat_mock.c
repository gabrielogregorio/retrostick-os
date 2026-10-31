/* Platform stub for unit tests: nothing on screen, nothing on hardware. */
#include <stdio.h>
#include <stdlib.h>
#include "plat.h"

static char roms_dir[512] = ".";
void plat_mock_set_roms(const char *dir) { snprintf(roms_dir, sizeof roms_dir, "%s", dir); }
const char *plat_roms_dir(void) { return roms_dir; }

int plat_init(void) { return 0; }
void plat_shutdown(void) {}
void plat_present(const uint32_t *t) { (void)t; }
void plat_input(input_t *e) { (void)e; }
uint32_t plat_ms(void) { return 0; }
void plat_sleep(int ms) { (void)ms; }
void plat_log(const char *fmt, int v) { (void)fmt; (void)v; }
void plat_diagnostics(void) {}
int plat_audio_init(int rate) { (void)rate; return -1; }
int plat_audio_rate(void) { return 0; }
void plat_audio_close(void) {}
int plat_audio(const int16_t *s, int frames) { (void)s; return frames; }
int plat_audio_queue(void) { return -1; }
int plat_launch(const char *prog, const char *game, const char *saves) { (void)prog; (void)game; (void)saves; return 0; }
const char *plat_runner(const char *system) { (void)system; return NULL; }
