/* Stick platform (GeneralPlus GPL32900B): init, shutdown, log, clock, paths and diagnostics.
 * Video, input and audio live in stick_video.c, stick_input.c and stick_audio.c. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "plat.h"
#include "stick_internal.h"

#define LOG_BUFFER_BYTES 16384
#define LOG_LINE_BYTES 256
#define PROCESS_SCAN_LAST_PID 2000
#define US_PER_MS 1000
#define US_PER_SECOND 1000000u
#define NS_PER_US 1000
#define NS_PER_MS 1000000
#define MS_PER_SECOND 1000

static FILE *log_file;

static void write_step(const char *fmt, int first, int second)
{
    fprintf(log_file, "step: ");
    fprintf(log_file, fmt, first, second);
    fprintf(log_file, "\n");
}

static void sync_log(void)
{
    fflush(log_file);
    fsync(fileno(log_file));
}

void stick_step(const char *fmt, int first, int second)
{
    if (!log_file) {
        return;
    }
    write_step(fmt, first, second);
    sync_log();
}

void stick_note(const char *fmt, int first, int second)
{
    if (!log_file) {
        return;
    }
    write_step(fmt, first, second);
}

FILE *stick_log_file(void)
{
    return log_file;
}

int plat_init(void)
{
    if (getenv("STICK_LOG")) {
        log_file = fopen(getenv("STICK_LOG"), "a");
    }
    if (log_file) {
        setvbuf(log_file, NULL, _IOFBF, LOG_BUFFER_BYTES);
    }
    stick_video_init();
    stick_input_init();

    return 0;
}

void plat_shutdown(void)
{
    if (log_file) {
        sync_log();
    }
    if (stick_video_headless()) {
        stick_video_save_shot();
    }
}

uint32_t plat_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return now.tv_sec * US_PER_SECOND + now.tv_nsec / NS_PER_US;
}

uint32_t plat_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return now.tv_sec * MS_PER_SECOND + now.tv_nsec / NS_PER_MS;
}

void plat_sleep(int ms)
{
    usleep(ms * US_PER_MS);
}

const char *plat_roms_dir(void)
{
    const char *dir = getenv("STICK_ROMS");

    return dir ? dir : "/usr/retro/roms";
}

void plat_log(const char *fmt, int value)
{
    stick_note(fmt, value, 0);
}

const char *plat_runner(const char *system_name)
{
    static char path[256];
    snprintf(path, sizeof path, "%s/../%s-arm", plat_roms_dir(), system_name);

    return access(path, X_OK) ? NULL : path;
}

static void copy_file_to_log(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    char line[LOG_LINE_BYTES];
    fprintf(log_file, "--- %s\n", path);
    while (fgets(line, sizeof line, file)) {
        fputs(line, log_file);
    }
    fclose(file);
}

/* name and state of each /proc/<pid>/stat */
static void log_processes(void)
{
    fprintf(log_file, "--- processes (pid name state)\n");
    for (int pid = 1; pid < PROCESS_SCAN_LAST_PID; pid += 1) {
        char path[64], line[LOG_LINE_BYTES];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        FILE *file = fopen(path, "r");
        if (!file) {
            continue;
        }
        if (fgets(line, sizeof line, file)) {
            fputs(line, log_file);
        }
        fclose(file);
    }
}

void plat_diagnostics(void)
{
    if (!log_file) {
        return;
    }
    stick_input_log_state(log_file);
    copy_file_to_log("/proc/tty/driver/serial");
    copy_file_to_log("/proc/loadavg");
    copy_file_to_log("/proc/interrupts");
    log_processes();
    sync_log();
}
