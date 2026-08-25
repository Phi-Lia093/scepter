/* pwd - print working directory */
#include "stdio.h"
#include "unistd.h"

int main(void)
{
    char buf[256];
    if (getcwd(buf, sizeof buf))
        printf("%s\n", buf);
    return 0;
}
