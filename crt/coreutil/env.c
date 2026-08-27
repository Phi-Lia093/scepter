/* env - print the environment */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    extern char **environ;
    for (char **e = environ; e && *e; e++)
        printf("%s\n", *e);
    return 0;
}
