/* ifconfig - show or configure network interfaces */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "errno.h"
#include "sys/net.h"

static int parse_ip(const char *s, uint8_t out[4])
{
    int part = 0, val = 0, digits = 0;
    const char *p = s;
    for (;; p++) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            digits++;
            if (val > 255 || digits > 3)
                return -1;
        } else if (*p == '.' || *p == '\0') {
            if (digits == 0 || part > 3)
                return -1;
            out[part++] = (uint8_t)val;
            val = 0;
            digits = 0;
            if (*p == '\0')
                break;
        } else {
            return -1;
        }
    }
    return (part == 4) ? 0 : -1;
}

static void print_ip(const uint8_t ip[4])
{
    printf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

static void show_iface(int idx)
{
    net_ifconfig_t c;
    if (net_ifconfig(idx, &c) < 0)
        return;

    printf("%s: flags=%d  mtu %u\n", c.name, (unsigned)c.flags, (unsigned)c.mtu);
    printf("        ether %02x:%02x:%02x:%02x:%02x:%02x\n",
           c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5]);
    printf("        inet ");
    print_ip(c.ip);
    printf("  netmask ");
    print_ip(c.netmask);
    printf("  gateway ");
    print_ip(c.gw);
    printf("\n");
    printf("        RX packets %u bytes %u dropped %u\n",
           (unsigned)c.rx_packets, (unsigned)c.rx_bytes, (unsigned)c.rx_dropped);
    printf("        TX packets %u bytes %u dropped %u\n",
           (unsigned)c.tx_packets, (unsigned)c.tx_bytes, (unsigned)c.tx_dropped);
}

static int show_named(const char *name)
{
    for (int i = 0; i < 16; i++) {
        net_ifconfig_t c;
        if (net_ifconfig(i, &c) < 0)
            break;
        if (strcmp(c.name, name) == 0) {
            show_iface(i);
            return 0;
        }
    }
    return -1;
}

int main(int argc, char *argv[])
{
    if (argc == 2) {
        /* ifconfig <name>: show one interface */
        if (show_named(argv[1]) < 0) {
            fprintf(stderr, "ifconfig: no such interface '%s'\n", argv[1]);
            return 1;
        }
        return 0;
    }

    if (argc == 5) {
        /* ifconfig <name> <ip> <netmask> <gw>: set static config */
        uint8_t ip[4], mask[4], gw[4];
        if (parse_ip(argv[2], ip) < 0 || parse_ip(argv[3], mask) < 0 ||
            parse_ip(argv[4], gw) < 0) {
            fprintf(stderr, "ifconfig: bad address\n");
            return 1;
        }
        if (net_setip(argv[1], ip, mask, gw) < 0) {
            fprintf(stderr, "ifconfig: cannot set address on '%s'\n", argv[1]);
            return 1;
        }
        return 0;
    }

    /* ifconfig: list all interfaces */
    for (int i = 0; i < 16; i++) {
        net_ifconfig_t c;
        if (net_ifconfig(i, &c) < 0)
            break;
        show_iface(i);
    }
    return 0;
}
