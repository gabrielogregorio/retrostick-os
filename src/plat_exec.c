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

void plat_reactivate(void);

int plat_launch(const char *prog, const char *game, const char *saves)
{
    fflush(NULL); /* the menu log reaches the file before the game log */
    if (getenv("STICK_GAME_VIA_INIT")) {
        char path[600];
        snprintf(path, sizeof path, "%s/../launch.txt", plat_roms_dir());
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%s\n%s\n%s\n", prog, game, saves);
            if (fclose(f) == 0) { plat_shutdown(); exit(10); }
        }
        /* on the stick the game NEVER runs inside the menu (the menu would stay in memory and the game would be slow) */
        plat_log("menu: could not write launch.txt, error %d", errno);
        return -1;
    }
    pid_t p = fork();
    if (p < 0) return -1;
    if (!p) {
        execl(prog, prog, game, saves, (char *)0);
        perror(prog);
        _exit(127);
    }
    int st = 0;
    waitpid(p, &st, 0);
    plat_reactivate();
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
