/* posix2 - validation for settimeofday / chroot / flock / fcntl locks / mkfifo */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

static int pass, fail;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  OK   %s\n", msg); pass++; } \
    else { printf("  FAIL %s (errno=%d)\n", msg, errno); fail++; } \
} while (0)

static void child_flock(const char *path)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) _exit(10);
    /* Try non-blocking exclusive: parent holds LOCK_EX -> EWOULDBLOCK */
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
        _exit(11);
    if (errno != EAGAIN && errno != EWOULDBLOCK)
        _exit(12);
    /* Blocking exclusive now (parent will unlock). */
    if (flock(fd, LOCK_EX) != 0)
        _exit(13);
    flock(fd, LOCK_UN);
    _exit(0);
}

static void child_fifo_write(const char *path)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) _exit(20);
    write(fd, "fifo-data", 9);
    close(fd);
    _exit(0);
}

int main(void)
{
    printf("posix2: Phase 1-4 validation\n");

    /* ---- Phase 1: settimeofday ---- */
    printf("[settimeofday]\n");
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        long before = tv.tv_sec;
        struct timeval ntv;
        ntv.tv_sec = before + 1000;
        ntv.tv_usec = 0;
        if (settimeofday(&ntv, NULL) == 0) {
            gettimeofday(&tv, NULL);
            CHECK(tv.tv_sec >= before + 1000, "settimeofday advances wall clock +1000s");
            /* restore */
            ntv.tv_sec = before;
            settimeofday(&ntv, NULL);
        } else {
            CHECK(0, "settimeofday (+1000)");
        }
        time_t t = time(NULL);
        CHECK(t >= before - 5 && t <= before + 1005, "time() follows adjusted clock");
    } else {
        CHECK(0, "gettimeofday");
    }

    /* ---- Phase 2: chroot ---- */
    printf("[chroot]\n");
    if (geteuid() == 0) {
        mkdir("/jail", 0755);
        int f = open("/jail/pass.txt", O_CREAT | O_WRONLY, 0644);
        if (f >= 0) { write(f, "jail", 4); close(f); }

        pid_t pid = fork();
        if (pid == 0) {
            if (chroot("/jail") != 0) _exit(30);
            if (chdir("/") != 0) _exit(31);
            int fd = open("/pass.txt", O_RDONLY);
            if (fd < 0) _exit(32);       /* should see /jail/pass.txt as /pass.txt */
            char buf[8] = {0};
            read(fd, buf, 5);
            close(fd);
            if (strcmp(buf, "jail") != 0) _exit(33);
            /* ../.. must stay inside the jail */
            char cwd[256];
            if (!getcwd(cwd, sizeof(cwd))) _exit(34);
            if (chdir("/../../..") != 0) _exit(35);
            getcwd(cwd, sizeof(cwd));
            if (strcmp(cwd, "/") != 0) _exit(36);
            _exit(0);
        }
        int st;
        waitpid(pid, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "chroot jail: /pass.txt visible, can't escape");
        if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0))
            printf("    (chroot child exit code = %d%s)\n",
                   WIFEXITED(st) ? WEXITSTATUS(st) : -1,
                   WIFSIGNALED(st) ? " (signaled)" : "");

        unlink("/jail/pass.txt");
        rmdir("/jail");
    } else {
        CHECK(0, "chroot requires root (skipped logic)");
    }

    /* ---- Phase 3: flock + fcntl record locks ---- */
    printf("[flock]\n");
    const char *fl = "/flock-test";
    int fd = open(fl, O_CREAT | O_RDWR, 0644);
    CHECK(fd >= 0, "open flock-test");
    if (fd >= 0) {
        CHECK(flock(fd, LOCK_EX) == 0, "flock LOCK_EX");
        pid_t pid = fork();
        if (pid == 0) child_flock(fl);
        else {
            int st;
            sleep(1);                     /* let child try NB -> block */
            flock(fd, LOCK_UN);           /* release so child proceeds */
            waitpid(pid, &st, 0);
            CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "flock cross-process conflict + block");
            CHECK(flock(fd, LOCK_EX | LOCK_NB) == 0, "flock reacquire");
            flock(fd, LOCK_UN);
        }
        /* fcntl record locks */
        struct flock flk;
        memset(&flk, 0, sizeof(flk));
        flk.l_type = F_WRLCK;
        flk.l_whence = SEEK_SET;
        flk.l_start = 0;
        flk.l_len = 0;
        CHECK(fcntl(fd, F_SETLK, &flk) == 0, "fcntl F_SETLK write lock");
        /* F_GETLK on our own lock: no conflict -> F_UNLCK */
        struct flock glk;
        memset(&glk, 0, sizeof(glk));
        glk.l_type = F_WRLCK;
        glk.l_whence = SEEK_SET;
        glk.l_start = 0;
        glk.l_len = 0;
        fcntl(fd, F_GETLK, &glk);
        CHECK(glk.l_type == F_UNLCK, "fcntl F_GETLK self-lock returns UNLCK");
        flk.l_type = F_UNLCK;
        CHECK(fcntl(fd, F_SETLK, &flk) == 0, "fcntl F_SETLK unlock");
        close(fd);
    }
    unlink(fl);

    /* ---- Phase 4: mkfifo ---- */
    printf("[mkfifo]\n");
    const char *fp = "/fifo-test";
    unlink(fp);                        /* clean any stale file from prior boot */
    CHECK(mkfifo(fp, 0644) == 0, "mkfifo");
    struct stat st2;
    CHECK(stat(fp, &st2) == 0 && S_ISFIFO(st2.st_mode), "stat shows FIFO");
    pid_t wpid = fork();
    if (wpid == 0) child_fifo_write(fp);
    else {
        int rfd = open(fp, O_RDONLY);   /* blocks until writer opens */
        CHECK(rfd >= 0, "open fifo read");
        char buf[16] = {0};
        int n = rfd >= 0 ? read(rfd, buf, 15) : -1;
        CHECK(n == 9 && strcmp(buf, "fifo-data") == 0, "fifo data round-trip");
        if (rfd >= 0) close(rfd);
        int ws;
        waitpid(wpid, &ws, 0);
        CHECK(WIFEXITED(ws) && WEXITSTATUS(ws) == 0, "fifo writer exits cleanly");
    }
    /* O_NONBLOCK write open with no reader -> ENXIO */
    errno = 0;
    int nbfd = open(fp, O_WRONLY | O_NONBLOCK);
    CHECK(nbfd < 0 && errno == ENXIO, "fifo O_WRONLY|O_NONBLOCK -> ENXIO");
    unlink(fp);

    printf("\nposix2: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
