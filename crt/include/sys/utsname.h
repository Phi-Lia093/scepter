#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

#define UTS_LEN 65

struct utsname {
    char sysname[UTS_LEN];
    char nodename[UTS_LEN];
    char release[UTS_LEN];
    char version[UTS_LEN];
    char machine[UTS_LEN];
};

int uname(struct utsname *buf);

#endif /* _SYS_UTSNAME_H */
