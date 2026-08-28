/* chown - change file owner and group */
#include "stdio.h"
#include "unistd.h"
#include "stdlib.h"
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: chown USER[:GROUP] FILE...\n");
        return 1;
    }

    const char *spec = argv[1];
    int has_group = 0;
    char group[32] = "";
    const char *colon = strchr(spec, ':');
    if (colon) {
        has_group = 1;
        snprintf(group, sizeof(group), "%s", colon + 1);
    }

    /* Only numeric ids (and "root") are meaningful on this system. */
    uid_t uid;
    if (strcmp(spec, "root") == 0 || colon == spec) {
        uid = 0;
    } else if (colon) {
        char user[32];
        size_t ulen = (size_t)(colon - spec);
        snprintf(user, sizeof(user), "%.*s", (int)ulen, spec);
        uid = (strcmp(user, "root") == 0 || user[0] == '\0') ? 0
             : (uid_t)atoi(user);
    } else {
        uid = (strcmp(spec, "root") == 0) ? 0 : (uid_t)atoi(spec);
    }

    gid_t gid = 0;
    if (has_group && group[0] != '\0')
        gid = (strcmp(group, "root") == 0) ? 0 : (gid_t)atoi(group);

    int ret = 0;
    for (int i = 2; i < argc; i++) {
        if (has_group) {
            if (chown(argv[i], uid, gid) < 0) {
                fprintf(stderr, "chown: %s: failed\n", argv[i]);
                ret = 1;
            }
        } else {
            if (chown(argv[i], uid, (gid_t)-1) < 0) {
                fprintf(stderr, "chown: %s: failed\n", argv[i]);
                ret = 1;
            }
        }
    }
    return ret;
}
