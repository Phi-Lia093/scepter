/* uname - print system information */
#include "stdio.h"
#include "sys/utsname.h"

int main(void)
{
    struct utsname u;
    if (uname(&u) == 0)
        printf("%s %s %s %s\n", u.sysname, u.nodename, u.release, u.machine);
    else
        fprintf(stderr, "uname: failed\n");
    return 0;
}
