/* yes - output a string repeatedly until killed */
#include "stdio.h"
#include "string.h"
#include "unistd.h"

int main(int argc, char *argv[])
{
    const char *s = (argc > 1) ? argv[1] : "y";
    size_t len = strlen(s);

    for (;;) {
        if (write(STDOUT_FILENO, s, len) < 0)
            break;
        if (write(STDOUT_FILENO, "\n", 1) < 0)
            break;
    }
    return 0;
}
