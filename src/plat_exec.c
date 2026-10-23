/* Opens the game runner as another process and waits for it to finish (PC and stick). */
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include "plat.h"

void plat_reactivate(void);

int plat_launch(const char *prog, const char *game, const char *saves)
{
    fflush(NULL); /* the menu log reaches the file before the game log */
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
