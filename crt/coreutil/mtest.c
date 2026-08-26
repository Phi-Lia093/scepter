/* ============================================================================
 * mtest - mount(2)/umount(2)/sync(2) + /dev/null + /dev/zero regression test.
 *
 * Requires data.img to have a second Minix partition (see
 * script/make_test_disk.sh) which becomes /dev/hdb2.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>

static int failures = 0;
static void check(int cond, const char *msg)
{
    if (cond) {
        printf("PASS: %s\n", msg);
    } else {
        printf("FAIL: %s (errno=%d)\n", msg, errno);
        failures++;
    }
}

int main(void)
{
    printf("mtest: mount/umount + null/zero/sync tests\n");

    /* ---- /dev/null ---- */
    int fd = open("/dev/null", O_WRONLY);
    check(fd >= 0, "open /dev/null O_WRONLY");
    if (fd >= 0) {
        check(write(fd, "discard me", 10) == 10, "write to /dev/null");
        close(fd);
    }
    fd = open("/dev/null", O_RDONLY);
    if (fd >= 0) {
        char b;
        check(read(fd, &b, 1) == 0, "read /dev/null returns EOF");
        close(fd);
    }

    /* ---- /dev/zero ---- */
    fd = open("/dev/zero", O_RDONLY);
    check(fd >= 0, "open /dev/zero");
    if (fd >= 0) {
        char z[8];
        memset(z, 0xAA, sizeof z);
        check(read(fd, z, sizeof z) == 8, "read /dev/zero returns 8");
        int all_zero = 1;
        for (int i = 0; i < 8; i++)
            if (z[i] != 0) all_zero = 0;
        check(all_zero, "/dev/zero bytes are zero");
        close(fd);
    }

    /* ---- sync() ---- */
    sync();
    check(1, "sync() returns");

    /* ---- mount /dev/hdb2 at /mnt ---- */
    mkdir("/mnt", 0777);
    check(mount("/dev/hdb2", "/mnt", "minix3", 0, NULL) == 0,
          "mount /dev/hdb2 at /mnt");

    int mfd = open("/mnt/hello.txt", O_CREAT | O_WRONLY);
    check(mfd >= 0, "create /mnt/hello.txt");
    if (mfd >= 0) {
        check(write(mfd, "mount works", 11) == 11, "write to /mnt file");
        close(mfd);
    }

    /* ---- unmount then remount: data must persist ---- */
    check(umount("/mnt") == 0, "umount /mnt");
    check(mount("/dev/hdb2", "/mnt", "minix3", 0, NULL) == 0,
          "remount /dev/hdb2 at /mnt");
    mfd = open("/mnt/hello.txt", O_RDONLY);
    check(mfd >= 0, "reopen /mnt/hello.txt");
    if (mfd >= 0) {
        char buf[16];
        int n = read(mfd, buf, sizeof buf);
        check(n == 11 && memcmp(buf, "mount works", 11) == 0,
              "data survived unmount/remount");
        close(mfd);
        unlink("/mnt/hello.txt");
    }

    /* ---- mount with a bad fstype / missing source ---- */
    errno = 0;
    check(mount("/dev/hdb2", "/mnt", "nosuchfs", 0, NULL) < 0,
          "mount with unknown fstype fails");
    errno = 0;
    check(mount("/dev/nosuchdev", "/mnt", "minix3", 0, NULL) < 0,
          "mount with missing source fails");
    errno = 0;
    check(mount("/dev/hdb2", "/nonexistent_dir", "minix3", 0, NULL) < 0,
          "mount at missing target fails");

    check(umount("/mnt") == 0, "final umount /mnt");

    if (failures == 0)
        printf("mtest: ALL TESTS PASSED\n");
    else
        printf("mtest: %d FAILURE(S)\n", failures);
    return failures;
}
