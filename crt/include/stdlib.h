#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

/* Memory allocation */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

/* String conversion */
int atoi(const char *s);
long strtol(const char *nptr, char **endptr, int base);

/* Absolute value */
int abs(int n);
long labs(long n);

/* Process control */
void exit(int status) __attribute__((noreturn));

/* Environment */
extern char **environ;
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

#endif /* _STDLIB_H */
