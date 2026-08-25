/* ============================================================================
 * Environment access.
 *
 * environ is set by crt0.s to point at the envp block on the initial stack.
 * On the first mutation (setenv/unsetenv) we copy the pointer array into
 * a heap-owned, growable array so it can be extended safely.
 * ============================================================================ */

#include <errno.h>


#include <stdlib.h>
#include <string.h>

char **environ = 0;

static char **env_own = 0;
static int   env_cap = 0;

static int env_count(void)
{
    int n = 0;
    if (environ) while (environ[n]) n++;
    return n;
}

static char *make_entry(const char *name, const char *value)
{
    size_t nlen = strlen(name);
    size_t vlen = strlen(value);
    char *e = malloc(nlen + vlen + 2);
    if (!e) return NULL;
    memcpy(e, name, nlen);
    e[nlen] = '=';
    memcpy(e + nlen + 1, value, vlen);
    e[nlen + vlen + 1] = '\0';
    return e;
}

static int find_slot(const char *name)
{
    if (!environ) return -1;
    size_t nlen = strlen(name);
    for (int i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, nlen) == 0 && environ[i][nlen] == '=')
            return i;
    }
    return -1;
}

char *getenv(const char *name)
{
    if (!name || !environ) return NULL;
    size_t nlen = strlen(name);
    for (int i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, nlen) == 0 && environ[i][nlen] == '=')
            return environ[i] + nlen + 1;
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }

    int slot = find_slot(name);
    if (slot >= 0) {
        if (!overwrite) return 0;
        char *e = make_entry(name, value);
        if (!e) { errno = ENOMEM; return -1; }
        environ[slot] = e;   /* old entry leaks; acceptable for a small libc */
        return 0;
    }

    /* Append: make sure we own a growable array */
    int n = env_count();
    if (environ != env_own) {
        char **copy = malloc(sizeof(char *) * (n + 2));
        if (!copy) { errno = ENOMEM; return -1; }
        for (int i = 0; i < n; i++) copy[i] = environ[i];
        copy[n] = NULL;
        env_own = copy;
        env_cap = n + 2;
        environ = copy;
    } else if (n + 2 > env_cap) {
        int newcap = env_cap ? env_cap * 2 : 4;
        char **copy = realloc(env_own, sizeof(char *) * newcap);
        if (!copy) { errno = ENOMEM; return -1; }
        env_own = copy;
        env_cap = newcap;
        environ = copy;
    }

    char *e = make_entry(name, value);
    if (!e) { errno = ENOMEM; return -1; }
    env_own[n] = e;
    env_own[n + 1] = NULL;
    return 0;
}

int unsetenv(const char *name)
{
    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    int slot = find_slot(name);
    if (slot < 0) return 0;
    for (int i = slot; environ[i]; i++)
        environ[i] = environ[i + 1];
    return 0;
}
