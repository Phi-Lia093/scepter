#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H

/* reboot() magic numbers (Linux). */
#define LINUX_REBOOT_MAGIC1         0xfee1dead
#define LINUX_REBOOT_MAGIC2         0x28121969
#define LINUX_REBOOT_MAGIC2A        0x05121996
#define LINUX_REBOOT_MAGIC2B        0x16041998
#define LINUX_REBOOT_MAGIC2C        0x20112004

#define LINUX_REBOOT_CMD_RESTART    0x01234567
#define LINUX_REBOOT_CMD_POWER_OFF  0x4321fedc
#define LINUX_REBOOT_CMD_RESTART2   0xa1b2c3d4

int reboot(int magic1, int magic2, int cmd, void *arg);

#endif /* _SYS_REBOOT_H */
