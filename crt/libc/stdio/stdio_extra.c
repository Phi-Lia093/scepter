/* ============================================================================
 * Additional stdio: FILE-as-fd helpers + line/char input
 * ============================================================================ */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int getchar(void)
{
    char c;
    if (read(stdin, &c, 1) == 1)
        return (unsigned char)c;
    return EOF;
}

int fgetc(FILE stream)
{
    char c;
    if (read(stream, &c, 1) == 1)
        return (unsigned char)c;
    return EOF;
}

int fputc(int c, FILE stream)
{
    char ch = (char)c;
    if (write(stream, &ch, 1) == 1)
        return (unsigned char)ch;
    return EOF;
}

int fputs(const char *s, FILE stream)
{
    size_t len = 0;
    while (s[len]) len++;
    ssize_t n = write(stream, s, len);
    return (n >= 0) ? 0 : EOF;
}

/* Read up to size-1 characters, stopping at newline (kept in the buffer).
 * Returns s on success, NULL on EOF-with-no-bytes or error. */
char *fgets(char *s, int size, FILE stream)
{
    int i = 0;
    if (size <= 1) {
        if (size == 1) s[0] = '\0';
        return s;
    }
    while (i < size - 1) {
        char c;
        int n = read(stream, &c, 1);
        if (n <= 0)
            break;
        s[i++] = c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return NULL;
    s[i] = '\0';
    return s;
}

FILE fopen(const char *path, const char *mode)
{
    if (!mode) return -1;
    int plus = (mode[1] == '+');
    int flags;
    switch (mode[0]) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY;                       break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default:  return -1;
    }
    return open(path, flags, 0666);
}

int fclose(FILE stream)
{
    return close(stream);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE stream)
{
    size_t total = size * nmemb;
    size_t got = 0;
    if (total == 0) return 0;
    while (got < total) {
        ssize_t n = read(stream, (char *)ptr + got, total - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    return got / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE stream)
{
    size_t total = size * nmemb;
    size_t done = 0;
    if (total == 0) return 0;
    while (done < total) {
        ssize_t n = write(stream, (const char *)ptr + done, total - done);
        if (n <= 0) break;
        done += (size_t)n;
    }
    return done / size;
}
