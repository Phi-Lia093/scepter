#ifndef _SYS_FILE_H
#define _SYS_FILE_H

/* flock(2) operation values (Linux ABI). */
#define LOCK_SH 1   /* shared lock  */
#define LOCK_EX 2   /* exclusive lock */
#define LOCK_NB 4   /* don't block when locking */
#define LOCK_UN 8   /* remove lock  */

int flock(int fd, int operation);

#endif /* _SYS_FILE_H */