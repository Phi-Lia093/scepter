#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* ============================================================================
 * Minimal FILE abstraction: a FILE is just a file descriptor.
 * ============================================================================ */

typedef int FILE;

#define stdin   0
#define stdout  1
#define stderr  2

#define EOF (-1)

/* Character I/O */
int putchar(int c);
int getchar(void);
int fgetc(FILE stream);
int fputc(int c, FILE stream);

/* String I/O */
int puts(const char *s);
int fputs(const char *s, FILE stream);
char *fgets(char *s, int size, FILE stream);

/* Formatted output */
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int fprintf(FILE stream, const char *fmt, ...);
int sprintf(char *s, const char *fmt, ...);
int snprintf(char *s, size_t size, const char *fmt, ...);
int vsprintf(char *s, const char *fmt, va_list ap);
int vsnprintf(char *s, size_t size, const char *fmt, va_list ap);

/* Buffered files (thin wrappers over the fd syscalls) */
FILE fopen(const char *path, const char *mode);
int fclose(FILE stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE stream);

#endif /* _STDIO_H */
