/* ============================================================================
 * Additional stdlib: abs, labs, strtol
 * ============================================================================ */

int abs(int n)
{
    return n < 0 ? -n : n;
}

long labs(long n)
{
    return n < 0 ? -n : n;
}

long strtol(const char *nptr, char **endptr, int base)
{
    const char *p = nptr;
    int neg = 0;
    long val = 0;

    while (*p == ' ' || *p == '\t' || *p == '\n' ||
           *p == '\r' || *p == '\f' || *p == '\v')
        p++;

    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') p++;

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (base == 0 && *p == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }

    int any = 0;
    for (;; p++) {
        int d;
        if (*p >= '0' && *p <= '9')       d = *p - '0';
        else if (*p >= 'a' && *p <= 'z')  d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z')  d = *p - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        any = 1;
    }

    if (endptr) *endptr = any ? (char *)p : (char *)nptr;
    return neg ? -val : val;
}
