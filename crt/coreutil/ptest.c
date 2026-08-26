/* ============================================================================
 * ptest - Priority-1 regression test: RTC, clock_gettime, times, itimers,
 * mprotect, file-backed mmap (MAP_SHARED write-back), posix_memalign,
 * strtok_r/strcasecmp, scandir, getopt, localtime/mktime round-trip.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

static int failures = 0;
static char fail_msgs[10][64];
static int nfail = 0;

static void check(int cond, const char *msg)
{
    if (cond) {
        printf("PASS: %s\n", msg);
    } else {
        printf("FAIL: %s (errno=%d)\n", msg, errno);
        failures++;
        if (nfail < 10) {
            strncpy(fail_msgs[nfail], msg, 63);
            fail_msgs[nfail][63] = 0;
            nfail++;
        }
    }
}

static volatile int sigalrm_count = 0;
static void on_alarm(int sig) { (void)sig; sigalrm_count++; }

int main(void)
{
    printf("ptest: priority-1 regression tests\n");

    /* ---- RTC device ---- */
    int rfd = open("/dev/rtc0", O_RDONLY);
    check(rfd >= 0, "open /dev/rtc0");
    if (rfd >= 0) {
        char c;
        check(read(rfd, &c, 1) == 1, "read /dev/rtc0 returns a byte");
        int t = ioctl(rfd, IOCTL_RTC_GET_TIME, 0);
        check(t > 1700000000, "ioctl RTC_GET_TIME is a plausible Unix time");
        close(rfd);
    }

    /* ---- clock_gettime ---- */
    struct timespec ts1, ts2;
    check(clock_gettime(CLOCK_MONOTONIC, &ts1) == 0, "clock_gettime MONOTONIC");
    nanosleep(&(struct timespec){0, 50000000}, NULL);
    check(clock_gettime(CLOCK_MONOTONIC, &ts2) == 0, "clock_gettime #2");
    check(ts2.tv_sec > ts1.tv_sec ||
          (ts2.tv_sec == ts1.tv_sec && ts2.tv_nsec > ts1.tv_nsec),
          "MONOTONIC advances");
    check(clock_gettime(CLOCK_REALTIME, &ts1) == 0 && ts1.tv_sec > 1700000000,
          "REALTIME is plausible");
    check(clock_getres(CLOCK_MONOTONIC, &ts2) == 0 && ts2.tv_nsec <= 10000000,
          "clock_getres <= 10ms");

    /* ---- time() ---- */
    time_t now = time(NULL);
    check(now > 1700000000, "time() returns plausible value");

    /* ---- times() ---- */
    struct tms tms;
    clock_t tc = times(&tms);
    check(tc > 0 && tms.tms_utime >= 0, "times() returns ticks");

    /* ---- setitimer -> SIGALRM ---- */
    signal(SIGALRM, on_alarm);
    struct itimerval itv;
    itv.it_value.tv_sec = 0; itv.it_value.tv_usec = 200000;
    itv.it_interval.tv_sec = 0; itv.it_interval.tv_usec = 0;
    check(setitimer(ITIMER_REAL, &itv, NULL) == 0, "setitimer 200ms");
    while (sigalrm_count == 0)
        pause();   /* block until the handler runs */
    check(sigalrm_count > 0, "SIGALRM fired");
    struct itimerval old;
    check(getitimer(ITIMER_REAL, &old) == 0, "getitimer");
    check(setitimer(ITIMER_REAL, &(struct itimerval){0}, NULL) == 0,
          "setitimer disarm");
    signal(SIGALRM, SIG_DFL);

    /* ---- mprotect ---- */
    void *mp = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(mp != MAP_FAILED, "anon mmap");
    if (mp != MAP_FAILED) {
        ((char *)mp)[0] = 0x42;
        check(((char *)mp)[0] == 0x42, "mmap page writable");
        check(mprotect(mp, 4096, PROT_READ) == 0, "mprotect PROT_READ");
        check(mprotect(mp, 4096, PROT_READ | PROT_WRITE) == 0,
              "mprotect restore RW");
        check(((char *)mp)[0] == 0x42, "data survives mprotect");
        check(munmap(mp, 4096) == 0, "munmap");
    }

    /* ---- file-backed mmap, MAP_SHARED write-back ---- */
    const char *fn = "/ptest_shm.txt";
    int fd = open(fn, O_CREAT | O_RDWR);
    check(fd >= 0, "open shared file");
    if (fd >= 0) {
        char init[16];
        for (int i = 0; i < 16; i++) init[i] = 'A' + i;
        check(write(fd, init, 16) == 16, "write initial file");
        void *map = mmap(NULL, 16, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        check(map != MAP_FAILED, "MAP_SHARED file mmap");
        if (map != MAP_FAILED) {
            check(((char *)map)[0] == 'A', "mmap reads file content");
            ((char *)map)[5] = 'X';   /* modify */
            check(munmap(map, 16) == 0, "munmap shared");
            lseek(fd, 0, SEEK_SET);
            char rbuf[16];
            check(read(fd, rbuf, 16) == 16 && rbuf[5] == 'X',
                  "MAP_SHARED wrote back to file");
        }
        close(fd);
        unlink(fn);
    }

    /* ---- posix_memalign ---- */
    void *al = NULL;
    check(posix_memalign(&al, 64, 123) == 0, "posix_memalign 64");
    check(al && ((uintptr_t)al & 63) == 0, "posix_memalign aligned to 64");
    if (al) {
        ((char *)al)[0] = 1;
        ((char *)al)[122] = 2;
        free(al);
        check(1, "free(posix_memalign) safe");
    }

    /* ---- strtok_r / strcasecmp / strndup ---- */
    char buf[] = "alpha,beta,gamma";
    char *save = NULL;
    char *t1 = strtok_r(buf, ",", &save);
    char *t2 = strtok_r(NULL, ",", &save);
    char *t3 = strtok_r(NULL, ",", &save);
    check(t1 && strcmp(t1, "alpha") == 0 && t2 && strcmp(t2, "beta") == 0 &&
          t3 && strcmp(t3, "gamma") == 0, "strtok_r splits");
    check(strcasecmp("AbC", "aBc") == 0, "strcasecmp equal");
    check(strcasecmp("abc", "abd") < 0, "strcasecmp ordering");
    char *dup = strndup("hello world", 5);
    check(dup && strcmp(dup, "hello") == 0, "strndup");
    free(dup);

    /* ---- scandir / qsort ---- */
    mkdir("/ptest_dir", 0755);
    int dfd = open("/ptest_dir/z.txt", O_CREAT | O_WRONLY);
    if (dfd >= 0) close(dfd);
    dfd = open("/ptest_dir/a.txt", O_CREAT | O_WRONLY);
    if (dfd >= 0) close(dfd);
    struct dirent **names = NULL;
    int n = scandir("/ptest_dir", &names, NULL, NULL);
    check(n >= 4, "scandir lists entries");
    if (n > 0) {
        /* scandir includes "." and ".."; verify the real entries sort. */
        int ia = -1, iz = -1;
        for (int i = 0; i < n; i++) {
            if (strcmp(names[i]->d_name, "a.txt") == 0) ia = i;
            if (strcmp(names[i]->d_name, "z.txt") == 0) iz = i;
        }
        check(ia >= 0 && iz > ia, "scandir sorted (a.txt before z.txt)");
        for (int i = 0; i < n; i++) free(names[i]);
        free(names);
    }

    /* ---- localtime / mktime round-trip ---- */
    time_t t0 = 1234567890;
    struct tm *lt = localtime(&t0);
    check(lt && lt->tm_year == 109 && lt->tm_mon == 1 && lt->tm_mday == 13,
          "localtime(1234567890) == 2009-02-13");
    char stamp[64];
    if (lt)
        check(strftime(stamp, sizeof(stamp), "%Y-%m-%d", lt) > 0 &&
              strcmp(stamp, "2009-02-13") == 0, "strftime %Y-%m-%d");
    struct tm round = *lt;
    time_t t1_ = mktime(&round);
    check(t1_ == t0, "mktime(localtime(t)) == t");

    /* ---- basename / dirname ---- */
    check(strcmp(basename("/a/b/c.txt"), "c.txt") == 0, "basename");
    check(strcmp(dirname("/a/b/c.txt"), "/a/b") == 0, "dirname");

    /* ---- getline ---- */
    int f2 = open("/ptest_getline.txt", O_CREAT | O_WRONLY);
    if (f2 >= 0) {
        write(f2, "first line\nsecond\n", 18);
        close(f2);
    }
    f2 = open("/ptest_getline.txt", O_RDONLY);
    char *line = NULL;
    size_t linecap = 0;
    ssize_t gl = getline(&line, &linecap, f2);
    check(gl == 11 && line && strncmp(line, "first line", 10) == 0,
          "getline reads first line");
    gl = getline(&line, &linecap, f2);
    check(gl == 7 && line && strncmp(line, "second", 6) == 0,
          "getline reads second line");
    free(line);
    close(f2);
    unlink("/ptest_getline.txt");

    /* ---- sysconf ---- */
    check(sysconf(_SC_CLK_TCK) == 100, "sysconf CLK_TCK == 100");
    check(sysconf(_SC_PAGESIZE) == 4096, "sysconf PAGESIZE == 4096");

    /* ---- cleanup ---- */
    unlink("/ptest_dir/z.txt");
    unlink("/ptest_dir/a.txt");
    check(rmdir("/ptest_dir") == 0, "rmdir cleanup");

    if (failures == 0) {
        printf("ptest: ALL TESTS PASSED\n");
    } else {
        printf("ptest: %d FAILURE(S):\n", failures);
        for (int i = 0; i < nfail; i++)
            printf("  * %s\n", fail_msgs[i]);
    }
    return failures;
}
