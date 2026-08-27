#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* ============================================================================
 * open() flags.  Values MUST match the kernel (include/fs/fs.h).
 * ============================================================================ */

#define O_RDONLY     0x0001
#define O_WRONLY     0x0002
#define O_RDWR       0x0003
#define O_ACCMODE    0x0003
#define O_CREAT      0x0100
#define O_APPEND     0x0200
#define O_DIRECTORY  0x0400   /* hint: opening a directory */
#define O_TRUNC      0x0800   /* truncate file on open     */
#define O_NONBLOCK   0x1000   /* don't block on no-data    */
#define O_CLOEXEC    0x2000   /* close-on-exec             */

/* access() mode constants */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

/* ============================================================================
 * fcntl() commands
 * ============================================================================ */

#define F_DUPFD   0
#define F_GETFD   1
#define F_SETFD   2
#define F_GETFL   3
#define F_SETFL   4
#define F_SETOWN  8
#define F_GETOWN  9

#define FD_CLOEXEC 1

int fcntl(int fd, int cmd, ...);

#endif /* _FCNTL_H */


int pipe2(int pipefd[2], int flags);
