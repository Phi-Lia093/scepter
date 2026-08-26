/* ============================================================================
 * tertest - termios regression tests.
 *
 * Auto checks (no keyboard input required):
 *   - tcgetattr defaults (ICANON|ECHO|ISIG|ICRNL|OPOST|ONLCR)
 *   - tcgetattr on a non-tty fails with ENOTTY
 *   - TIOCGWINSZ returns 80x25
 *   - cfmakeraw() clears ICANON|ECHO|ISIG and OPOST, sets VMIN=1
 *   - tcsetattr round-trip
 *
 * Interactive mode (run with "tertest interact"):
 *   - switches to canonical+ECHO and reads a line, printing what it got
 *   - switch to raw+no-ISIG: read should return bytes as typed
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
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

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "interact") == 0) {
        /* ---- interactive canonical read ---- */
        struct termios t;
        tcgetattr(0, &t);
        t.c_lflag |= (ICANON | ECHO);
        t.c_iflag |= ICRNL;
        tcsetattr(0, TCSANOW, &t);

        printf("type a line: ");
        char line[128];
        int n = read(0, line, sizeof(line) - 1);
        if (n > 0) {
            line[n] = '\0';
            printf("\ngot: \"%s\"\n", line);
        } else {
            printf("\ngot EOF\n");
        }

        /* ---- raw mode: bytes pass through as typed (no echo) ---- */
        t.c_lflag &= ~(ICANON | ECHO | ISIG);
        tcsetattr(0, TCSANOW, &t);
        printf("raw mode; type 3 chars: ");
        char buf[3];
        n = read(0, buf, 3);
        printf("\nraw got %d bytes\n", n);
        return failures;
    }

    printf("tertest: termios regression tests\n");

    /* ---- default termios on the tty ---- */
    struct termios t;
    check(tcgetattr(0, &t) == 0, "tcgetattr(stdin)");
    check((t.c_lflag & ICANON) != 0, "default ICANON");
    check((t.c_lflag & ECHO) != 0, "default ECHO");
    check((t.c_lflag & ISIG) != 0, "default ISIG");
    check((t.c_iflag & ICRNL) != 0, "default ICRNL");
    check((t.c_oflag & OPOST) != 0 && (t.c_oflag & ONLCR) != 0,
          "default OPOST|ONLCR");
    check(t.c_cc[VINTR] == 0x03, "VINTR == ^C");
    check(t.c_cc[VERASE] == 0x7f, "VERASE == DEL");
    check(t.c_cc[VMIN] == 1, "VMIN == 1");

    /* ---- tcgetattr on a non-tty fails ENOTTY ---- */
    int fd = open("/bin/tertest", O_RDONLY);
    if (fd >= 0) {
        struct termios nt;
        errno = 0;
        check(tcgetattr(fd, &nt) < 0 && errno == ENOTTY,
              "tcgetattr on regular file -> ENOTTY");
        close(fd);
    }

    /* ---- winsize ---- */
    struct winsize ws;
    check(ioctl(0, TIOCGWINSZ, (unsigned int)&ws) == 0, "TIOCGWINSZ");
    check(ws.ws_col == 80 && ws.ws_row == 25, "winsize is 80x25");

    /* ---- cfmakeraw + round-trip ---- */
    struct termios saved = t;
    struct termios raw = t;
    cfmakeraw(&raw);
    check((raw.c_lflag & (ICANON | ECHO | ISIG)) == 0,
          "cfmakeraw clears ICANON|ECHO|ISIG");
    check((raw.c_oflag & OPOST) == 0, "cfmakeraw clears OPOST");
    check(raw.c_cc[VMIN] == 1 && raw.c_cc[VTIME] == 0,
          "cfmakeraw sets VMIN=1 VTIME=0");

    check(tcsetattr(0, TCSANOW, &raw) == 0, "tcsetattr raw");
    struct termios got;
    tcgetattr(0, &got);
    check((got.c_lflag & (ICANON | ECHO | ISIG)) == 0 &&
          (got.c_oflag & OPOST) == 0, "raw mode applied");

    /* ---- tcflush(TCIFLUSH) works ---- */
    check(tcflush(0, TCIFLUSH) == 0, "tcflush(TCIFLUSH)");

    /* ---- restore ---- */
    check(tcsetattr(0, TCSANOW, &saved) == 0, "restore termios");

    if (failures == 0)
        printf("tertest: ALL TESTS PASSED\n");
    else
        printf("tertest: %d FAILURE(S)\n", failures);
    return failures;
}
