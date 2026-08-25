/* clear - clear the terminal */
#include "stdio.h"
#include "sys/ioctl.h"
#include "unistd.h"

int main(void)
{
    ioctl(STDOUT_FILENO, IOCTL_TTY_CLEAR, 0);
    return 0;
}
