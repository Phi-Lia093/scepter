/* uptime - how long the system has been running */
#include <stdio.h>
#include "sys/sysinfo.h"

int main(void)
{
    struct sysinfo info;
    if (sysinfo(&info) < 0) {
        printf("uptime: sysinfo failed\n");
        return 1;
    }

    unsigned long up = (unsigned long)info.uptime;
    unsigned long days = up / 86400;
    unsigned long hrs  = (up % 86400) / 3600;
    unsigned long mins = (up % 3600) / 60;

    if (days > 0)
        printf(" up %lu day%s, %lu:%02lu\n", days, days == 1 ? "" : "s", hrs, mins);
    else
        printf(" up %lu:%02lu\n", hrs, mins);

    return 0;
}
