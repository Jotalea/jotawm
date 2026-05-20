#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    setenv("QT_QPA_PLATFORMTHEME",      "qt6ct", 1);
    setenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1",   1);
    setenv("GDK_BACKEND",               "x11",   1);

    char *argv[] = { "jwm", NULL };
    execvp("jwm", argv);
    return 1;
}
