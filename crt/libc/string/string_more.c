/* ============================================================================
 * Additional string functions (strtok_r, strndup, strsep, strcasecmp,
 * basename, dirname).
 * ============================================================================ */

#include <string.h>
#include <stdlib.h>
#include <stddef.h>

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *s;

    if (str == NULL)
        str = *saveptr;
    if (str == NULL)
        return NULL;

    /* Skip leading delimiters. */
    s = str + strspn(str, delim);
    if (*s == '\0') {
        *saveptr = s;
        return NULL;
    }

    /* Find end of token. */
    char *end = s + strcspn(s, delim);
    if (*end != '\0') {
        *end = '\0';
        *saveptr = end + 1;
    } else {
        *saveptr = end;
    }
    return s;
}

char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    for (size_t i = 0; i < len; i++)
        out[i] = s[i];
    out[len] = '\0';
    return out;
}

char *strsep(char **stringp, const char *delim)
{
    char *s = *stringp;
    if (s == NULL)
        return NULL;

    char *end = s + strcspn(s, delim);
    if (*end != '\0') {
        *end = '\0';
        *stringp = end + 1;
    } else {
        *stringp = NULL;
    }
    return s;
}

int strcasecmp(const char *s1, const char *s2)
{
    unsigned char a, b;
    do {
        a = (unsigned char)*s1++;
        b = (unsigned char)*s2++;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
    } while (a && a == b);
    return (int)a - (int)b;
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char a = (unsigned char)s1[i];
        unsigned char b = (unsigned char)s2[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b)
            return (int)a - (int)b;
        if (a == 0)
            break;
    }
    return 0;
}

char *basename(const char *path)
{
    if (!path || *path == '\0')
        return (char *)".";
    const char *p = path;
    const char *last = path;
    while (*p) {
        if (*p == '/' && p[1] != '\0')
            last = p + 1;
        p++;
    }
    return (char *)last;
}

char *dirname(const char *path)
{
    static char buf[256];
    if (!path || *path == '\0') {
        buf[0] = '.';
        buf[1] = '\0';
        return buf;
    }

    const char *p = path;
    const char *last_slash = NULL;
    while (*p) {
        if (*p == '/')
            last_slash = p;
        p++;
    }

    if (!last_slash) {
        buf[0] = '.';
        buf[1] = '\0';
        return buf;
    }

    size_t n = (size_t)(last_slash - path);
    if (n == 0) {
        buf[0] = '/';
        buf[1] = '\0';
        return buf;
    }
    if (n > 255)
        n = 255;
    for (size_t i = 0; i < n; i++)
        buf[i] = path[i];
    buf[n] = '\0';
    return buf;
}
