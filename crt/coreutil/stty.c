/* stty - print or change terminal settings */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "sys/ioctl.h"
#include "termios.h"

int main(int argc, char *argv[])
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) < 0) {
        fprintf(stderr, "stty: not a tty\n");
        return 1;
    }

    /* With no args, print the current settings (human readable). */
    if (argc < 2) {
        struct winsize ws;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, (unsigned int)&ws) == 0)
            printf("rows %u; columns %u;\n", ws.ws_row, ws.ws_col);
        printf("lflags: %s%s%s%s\n",
               (t.c_lflag & ICANON) ? "icanon " : "",
               (t.c_lflag & ECHO) ? "echo " : "",
               (t.c_lflag & ISIG) ? "isig " : "",
               (t.c_lflag & ECHOE) ? "echoe" : "");
        return 0;
    }

    /* Otherwise set flags: icanon, -icanon, echo, -echo, isig, -isig,
     * cooked, raw. */
    for (int i = 1; i < argc; i++) {
        const char *f = argv[i];
        int neg = 0;
        if (f[0] == '-') {
            neg = 1;
            f++;
        }
        if (strcmp(f, "icanon") == 0) {
            if (neg) t.c_lflag &= ~ICANON; else t.c_lflag |= ICANON;
        } else if (strcmp(f, "echo") == 0) {
            if (neg) t.c_lflag &= ~ECHO; else t.c_lflag |= ECHO;
        } else if (strcmp(f, "isig") == 0) {
            if (neg) t.c_lflag &= ~ISIG; else t.c_lflag |= ISIG;
        } else if (strcmp(f, "cooked") == 0) {
            t.c_lflag |= ICANON | ECHO;
        } else if (strcmp(f, "raw") == 0) {
            t.c_lflag &= ~(ICANON | ECHO);
        } else {
            fprintf(stderr, "stty: unknown setting: %s\n", argv[i]);
            return 1;
        }
    }

    return tcsetattr(STDIN_FILENO, TCSANOW, &t);
}
