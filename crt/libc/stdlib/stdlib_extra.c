/* ============================================================================
 * Additional stdlib: abs, labs, strtol, qsort
 * ============================================================================ */

#include <stddef.h>

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

/* ============================================================================
 * qsort - simple quicksort (Hoare partition).
 * ============================================================================ */

static void swap_bytes(char *a, char *b, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

static void qsort_rec(char *base, size_t nmemb, size_t size,
                      int (*compar)(const void *, const void *))
{
    if (nmemb <= 1)
        return;

    /* Median-of-three pivot. */
    char *lo = base;
    char *hi = base + (nmemb - 1) * size;
    char *mid = base + (nmemb / 2) * size;
    if (compar(mid, lo) < 0) swap_bytes(mid, lo, size);
    if (compar(hi, mid) < 0) swap_bytes(hi, mid, size);
    if (compar(mid, lo) < 0) swap_bytes(mid, lo, size);
    swap_bytes(mid, lo, size);

    char *pivot = lo;
    char *i = base + size;
    char *j = hi;
    while (i <= j) {
        while (i <= j && compar(i, pivot) <= 0) i += size;
        while (compar(j, pivot) > 0) j -= size;
        if (i < j) swap_bytes(i, j, size);
    }
    swap_bytes(pivot, j, size);

    size_t left = (size_t)(j - base) / size;
    qsort_rec(base, left, size, compar);
    qsort_rec(j + size, nmemb - left - 1, size, compar);
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    if (!base || !compar || size == 0)
        return;
    qsort_rec((char *)base, nmemb, size, compar);
}
