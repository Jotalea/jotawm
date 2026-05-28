#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

int main(void)
{
    const char *home = getenv("HOME");
    if (home) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/Pictures", home);
        mkdir(path, 0755);
    }
    setenv("QT_QPA_PLATFORMTHEME",      "qt6ct", 1);
    setenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1",   1);
    setenv("GDK_BACKEND",               "x11",   1);
    setenv("GDK_CORE_DEVICE_EVENTS", "1", 1);

    if (fork() == 0) {
        setsid();
        char *cmd[] = { "/bin/sh", "-c", "~/.jotalea/jwm.sh", NULL };
        execvp(cmd[0], cmd);
        _exit(0);
    }

    char *argv[] = { "jwm", NULL };
    execvp("jwm", argv);
    return 1;
}
