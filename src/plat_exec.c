/* Opens the game runner.
 * On the stick (STICK_GAME_VIA_INIT, set by retro/init): the menu writes the request to retro/launch.txt and
 * EXITS (code 10); init runs the game and reopens the menu. That leaves the menu's memory and screen free for the game.
 * On the PC: another process, and the menu waits. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "plat.h"

#define EXIT_CODE_LAUNCH_VIA_INIT 10
#define EXIT_CODE_EXEC_FAILED 127

/* on the stick the game NEVER runs inside the menu (the menu would stay in memory and the game would be slow) */
static int launch_via_init(const char *prog, const char *game, const char *saves)
{
    char path[600];
    snprintf(path, sizeof path, "%s/../launch.txt", plat_roms_dir());
    FILE *file = fopen(path, "w");
    if (file) {
        fprintf(file, "%s\n%s\n%s\n", prog, game, saves);
        if (fclose(file) == 0) {
            plat_shutdown();
            exit(EXIT_CODE_LAUNCH_VIA_INIT);
        }
    }
    plat_log("menu: could not write launch.txt, error %d", errno);

    return -1;
}

static int run_and_wait(const char *prog, const char *game, const char *saves)
{
    pid_t child = fork();
    if (child < 0) {
        return -1;
    }
    if (!child) {
        execl(prog, prog, game, saves, (char *)0);
        perror(prog);
        _exit(EXIT_CODE_EXEC_FAILED);
    }
    int status = 0;
    waitpid(child, &status, 0);
    plat_reactivate();

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int plat_launch(const char *prog, const char *game, const char *saves)
{
    fflush(NULL); /* the menu log reaches the file before the game log */
    if (getenv("STICK_GAME_VIA_INIT")) {
        return launch_via_init(prog, game, saves);
    }

    return run_and_wait(prog, game, saves);
}
