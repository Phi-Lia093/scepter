/* ============================================================================
 * ftest - Phase B regression test: fcntl, O_NONBLOCK, select/poll,
 * readv/writev, pread/pwrite, link/symlink/readlink, chmod, umask, mknod,
 * ftruncate, FD_CLOEXEC.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <errno.h>

static int failures = 0;
static char fail_msgs[8][64];
static int nfail = 0;

static void check(int cond, const char *msg)
{
    if (cond)
        printf("PASS: %s\n", msg);
    else {
        printf("FAIL: %s (errno=%d)\n", msg, errno);
        failures++;
        if (nfail < 8) {
            strncpy(fail_msgs[nfail], msg, 63);
            fail_msgs[nfail][63] = 0;
            nfail++;
        }
    }
}

int main(void)
{
    printf("ftest: phase B regression tests\n");
    const char *dir = "/ftest_tmp";

    check(mkdir(dir, 0755) == 0, "mkdir /ftest_tmp");

    /* ---- fcntl: F_DUPFD ---- */
    int fd = open("/ftest_tmp/a.txt", O_CREAT | O_RDWR);
    check(fd >= 0, "open O_CREAT");
    int dupfd = fcntl(fd, F_DUPFD, 10);
    check(dupfd >= 10, "fcntl F_DUPFD returns fd >= 10");
    int dupfd2 = fcntl(fd, F_DUPFD, 10);
    check(dupfd2 == dupfd + 1, "F_DUPFD gives the next lowest free fd");
    close(dupfd2);

    /* ---- fcntl: F_GETFD/F_SETFD (FD_CLOEXEC) ---- */
    check(fcntl(fd, F_GETFD) == 0, "F_GETFD initially 0");
    check(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0, "F_SETFD FD_CLOEXEC");
    check(fcntl(fd, F_GETFD) == FD_CLOEXEC, "F_GETFD sees FD_CLOEXEC");
    check(fcntl(fd, F_SETFD, 0) == 0, "F_SETFD clear");
    check(fcntl(fd, F_GETFD) == 0, "F_GETFD cleared");

    /* ---- fcntl: F_GETFL/F_SETFL (O_NONBLOCK) ---- */
    check((fcntl(fd, F_GETFL) & O_RDWR) == O_RDWR, "F_GETFL preserves access mode");
    check(fcntl(fd, F_SETFL, O_NONBLOCK) == 0, "F_SETFL O_NONBLOCK");
    check((fcntl(fd, F_GETFL) & O_NONBLOCK) != 0, "F_GETFL sees O_NONBLOCK");
    check((fcntl(fd, F_GETFL) & O_RDWR) == O_RDWR, "F_SETFL keeps access mode");
    fcntl(fd, F_SETFL, 0);   /* clear O_NONBLOCK for later use */

    /* ---- O_NONBLOCK pipe: read returns EAGAIN ---- */
    int pfd[2];
    check(pipe(pfd) == 0, "pipe()");
    check(fcntl(pfd[0], F_SETFL, O_NONBLOCK) == 0, "set pipe read nonblock");
    char c = 0;
    errno = 0;
    check(read(pfd[0], &c, 1) == -1 && errno == EAGAIN,
          "nonblock read on empty pipe -> EAGAIN");
    check(write(pfd[1], "Z", 1) == 1, "write to pipe");
    check(read(pfd[0], &c, 1) == 1 && c == 'Z', "nonblock read gets data");

    /* ---- poll() ---- */
    check(write(pfd[1], "P", 1) == 1, "write for poll");
    struct pollfd pf[2];
    pf[0].fd = pfd[0]; pf[0].events = POLLIN; pf[0].revents = 0;
    pf[1].fd = pfd[1]; pf[1].events = POLLOUT; pf[1].revents = 0;
    int pr = poll(pf, 2, 0);
    check(pr == 2, "poll reports read+write ready");
    check((pf[0].revents & POLLIN) != 0, "poll read end POLLIN");
    check((pf[1].revents & POLLOUT) != 0, "poll write end POLLOUT");
    /* Empty read end after draining: not POLLIN */
    check(read(pfd[0], &c, 1) == 1 && c == 'P', "drain pipe");
    pr = poll(pf, 1, 0);
    check(pr == 0, "poll on empty pipe -> 0 ready");

    /* ---- select() ---- */
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(pfd[0], &rset);
    struct timeval tv = { 0, 200000 };
    int sr = select(pfd[0] + 1, &rset, NULL, NULL, &tv);
    check(sr == 0, "select times out (200ms) on empty pipe");
    check(write(pfd[1], "S", 1) == 1, "write for select");
    FD_ZERO(&rset);
    FD_SET(pfd[0], &rset);
    tv.tv_sec = 1; tv.tv_usec = 0;
    sr = select(pfd[0] + 1, &rset, NULL, NULL, &tv);
    check(sr == 1 && FD_ISSET(pfd[0], &rset), "select sees pipe data");
    check(read(pfd[0], &c, 1) == 1 && c == 'S', "select data readable");
    close(pfd[0]); close(pfd[1]);

    /* ---- readv / writev ---- */
    int fv = open("/ftest_tmp/vec.txt", O_CREAT | O_RDWR);
    struct iovec wv[2];
    char part1[] = "Hello ";
    char part2[] = "World!";
    wv[0].iov_base = part1; wv[0].iov_len = 6;
    wv[1].iov_base = part2; wv[1].iov_len = 6;
    check(writev(fv, wv, 2) == 12, "writev writes 12 bytes");
    lseek(fv, 0, SEEK_SET);
    char rbuf[13];
    struct iovec rv[2];
    rv[0].iov_base = rbuf;      rv[0].iov_len = 6;
    rv[1].iov_base = rbuf + 6;  rv[1].iov_len = 6;
    check(readv(fv, rv, 2) == 12, "readv reads 12 bytes");
    rbuf[12] = 0;
    check(strcmp(rbuf, "Hello World!") == 0, "readv content matches");

    /* ---- pread / pwrite ---- */
    check(pwrite(fv, "B", 1, 10) == 1, "pwrite at offset 10");
    char bc = 0;
    check(pread(fv, &bc, 1, 10) == 1 && bc == 'B', "pread at offset 10");
    off_t cur = lseek(fv, 0, SEEK_CUR);
    check(cur == 12, "file offset unchanged after pread/pwrite");

    /* ---- ftruncate ---- */
    check(ftruncate(fv, 5) == 0, "ftruncate to 5");
    struct stat st;
    check(fstat(fv, &st) == 0 && st.st_size == 5, "fstat size == 5");
    close(fv);

    /* ---- link (hard link) ---- */
    check(link("/ftest_tmp/vec.txt", "/ftest_tmp/vec2.txt") == 0,
          "link creates second name");
    check(access("/ftest_tmp/vec2.txt", F_OK) == 0, "linked name exists");
    check(link("/ftest_tmp/a.txt", "/ftest_tmp/alink.txt") == 0, "link a.txt");

    /* ---- symlink + readlink + lstat ---- */
    check(symlink("vec.txt", "/ftest_tmp/vec.lnk") == 0, "symlink");
    char lbuf[64];
    ssize_t lr = readlink("/ftest_tmp/vec.lnk", lbuf, sizeof(lbuf));
    check(lr == 7 && strcmp(lbuf, "vec.txt") == 0, "readlink target");
    check(lstat("/ftest_tmp/vec.lnk", &st) == 0 && S_ISLNK(st.st_mode),
          "lstat sees symlink type");

    /* ---- chmod / fchmod ---- */
    check(chmod("/ftest_tmp/a.txt", 0600) == 0, "chmod 0600");
    check(stat("/ftest_tmp/a.txt", &st) == 0 &&
          (st.st_mode & 0777) == 0600, "stat sees chmod mode");
    int fa = open("/ftest_tmp/a.txt", O_RDONLY);
    check(fchmod(fa, 0644) == 0, "fchmod 0644");
    check(stat("/ftest_tmp/a.txt", &st) == 0 &&
          (st.st_mode & 0777) == 0644, "stat sees fchmod mode");
    close(fa);

    /* ---- umask ---- */
    mode_t oldmask = umask(0);
    check(umask(oldmask) == 0, "umask returns old mask");
    check(umask(0022) == oldmask, "umask round-trip");

    /* ---- mknod (char device inode) ---- */
    check(mknod("/ftest_tmp/null", S_IFCHR | 0666, 0) == 0, "mknod char dev");
    check(lstat("/ftest_tmp/null", &st) == 0 && S_ISCHR(st.st_mode),
          "mknod created S_IFCHR");

    /* ---- truncate on a path ---- */
    check(truncate("/ftest_tmp/a.txt", 2) == 0, "truncate path to 2");
    check(stat("/ftest_tmp/a.txt", &st) == 0 && st.st_size == 2,
          "truncated size == 2");

    /* ---- cleanup ---- */
    unlink("/ftest_tmp/a.txt"); unlink("/ftest_tmp/alink.txt");
    unlink("/ftest_tmp/vec.txt"); unlink("/ftest_tmp/vec2.txt");
    unlink("/ftest_tmp/vec.lnk"); unlink("/ftest_tmp/null");
    check(rmdir("/ftest_tmp") == 0, "rmdir cleanup");

    if (failures == 0) {
        printf("ftest: ALL TESTS PASSED\n");
    } else {
        printf("ftest: %d FAILURE(S):\n", failures);
        for (int i = 0; i < nfail; i++)
            printf("  * %s\n", fail_msgs[i]);
    }
    return failures;
}
