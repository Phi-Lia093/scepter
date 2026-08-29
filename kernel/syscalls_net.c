/* ============================================================================
 * Networking System Calls
 *
 * Thin wrappers over the net core that validate user pointers and translate
 * the net subsystem's state into POSIX-style errno values.
 *
 *   SYS_NET_IFCONFIG (900): query interface configuration / statistics
 *   SYS_NET_SEND     (901): transmit a raw ethernet frame
 *   SYS_NET_RECV     (902): receive the next queued frame (blocking optional)
 *   SYS_NET_SETIP    (903): change an interface's static IPv4 config
 * ========================================================================= */

#include "kernel/syscalls_net.h"
#include "kernel/syscall.h"
#include "kernel/sched.h"
#include "net/net.h"
#include "lib/string.h"
#include "errno.h"

/* Helpers defined in kernel/syscall.c */
extern int valid_user_pointer(const void *ptr, size_t len);
extern int copy_from_user(void *kernel_dst, const void *user_src, size_t n);
extern int copy_to_user(void *user_dst, const void *kernel_src, size_t n);

/* ---- helpers ---- */

static int copy_name_from_user(const char *user, char *kern, size_t n)
{
    if (!valid_user_pointer(user, 1))
        return -1;
    for (size_t i = 0; i < n - 1; i++) {
        if (copy_from_user(&kern[i], &user[i], 1) < 0)
            return -1;
        if (kern[i] == '\0')
            return 0;
    }
    kern[n - 1] = '\0';
    return 0;
}

/* ============================================================================
 * sys_net_ifconfig - copy interface info to userspace
 * ========================================================================= */

int sys_net_ifconfig(int index, void *user_out)
{
    net_iface_t *iface = net_get_iface(index);
    if (!iface)
        return -ENODEV;
    if (!valid_user_pointer(user_out, sizeof(net_ifconfig_t)))
        return -EFAULT;

    net_ifconfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, iface->name, NET_NAME_LEN - 1);
    memcpy(cfg.mac, iface->mac, NET_MAC_LEN);
    memcpy(cfg.ip, iface->ip, 4);
    memcpy(cfg.netmask, iface->netmask, 4);
    memcpy(cfg.gw, iface->gw, 4);
    cfg.mtu   = iface->mtu;
    cfg.flags = iface->flags;
    cfg.rx_packets  = iface->stats.rx_packets;
    cfg.tx_packets  = iface->stats.tx_packets;
    cfg.rx_bytes    = iface->stats.rx_bytes;
    cfg.tx_bytes    = iface->stats.tx_bytes;
    cfg.rx_dropped  = iface->stats.rx_dropped;
    cfg.tx_dropped  = iface->stats.tx_dropped;

    if (copy_to_user(user_out, &cfg, sizeof(cfg)) < 0)
        return -EFAULT;
    return 0;
}

/* ============================================================================
 * sys_net_send - transmit a raw frame
 * ========================================================================= */

int sys_net_send(const char *user_name, const void *user_frame, uint32_t len)
{
    if (!valid_user_pointer(user_name, 1))
        return -EFAULT;
    if (len == 0 || len > NET_MAX_FRAME)
        return -EINVAL;
    if (!valid_user_pointer(user_frame, len))
        return -EFAULT;

    char name[NET_NAME_LEN];
    if (copy_name_from_user(user_name, name, sizeof(name)) < 0)
        return -EFAULT;

    net_iface_t *iface = net_find_iface(name);
    if (!iface)
        return -ENODEV;
    if (!(iface->flags & NET_IF_UP))
        return -ENODEV;

    uint8_t frame[NET_MAX_FRAME];
    if (copy_from_user(frame, user_frame, len) < 0)
        return -EFAULT;

    if (net_transmit(iface, frame, len) != 0)
        return -EIO;
    return (int)len;
}

/* ============================================================================
 * sys_net_recv - dequeue one raw frame
 *
 * When block is non-zero and the queue is empty, the caller sleeps on the
 * interface's wait queue (woken by net_rx from IRQ context).  Runs with
 * IF=0 (syscall context) so the empty-check -> sleep sequence is atomic.
 * ========================================================================= */

int sys_net_recv(const char *user_name, void *user_buf, uint32_t buflen, int block)
{
    if (!valid_user_pointer(user_name, 1))
        return -EFAULT;
    if (buflen == 0)
        return 0;
    if (!valid_user_pointer(user_buf, buflen))
        return -EFAULT;

    char name[NET_NAME_LEN];
    if (copy_name_from_user(user_name, name, sizeof(name)) < 0)
        return -EFAULT;

    net_iface_t *iface = net_find_iface(name);
    if (!iface)
        return -ENODEV;

    while (1) {
        uint8_t buf[NET_MAX_FRAME];
        int n = net_recv(iface, buf, sizeof(buf));
        if (n > 0) {
            uint32_t out = (buflen < (uint32_t)n) ? buflen : (uint32_t)n;
            if (copy_to_user(user_buf, buf, out) < 0)
                return -EFAULT;
            return (int)out;
        }
        if (!block)
            return 0;
        if (current->pending)
            return -EINTR;
        sleep_on(&iface->rx_wq);
    }
}

/* ============================================================================
 * sys_net_setip - change static IPv4 configuration
 * ========================================================================= */

int sys_net_setip(const char *user_name, const void *ip,
                  const void *netmask, const void *gw)
{
    if (!valid_user_pointer(user_name, 1))
        return -EFAULT;
    if (!ip || !netmask || !gw)
        return -EFAULT;
    if (!valid_user_pointer(ip, 4) ||
        !valid_user_pointer(netmask, 4) ||
        !valid_user_pointer(gw, 4))
        return -EFAULT;

    char name[NET_NAME_LEN];
    if (copy_name_from_user(user_name, name, sizeof(name)) < 0)
        return -EFAULT;

    net_iface_t *iface = net_find_iface(name);
    if (!iface)
        return -ENODEV;

    uint8_t ipb[4], mb[4], gb[4];
    if (copy_from_user(ipb, ip, 4) < 0 ||
        copy_from_user(mb, netmask, 4) < 0 ||
        copy_from_user(gb, gw, 4) < 0)
        return -EFAULT;

    net_set_ip(iface, ipb, mb, gb);
    return 0;
}
