#ifndef _SYS_PRCTL_H
#define _SYS_PRCTL_H

/* prctl() options (Linux subset). */
#define PR_SET_NAME 15
#define PR_GET_NAME 16

int prctl(int option, unsigned long arg2, unsigned long arg3,
          unsigned long arg4, unsigned long arg5);

#endif /* _SYS_PRCTL_H */
