/* free - show memory usage */
#include <stdio.h>
#include "sys/sysinfo.h"

int main(void)
{
    struct sysinfo info;
    if (sysinfo(&info) < 0) {
        printf("free: sysinfo failed\n");
        return 1;
    }

    unsigned long unit = info.mem_unit ? info.mem_unit : 1;
    unsigned long total = (unsigned long)info.totalram * unit / 1024;
    unsigned long free_ = (unsigned long)info.freeram * unit / 1024;
    unsigned long used = total - free_;

    printf("              total        used        free\n");
    printf("Mem:      %10lu %10lu %10lu\n", total, used, free_);
    printf("Swap:     %10lu %10lu %10lu\n",
           (unsigned long)info.totalswap * unit / 1024,
           (unsigned long)(info.totalswap - info.freeswap) * unit / 1024,
           (unsigned long)info.freeswap * unit / 1024);
    return 0;
}
