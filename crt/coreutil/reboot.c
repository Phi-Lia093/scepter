/* reboot/poweroff/halt - control the machine.
 * Behavior depends on argv[0]: reboot -> restart, poweroff -> ACPI off,
 * halt -> halt the CPU. */
#include "stdio.h"
#include "string.h"
#include "sys/reboot.h"

int main(int argc, char *argv[])
{
    (void)argc;
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];

    unsigned int cmd;
    if (strcmp(base, "poweroff") == 0)
        cmd = LINUX_REBOOT_CMD_POWER_OFF;
    else if (strcmp(base, "halt") == 0)
        cmd = LINUX_REBOOT_CMD_RESTART;   /* kernel halts after any cmd */
    else
        cmd = LINUX_REBOOT_CMD_RESTART;

    reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd, NULL);
    fprintf(stderr, "%s: reboot failed\n", base);
    return 1;
}
