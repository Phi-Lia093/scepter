#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

/* String examination */
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);

/* String comparison */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

/* String manipulation */
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);

/* String searching */
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strtok(char *str, const char *delim);

/* Spanning */
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

/* Memory functions */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

/* Allocation-based helpers (require stdlib) */
char *strdup(const char *s);

#endif /* _STRING_H */
