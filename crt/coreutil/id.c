/* id - print user and group identity */
#include "stdio.h"
#include "string.h"
#include "unistd.h"

int main(int argc, char *argv[])
{
    int a = 1;
    while (a < argc && argv[a][0] == '-') {
        const char *f = argv[a] + 1;
        if (strcmp(f, "u") == 0) {
            printf("%u\n", getuid());
            return 0;
        }
        if (strcmp(f, "g") == 0) {
            printf("%u\n", getgid());
            return 0;
        }
        if (strcmp(f, "G") == 0) {
            printf("%u\n", getgid());
            return 0;
        }
        if (strcmp(f, "n") == 0) {
            /* -n with -u/-g is handled by the caller combining flags */
        } else if (strcmp(f, "-help") != 0) {
            fprintf(stderr, "id: unknown option: %s\n", argv[a]);
            return 1;
        }
        a++;
    }

    uid_t uid = geteuid();
    gid_t gid = getegid();
    printf("uid=%u(%s) gid=%u(%s)\n",
           uid, uid == 0 ? "root" : "user",
           gid, gid == 0 ? "root" : "user");
    return 0;
}
