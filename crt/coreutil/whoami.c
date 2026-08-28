/* whoami - print the current user name */
#include "stdio.h"
#include "unistd.h"

int main(void)
{
    uid_t uid = geteuid();
    printf("%s\n", uid == 0 ? "root" : "user");
    return 0;
}
