/* test / [ - evaluate conditional expressions
 *
 * Supported:
 *   test EXPR          [ EXPR ]
 *   -e/-f/-d/-c/-b/-L FILE    -s FILE    -r/-w/-x FILE
 *   -n STR    -z STR    STR (non-empty)
 *   STR1 = STR2    STR1 != STR2
 *   N1 -eq/-ne/-lt/-le/-gt/-ge N2
 *   ! EXPR
 */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "sys/stat.h"
#include <stdlib.h>

static int is_lbracket = 0;

static int is_int(const char *s)
{
    if (!s || !*s)
        return 0;
    if (*s == '-' || *s == '+')
        s++;
    if (!*s)
        return 0;
    while (*s) {
        if (*s < '0' || *s > '9')
            return 0;
        s++;
    }
    return 1;
}

static int unary(const char *op, const char *arg)
{
    struct stat st;
    int have;

    if (strcmp(op, "-n") == 0)
        return arg[0] != '\0';
    if (strcmp(op, "-z") == 0)
        return arg[0] == '\0';

    /* file tests */
    if (strcmp(op, "-L") == 0)
        have = (lstat(arg, &st) == 0);
    else
        have = (stat(arg, &st) == 0);
    if (!have)
        return 0;

    if (strcmp(op, "-e") == 0)      return 1;
    if (strcmp(op, "-f") == 0)      return st.st_type == DT_REG;
    if (strcmp(op, "-d") == 0)      return st.st_type == DT_DIR;
    if (strcmp(op, "-c") == 0)      return st.st_type == DT_CHRDEV;
    if (strcmp(op, "-b") == 0)      return st.st_type == DT_BLKDEV;
    if (strcmp(op, "-s") == 0)      return st.st_size > 0;
    if (strcmp(op, "-r") == 0)      return access(arg, R_OK) == 0;
    if (strcmp(op, "-w") == 0)      return access(arg, W_OK) == 0;
    if (strcmp(op, "-x") == 0)      return access(arg, X_OK) == 0;

    return -1;   /* unknown op */
}

static int binary(const char *a, const char *op, const char *b)
{
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0)
        return strcmp(a, b) == 0;
    if (strcmp(op, "!=") == 0)
        return strcmp(a, b) != 0;

    if (is_int(a) && is_int(b)) {
        long x = strtol(a, NULL, 10);
        long y = strtol(b, NULL, 10);
        if (strcmp(op, "-eq") == 0) return x == y;
        if (strcmp(op, "-ne") == 0) return x != y;
        if (strcmp(op, "-lt") == 0) return x < y;
        if (strcmp(op, "-le") == 0) return x <= y;
        if (strcmp(op, "-gt") == 0) return x > y;
        if (strcmp(op, "-ge") == 0) return x >= y;
    }
    return -1;
}

int main(int argc, char *argv[])
{
    /* detect whether we were invoked as '[' */
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    if (base[0] == '[')
        is_lbracket = 1;

    char **av = argv + 1;
    int n = argc - 1;

    if (is_lbracket) {
        if (n == 0 || strcmp(argv[argc - 1], "]") != 0) {
            fprintf(stderr, "[: missing ]\n");
            return 2;
        }
        n--;   /* drop the trailing ] */
    }

    if (n == 0) {
        fprintf(stderr, "test: missing operand\n");
        return 2;
    }

    int result;

    if (n == 1) {
        /* STR (non-empty)? */
        result = av[0][0] != '\0';
    } else if (n == 2) {
        if (strcmp(av[0], "!") == 0) {
            /* ! EXPR — treat as negation of a single string */
            result = !(av[1][0] != '\0');
        } else {
            int r = unary(av[0], av[1]);
            if (r < 0) {
                fprintf(stderr, "test: unknown operator: %s\n", av[0]);
                return 2;
            }
            result = r;
        }
    } else if (n == 3) {
        int r = binary(av[0], av[1], av[2]);
        if (r < 0) {
            fprintf(stderr, "test: invalid expression\n");
            return 2;
        }
        result = r;
    } else if (n == 4 && strcmp(av[0], "!") == 0) {
        int r = binary(av[1], av[2], av[3]);
        if (r < 0) {
            fprintf(stderr, "test: invalid expression\n");
            return 2;
        }
        result = !r;
    } else {
        fprintf(stderr, "test: too many arguments\n");
        return 2;
    }

    return result ? 0 : 1;
}
