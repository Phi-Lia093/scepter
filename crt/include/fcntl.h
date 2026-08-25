#ifndef _FCNTL_H
#define _FCNTL_H

/* ============================================================================
 * open() flags.  Values MUST match the kernel (include/fs/fs.h).
 * ============================================================================ */

#define O_RDONLY     0x0001
#define O_WRONLY     0x0002
#define O_RDWR       0x0003
#define O_CREAT      0x0100
#define O_APPEND     0x0200
#define O_DIRECTORY  0x0400   /* hint: opening a directory */
#define O_TRUNC      0x0800   /* truncate file on open     */

/* access() mode constants */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#endif /* _FCNTL_H */
