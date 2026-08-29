#ifndef KERNEL_SYSCALLS_NET_H
#define KERNEL_SYSCALLS_NET_H

#include <stdint.h>

/* =========================================================================
 * Networking syscalls (kernel/syscalls_net.c)
 *
 * Custom SYS_NET_* numbers (900-903), implemented on top of the net core.
 * ========================================================================= */

/* ifconfig(index, user_out) - fill a struct net_ifconfig. Returns 0 or -errno. */
int sys_net_ifconfig(int index, void *user_out);

/* net_send(name, user_frame, len) - transmit a raw frame. Returns len. */
int sys_net_send(const char *user_name, const void *user_frame, uint32_t len);

/* net_recv(name, user_buf, buflen, block) - dequeue one raw frame.
 * Returns bytes received, 0 if empty and non-blocking, or -errno. */
int sys_net_recv(const char *user_name, void *user_buf, uint32_t buflen, int block);

/* net_setip(name, ip, netmask, gw) - set static IPv4 config. */
int sys_net_setip(const char *user_name, const void *ip,
                  const void *netmask, const void *gw);

#endif /* KERNEL_SYSCALLS_NET_H */
