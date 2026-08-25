/* ============================================================================
 * printf family - vsnprintf core
 *
 * Supports: %d %i %u %x %X %o %c %s %p %%
 * Flags: '-', '0', '+', ' ', '#'    Width, '*'    Precision, '.*'
 * ============================================================================ */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    char  *buf;      /* NULL => emit via fd/putchar               */
    size_t size;     /* capacity (excluding NUL); 0 = unbounded   */
    size_t len;      /* bytes emitted                             */
    int    discard;  /* 1 => count only, emit nothing             */
    int    fd;       /* target fd when buf==NULL && fd>=0         */
} fmt_t;

static void out_ch(fmt_t *f, char c)
{
    if (f->discard) {
        f->len++;
        return;
    }
    if (!f->buf) {
        if (f->fd >= 0) {
            extern int write(int fd, const void *buf, unsigned int count);
            write(f->fd, &c, 1);
        } else {
            extern int putchar(int c);
            putchar((unsigned char)c);
        }
    } else if (f->size == 0 || f->len < f->size) {
        f->buf[f->len] = c;
    }
    f->len++;
}

static void out_number(fmt_t *f, unsigned long v, int base, int upper,
                       int width, int prec, int left, char pad,
                       int neg, int showplus, int space, int alt)
{
    char tmp[40];
    int  ndig = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (v == 0 && prec != 0)
        tmp[ndig++] = '0';
    while (v > 0) {
        tmp[ndig++] = digits[v % base];
        v /= base;
    }

    /* leading sign / prefix */
    char pre[3];
    int  plen = 0;
    if (neg)            pre[plen++] = '-';
    else if (showplus)  pre[plen++] = '+';
    else if (space)     pre[plen++] = ' ';
    if (alt && base == 16) {
        pre[plen++] = '0';
        pre[plen++] = upper ? 'X' : 'x';
    } else if (alt && base == 8 && ndig > 0 && tmp[ndig - 1] != '0') {
        pre[plen++] = '0';
    }

    int zeros = (prec > ndig) ? prec - ndig : 0;
    int body  = ndig + zeros;
    int total = plen + body;

    if (pad == '0' && prec <= 0) {
        /* zero padding: sign/prefix first, then zeros up to width */
        int nz = width - total;
        if (nz < 0) nz = 0;
        for (int i = 0; i < plen; i++) out_ch(f, pre[i]);
        for (int i = 0; i < nz; i++)   out_ch(f, '0');
        for (int i = 0; i < zeros; i++) out_ch(f, '0');
        for (int i = ndig; i > 0; i--) out_ch(f, tmp[i - 1]);
        return;
    }

    int sp = width - total;
    if (sp > 0 && !left) {
        for (int i = 0; i < sp; i++) out_ch(f, ' ');
    }
    for (int i = 0; i < plen; i++) out_ch(f, pre[i]);
    for (int i = 0; i < zeros; i++) out_ch(f, '0');
    for (int i = ndig; i > 0; i--) out_ch(f, tmp[i - 1]);
    if (sp > 0 && left) {
        for (int i = 0; i < sp; i++) out_ch(f, ' ');
    }
}

static int fmt_run(fmt_t *f, const char *fmt, va_list ap)
{
    while (*fmt) {
        if (*fmt != '%') {
            out_ch(f, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            out_ch(f, '%');
            fmt++;
            continue;
        }

        /* flags */
        int left = 0, showplus = 0, space = 0, alt = 0;
        char pad = ' ';
        for (;;) {
            if      (*fmt == '-') { left = 1; pad = ' '; fmt++; }
            else if (*fmt == '+') { showplus = 1; fmt++; }
            else if (*fmt == ' ') { space = 1; fmt++; }
            else if (*fmt == '#') { alt = 1; fmt++; }
            else if (*fmt == '0') { if (!left) pad = '0'; fmt++; }
            else break;
        }

        /* width */
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) { left = 1; width = -width; }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }

        /* precision */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                if (prec < 0) prec = -1;
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }

        /* length modifiers (32-bit ints; accept and ignore) */
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' ||
               *fmt == 't' || *fmt == 'j')
            fmt++;

        int ch = *fmt++;
        switch (ch) {
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            if (v < 0)
                out_number(f, (unsigned long)-(long)v, 10, 0,
                           width, prec, left, pad, 1, 0, 0, 0);
            else
                out_number(f, (unsigned long)v, 10, 0,
                           width, prec, left, pad, 0, showplus, space, 0);
            break;
        }
        case 'u':
            out_number(f, va_arg(ap, unsigned int), 10, 0,
                       width, prec, left, pad, 0, 0, 0, 0);
            break;
        case 'x':
            out_number(f, va_arg(ap, unsigned int), 16, 0,
                       width, prec, left, pad, 0, 0, 0, alt);
            break;
        case 'X':
            out_number(f, va_arg(ap, unsigned int), 16, 1,
                       width, prec, left, pad, 0, 0, 0, alt);
            break;
        case 'o':
            out_number(f, va_arg(ap, unsigned int), 8, 0,
                       width, prec, left, pad, 0, 0, 0, alt);
            break;
        case 'p':
            out_number(f, (unsigned long)va_arg(ap, void *), 16, 0,
                       width, prec, left, pad, 0, 0, 0, 1);
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (left) {
                out_ch(f, c);
                for (int i = 1; i < width; i++) out_ch(f, ' ');
            } else {
                for (int i = 1; i < width; i++) out_ch(f, ' ');
                out_ch(f, c);
            }
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len]) len++;
            if (prec >= 0 && len > prec) len = prec;
            if (!left)
                for (int i = len; i < width; i++) out_ch(f, ' ');
            for (int i = 0; i < len; i++) out_ch(f, s[i]);
            if (left)
                for (int i = len; i < width; i++) out_ch(f, ' ');
            break;
        }
        default:
            out_ch(f, '%');
            if (ch) out_ch(f, (char)ch);
            break;
        }
    }
    return (int)f->len;
}

int vsnprintf(char *s, size_t size, const char *fmt, va_list ap)
{
    if (size == 0) {
        fmt_t f = { NULL, 0, 0, 1, -1 };   /* count-only */
        return fmt_run(&f, fmt, ap);
    }
    fmt_t f = { s, size - 1, 0, 0, -1 };
    int n = fmt_run(&f, fmt, ap);
    size_t pos = f.len < size - 1 ? f.len : size - 1;
    s[pos] = '\0';
    return n;
}

int vsprintf(char *s, const char *fmt, va_list ap)
{
    fmt_t f = { s, 0, 0, 0, -1 };      /* unbounded */
    int n = fmt_run(&f, fmt, ap);
    s[f.len] = '\0';
    return n;
}

int vprintf(const char *fmt, va_list ap)
{
    fmt_t f = { NULL, 0, 0, 0, -1 };
    return fmt_run(&f, fmt, ap);
}

int fprintf(FILE stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fmt_t f = { NULL, 0, 0, 0, stream };
    int n = fmt_run(&f, fmt, ap);
    va_end(ap);
    return n;
}

int snprintf(char *s, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s, size, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(s, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
