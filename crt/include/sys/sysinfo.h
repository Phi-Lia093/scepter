#ifndef _SYS_SYSINFO_H
#define _SYS_SYSINFO_H

#include <stdint.h>

struct sysinfo {
    int32_t  uptime;      /* seconds since boot                  */
    uint32_t loads[3];    /* 1/5/15 min load averages            */
    uint32_t totalram;    /* total usable main memory (bytes)    */
    uint32_t freeram;
    uint32_t sharedram;
    uint32_t bufferram;
    uint32_t totalswap;
    uint32_t freeswap;
    uint16_t procs;       /* number of current processes         */
    uint16_t pad;
    uint32_t totalhigh;
    uint32_t freehigh;
    uint32_t mem_unit;
    char     _f[8];
};

int sysinfo(struct sysinfo *info);

#endif /* _SYS_SYSINFO_H */
