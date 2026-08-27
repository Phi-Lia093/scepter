#ifndef _SYS_MOUNT_H
#define _SYS_MOUNT_H

#include <sys/types.h>

/* mount flags (accepted but not yet interpreted) */
#define MS_RDONLY      0x0001
#define MS_NOSUID      0x0002
#define MS_NODEV       0x0004
#define MS_NOEXEC      0x0008
#define MS_SYNCHRONOUS 0x0010
#define MS_REMOUNT     0x0020

/* umount2() flags */
#define MNT_FORCE 1
#define MNT_DETACH 2
#define MNT_EXPIRE 4


int mount(const char *source, const char *target,
          const char *filesystemtype, unsigned long mountflags,
          const void *data);
int umount(const char *target);
int umount2(const char *target, int flags);

#endif /* _SYS_MOUNT_H */
