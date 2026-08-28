/* ============================================================================
 * sh - an interactive shell for Scepter OS
 *
 * Features:
 *   - prompt with the current directory
 *   - line editing (echo, backspace, Ctrl-C aborts the line, Ctrl-D exits)
 *   - command history (Up/Down arrows)
 *   - tokenizer honouring single/double quotes and backslash escapes
 *   - command separation with ';', pipelines with '|', conditionals
 *     '&&' and '||', background jobs with '&'
 *   - redirection '<', '>', '>>'
 *   - glob expansion (*, ?) for unquoted words
 *   - $? last-exit-status expansion
 *   - builtins: cd, pwd, echo, exit, clear, help, export, unset, test/[,
 *     printf, type, command, umask, source/., jobs, fg, bg
 *   - external commands resolved through $PATH, fork+execve+wait
 * ============================================================================ */

#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "string.h"
#include "fcntl.h"
#include "sys/ioctl.h"
#include "sys/wait.h"
#include "sys/stat.h"
#include "termios.h"
#include "dirent.h"

#define MAX_LINE   512
#define MAX_WORDS  96
#define MAX_ARGS   32
#define MAX_STAGES 16
#define MAX_JOBS   16
#define HIST_MAX   50

/* ============================================================================
 * Background job table
 * ============================================================================ */

static pid_t jobs[MAX_JOBS];

static void add_job(pid_t p)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i] == 0) { jobs[i] = p; return; }
    }
}

static void reap_jobs(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i] != 0) {
            int st;
            pid_t r = waitpid(jobs[i], &st, WNOHANG);
            if (r == jobs[i]) {
                printf("[done] %d\n", jobs[i]);
                jobs[i] = 0;
            } else if (r < 0) {
                jobs[i] = 0;
            }
        }
    }
}

/* ============================================================================
 * Command history
 * ============================================================================ */

static char history[HIST_MAX][MAX_LINE];
static int  hist_count = 0;    /* number of entries stored */
static int  hist_cursor = 0;   /* current position for up/down recall */

static void history_add(const char *line)
{
    if (line[0] == '\0')
        return;
    /* don't store an exact duplicate of the previous entry */
    if (hist_count > 0 && strcmp(history[hist_count - 1], line) == 0)
        return;
    if (hist_count < HIST_MAX) {
        strncpy(history[hist_count], line, MAX_LINE - 1);
        history[hist_count][MAX_LINE - 1] = '\0';
        hist_count++;
    } else {
        /* shift down */
        for (int i = 1; i < HIST_MAX; i++)
            strcpy(history[i - 1], history[i]);
        strncpy(history[HIST_MAX - 1], line, MAX_LINE - 1);
        history[HIST_MAX - 1][MAX_LINE - 1] = '\0';
    }
    hist_cursor = hist_count;
}

/* ============================================================================
 * Last exit status ($?)
 * ============================================================================ */

static int last_status = 0;

/* Forward declarations (defined below). */
static void run_line(char *line);
static int find_in_path(const char *name, char *out, int size);

/* ============================================================================
 * Line editor
 *
 * Returns: line length on success, -1 on EOF (Ctrl-D / read error),
 *          -2 when the line was aborted with Ctrl-C.
 * ============================================================================ */

/* ============================================================================
 * termios helpers
 *
 * The shell performs its own line editing, so it puts the tty in raw mode
 * (ICANON/ECHO off, ISIG kept on so ^C still works) while reading a line,
 * and restores cooked mode while a foreground command is running.
 * ============================================================================ */

static struct termios shell_saved_tio;
static int shell_tio_saved = 0;

static void set_term_raw(void)
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
        /* Capture the pristine cooked termios exactly once, at the first
         * prompt.  Never overwrite it: a child (e.g. a termios
         * experiment) may have left the tty in raw mode, and we must
         * not treat that as the "cooked" state we restore for the next
         * foreground job. */
        if (!shell_tio_saved) {
            shell_saved_tio = t;
            shell_tio_saved = 1;
        }
        t.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
}

static void set_term_cooked(void)
{
    if (shell_tio_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &shell_saved_tio);
}

/* ============================================================================
 * read_line - read one line from stdin (raw mode: one byte at a time)
 * ============================================================================ */

static int read_line(char *buf, int size)
{
    int len = 0;
    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            return -1;

        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            write(STDOUT_FILENO, "\n", 1);
            history_add(buf);
            return len;
        }
        if (c == '\b' || c == 0x7f) {
            if (len > 0) {
                len--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }
        if (c == 0x03) {              /* Ctrl-C */
            write(STDOUT_FILENO, "^C\n", 3);
            return -2;
        }
        if (c == 0x04) {              /* Ctrl-D */
            if (len == 0)
                return -1;
            continue;
        }
        if (c == 0x1b) {              /* ESC: arrow keys */
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1)
                continue;
            if (seq[0] != '[') {
                /* lone ESC: ignore */
                continue;
            }
            if (read(STDIN_FILENO, &seq[1], 1) != 1)
                continue;

            /* erase the current edit line on screen */
            for (int i = 0; i < len; i++)
                write(STDOUT_FILENO, "\b \b", 3);

            if (seq[1] == 'A') {      /* Up */
                if (hist_cursor > 0)
                    hist_cursor--;
                else
                    hist_cursor = 0;
            } else if (seq[1] == 'B') { /* Down */
                if (hist_cursor < hist_count)
                    hist_cursor++;
            } else {
                /* left/right: unsupported; restore the edit line */
                for (int i = 0; i < len; i++)
                    write(STDOUT_FILENO, &buf[i], 1);
                continue;
            }

            if (hist_cursor < hist_count) {
                len = (int)strlen(history[hist_cursor]);
                if (len > size - 1)
                    len = size - 1;
                strncpy(buf, history[hist_cursor], (size_t)len);
                buf[len] = '\0';
                write(STDOUT_FILENO, buf, (size_t)len);
            } else {
                /* past the newest entry: empty line */
                len = 0;
                buf[0] = '\0';
            }
            continue;
        }
        if (c >= 32 && c < 127 && len < size - 1) {
            buf[len++] = c;
            write(STDOUT_FILENO, &c, 1);
        }
    }
}

/* ============================================================================
 * Tokenizer
 *
 * Splits a line into words honouring quotes/escapes.  Unquoted
 * '<', '>', '|', ';', '&' become their own operator tokens (so
 * "echo a>b" tokenizes as "echo", "a", ">", "b").
 * ============================================================================ */

static int is_meta(char c)
{
    return c == '<' || c == '>' || c == '|' || c == ';' || c == '&';
}

static int tokenize(char *line, char *words[], int max)
{
    static char ops[32][3];   /* storage for operator tokens */
    int n = 0;
    char *p = line;

    while (1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (n >= max) break;

        /* operator token */
        if (is_meta(*p)) {
            if (n >= 32) break;
            if (*p == '>' && p[1] == '>') {
                ops[n][0] = '>'; ops[n][1] = '>'; ops[n][2] = '\0';
                words[n] = ops[n];
                p += 2;
            } else {
                ops[n][0] = *p; ops[n][1] = '\0';
                words[n] = ops[n];
                p += 1;
            }
            n++;
            continue;
        }

        /* word token: copy in place, removing quotes/escapes */
        char *dst = p;
        int quote = 0;
        int stopped = 0;
        words[n] = p;
        while (*p) {
            if (quote == 0) {
                if (*p == ' ' || *p == '\t' || is_meta(*p)) { stopped = 1; break; }
                if (*p == '\'') { quote = 1; p++; continue; }
                if (*p == '"')  { quote = 2; p++; continue; }
                if (*p == '\\' && p[1]) { p++; *dst++ = *p++; continue; }
                *dst++ = *p++;
            } else if (quote == 1) {
                if (*p == '\'') { quote = 0; p++; continue; }
                *dst++ = *p++;
            } else {
                if (*p == '"') { quote = 0; p++; continue; }
                if (*p == '\\' && p[1]) { p++; *dst++ = *p++; continue; }
                *dst++ = *p++;
            }
        }
        *dst = '\0';
        if (stopped) p++;   /* consume the separator that ended the word */
        n++;
    }
    return n;
}

/* ============================================================================
 * Command representation
 * ============================================================================ */

typedef struct {
    char *argv[MAX_ARGS];
    char *in_file;
    char *out_file;
    int   append;
    int   background;
} command_t;

static int build_commands(char *words[], int n, command_t *cmds, int max_stages)
{
    int nstage = 0;
    command_t *cur = &cmds[0];
    memset(cur, 0, sizeof(*cur));
    int argc = 0;

    for (int i = 0; i < n; i++) {
        char *w = words[i];

        if (strcmp(w, "|") == 0) {
            cur->argv[argc] = NULL;
            if (++nstage >= max_stages) return -1;
            cur = &cmds[nstage];
            memset(cur, 0, sizeof(*cur));
            argc = 0;
        } else if (strcmp(w, "&") == 0) {
            cur->background = 1;
        } else if (strcmp(w, "<") == 0) {
            if (i + 1 >= n) { fprintf(stderr, "sh: syntax error near '<'\n"); return -1; }
            cur->in_file = words[++i];
        } else if (strcmp(w, ">") == 0 || strcmp(w, ">>") == 0) {
            if (i + 1 >= n) { fprintf(stderr, "sh: syntax error near '%s'\n", w); return -1; }
            cur->out_file = words[++i];
            cur->append = (w[1] == '>');
        } else {
            if (argc < MAX_ARGS - 1)
                cur->argv[argc++] = w;
        }
    }
    cur->argv[argc] = NULL;
    return nstage + 1;
}

/* ============================================================================
 * PATH lookup
 * ============================================================================ */

static int find_in_path(const char *name, char *out, int size)
{
    if (strchr(name, '/')) {
        strncpy(out, name, size - 1);
        out[size - 1] = '\0';
        return access(out, F_OK) == 0;
    }

    const char *path = getenv("PATH");
    if (!path) path = "/bin";

    while (*path) {
        const char *end = strchr(path, ':');
        int plen = end ? (int)(end - path) : (int)strlen(path);
        if (plen + (int)strlen(name) + 2 < size) {
            memcpy(out, path, plen);
            out[plen] = '/';
            strcpy(out + plen + 1, name);
            if (access(out, F_OK) == 0)
                return 1;
        }
        if (!end) break;
        path = end + 1;
    }
    return 0;
}

/* ============================================================================
 * Builtins
 * ============================================================================ */

static int run_simple(command_t *c);   /* defined below */
static int run_script_file(const char *path);   /* defined below */

static int is_builtin(const char *cmd)
{
    return strcmp(cmd, "cd") == 0 || strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "echo") == 0 || strcmp(cmd, "exit") == 0 ||
           strcmp(cmd, "clear") == 0 || strcmp(cmd, "help") == 0 ||
           strcmp(cmd, "export") == 0 || strcmp(cmd, "unset") == 0 ||
           strcmp(cmd, "type") == 0 || strcmp(cmd, "command") == 0 ||
           strcmp(cmd, "umask") == 0 || strcmp(cmd, "source") == 0 ||
           strcmp(cmd, ".") == 0 || strcmp(cmd, "jobs") == 0 ||
           strcmp(cmd, "fg") == 0 || strcmp(cmd, "bg") == 0 ||
           strcmp(cmd, "exec") == 0 || strcmp(cmd, "history") == 0;
}

static int do_builtin(command_t *c)
{
    const char *cmd = c->argv[0];

    if (strcmp(cmd, "cd") == 0) {
        const char *dir = c->argv[1] ? c->argv[1] : getenv("HOME");
        if (!dir) dir = "/";
        if (chdir(dir) < 0)
            fprintf(stderr, "sh: cd: %s: no such directory\n", dir);
        return 0;
    }

    if (strcmp(cmd, "pwd") == 0) {
        char buf[256];
        if (getcwd(buf, sizeof buf))
            printf("%s\n", buf);
        return 0;
    }

    if (strcmp(cmd, "echo") == 0) {
        int i = 1, nl = 1;
        if (c->argv[1] && strcmp(c->argv[1], "-n") == 0) { nl = 0; i = 2; }
        int first = 1;
        for (; c->argv[i]; i++) {
            if (!first) printf(" ");
            printf("%s", c->argv[i]);
            first = 0;
        }
        if (nl) printf("\n");
        return 0;
    }

    if (strcmp(cmd, "exit") == 0) {
        int st = 0;
        if (c->argv[1]) st = atoi(c->argv[1]);
        set_term_cooked();   /* restore cooked mode before exiting */
        exit(st);
    }

    if (strcmp(cmd, "clear") == 0) {
        ioctl(STDOUT_FILENO, IOCTL_TTY_CLEAR, 0);
        return 0;
    }

    if (strcmp(cmd, "help") == 0) {
        printf("builtins: cd pwd echo exit clear help export unset\n");
        printf("          type command umask source . jobs fg bg exec history\n");
        printf("syntax:   cmd | cmd    (pipe)\n");
        printf("          cmd < f      (input redirection)\n");
        printf("          cmd > f, >> f (output redirection)\n");
        printf("          cmd &        (background)\n");
        printf("          cmd ; cmd    (sequential)\n");
        printf("          cmd && cmd   (run if success)\n");
        printf("          cmd || cmd   (run if failure)\n");
        printf("          * and ? glob, $? last status, Up/Down history\n");
        return 0;
    }

    if (strcmp(cmd, "export") == 0) {
        if (c->argv[1]) {
            char *eq = strchr(c->argv[1], '=');
            if (eq) {
                *eq = '\0';
                setenv(c->argv[1], eq + 1, 1);
            }
        }
        return 0;
    }

    if (strcmp(cmd, "unset") == 0) {
        if (c->argv[1]) unsetenv(c->argv[1]);
        return 0;
    }

    if (strcmp(cmd, "umask") == 0) {
        if (c->argv[1]) {
            mode_t m = (mode_t)strtol(c->argv[1], NULL, 8);
            umask(m);
        } else {
            mode_t old = umask(0);
            umask(old);
            printf("%04o\n", old);
        }
        return 0;
    }

    if (strcmp(cmd, "type") == 0) {
        if (!c->argv[1]) {
            fprintf(stderr, "type: usage: type NAME...\n");
            return 2;
        }
        for (int i = 1; c->argv[i]; i++) {
            if (is_builtin(c->argv[i])) {
                printf("%s is a shell builtin\n", c->argv[i]);
            } else {
                char path[256];
                if (find_in_path(c->argv[i], path, sizeof(path)))
                    printf("%s is %s\n", c->argv[i], path);
                else
                    printf("type: %s: not found\n", c->argv[i]);
            }
        }
        return 0;
    }

    if (strcmp(cmd, "command") == 0) {
        if (!c->argv[1]) {
            fprintf(stderr, "command: usage: command [-v] CMD [ARG...]\n");
            return 2;
        }
        if (strcmp(c->argv[1], "-v") == 0 && c->argv[2]) {
            char path[256];
            if (find_in_path(c->argv[2], path, sizeof(path)))
                printf("%s\n", path);
            else
                fprintf(stderr, "command: %s: not found\n", c->argv[2]);
            return 0;
        }
        /* shift argv: run the remaining command (builtin or external) */
        for (int i = 1; c->argv[i]; i++)
            c->argv[i - 1] = c->argv[i];
        return run_simple(c);
    }

    if (strcmp(cmd, "source") == 0 || strcmp(cmd, ".") == 0) {
        if (!c->argv[1]) {
            fprintf(stderr, "%s: usage: %s FILE\n", cmd, cmd);
            return 2;
        }
        return run_script_file(c->argv[1]);
    }

    if (strcmp(cmd, "jobs") == 0) {
        int any = 0;
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i]) {
                printf("[%d] %d\n", i + 1, jobs[i]);
                any = 1;
            }
        }
        if (!any)
            printf("no jobs\n");
        return 0;
    }

    if (strcmp(cmd, "fg") == 0) {
        /* Wait for a background job.  No stop/continue support in the
         * kernel, so fg simply waits for the job to finish. */
        pid_t p = 0;
        if (c->argv[1]) {
            if (c->argv[1][0] == '%')
                p = jobs[atoi(c->argv[1] + 1) - 1];
            else
                p = (pid_t)atoi(c->argv[1]);
        }
        if (!p) {
            for (int i = 0; i < MAX_JOBS; i++)
                if (jobs[i]) { p = jobs[i]; break; }
        }
        if (!p) {
            fprintf(stderr, "fg: no current job\n");
            return 1;
        }
        for (int i = 0; i < MAX_JOBS; i++)
            if (jobs[i] == p) jobs[i] = 0;
        int st;
        waitpid(p, &st, 0);
        return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }

    if (strcmp(cmd, "bg") == 0) {
        /* No job control (no stop/continue) - the job already runs. */
        if (c->argv[1])
            printf("bg: %s (already running; no stop/continue support)\n",
                   c->argv[1]);
        else
            printf("bg: no job control in this shell\n");
        return 0;
    }

    if (strcmp(cmd, "exec") == 0) {
        if (!c->argv[1]) {
            fprintf(stderr, "exec: usage: exec CMD [ARG...]\n");
            return 2;
        }
        char path[256];
        if (!find_in_path(c->argv[1], path, sizeof(path))) {
            fprintf(stderr, "exec: %s: command not found\n", c->argv[1]);
            return 127;
        }
        execve(path, &c->argv[1], environ);
        fprintf(stderr, "exec: %s: exec failed\n", c->argv[1]);
        return 126;
    }

    if (strcmp(cmd, "history") == 0) {
        for (int i = 0; i < hist_count; i++)
            printf("%4d  %s\n", i + 1, history[i]);
        return 0;
    }

    return 1;
}

/* Execute a shell script from a file (used by 'source'/'.' and by
 * 'sh FILE' invocation).  Returns the last command's exit status. */
static int run_script_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "sh: %s: no such file\n", path);
        return 1;
    }
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, fd) > 0) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (line[0] != '\0' && line[0] != '#')
            run_line(line);
    }
    free(line);
    close(fd);
    return last_status;
}

/* ============================================================================
 * Child-side redirection setup.  Returns 0 on success, -1 on error.
 * ============================================================================ */

static int setup_redirection(command_t *c)
{
    if (c->in_file) {
        int fd = open(c->in_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "sh: %s: no such file\n", c->in_file);
            return -1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (c->out_file) {
        int flags = O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC);
        int fd = open(c->out_file, flags, 0644);
        if (fd < 0) {
            fprintf(stderr, "sh: %s: cannot create\n", c->out_file);
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    return 0;
}

/* ============================================================================
 * Run a single (non-pipe) command, with optional backgrounding.
 * ============================================================================ */

static int run_simple(command_t *c)
{
    if (!c->argv[0])
        return 0;

    /* A builtin without redirection runs directly in the shell.  With
     * redirection it must run in a child so the redirect applies. */
    if (is_builtin(c->argv[0]) && !c->in_file && !c->out_file)
        return do_builtin(c);

    char path[256];
    if (!find_in_path(c->argv[0], path, sizeof path)) {
        fprintf(stderr, "sh: %s: command not found\n", c->argv[0]);
        return 127;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "sh: fork failed\n");
        return -1;
    }
    if (pid == 0) {
        if (setup_redirection(c) < 0)
            exit(1);
        if (is_builtin(c->argv[0])) {
            do_builtin(c);
            exit(0);
        }
        execve(path, c->argv, environ);
        fprintf(stderr, "sh: %s: exec failed\n", c->argv[0]);
        exit(126);
    }

    if (c->background) {
        printf("[%d]\n", pid);
        add_job(pid);
        return 0;
    }

    ioctl(STDOUT_FILENO, IOCTL_TTY_SET_FG, (unsigned int)pid);
    int status = 0;
    waitpid(pid, &status, 0);
    ioctl(STDOUT_FILENO, IOCTL_TTY_SET_FG, 0);

    if (WIFSIGNALED(status) && WTERMSIG(status) != 0) {
        fprintf(stderr, "sh: %s: terminated by signal %d\n",
                c->argv[0], WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* ============================================================================
 * Run a pipeline of commands.
 * ============================================================================ */

static int run_pipeline(command_t *cmds, int n)
{
    int bg = cmds[n - 1].background;
    int prev_read = -1;
    pid_t pids[MAX_STAGES];

    for (int i = 0; i < n; i++) {
        int fds[2] = { -1, -1 };
        if (i < n - 1) {
            if (pipe(fds) < 0) {
                fprintf(stderr, "sh: pipe failed\n");
                if (prev_read >= 0) close(prev_read);
                return -1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "sh: fork failed\n");
            return -1;
        }

        if (pid == 0) {
            /* child: wire up the pipe ends */
            if (prev_read >= 0) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }
            if (i < n - 1) {
                dup2(fds[1], STDOUT_FILENO);
            }
            if (fds[0] >= 0) close(fds[0]);
            if (fds[1] >= 0) close(fds[1]);

            if (!cmds[i].argv[0]) exit(0);
            if (setup_redirection(&cmds[i]) < 0) exit(1);
            if (is_builtin(cmds[i].argv[0])) {
                /* builtin mid-pipeline: run it in the child */
                do_builtin(&cmds[i]);
                exit(0);
            }
            char path[256];
            if (!find_in_path(cmds[i].argv[0], path, sizeof path)) {
                fprintf(stderr, "sh: %s: command not found\n", cmds[i].argv[0]);
                exit(127);
            }
            execve(path, cmds[i].argv, environ);
            fprintf(stderr, "sh: %s: exec failed\n", cmds[i].argv[0]);
            exit(126);
        }

        /* parent: close the previous pipe's read end (the child already has
         * its copy); KEEP the current pipe's read end open for the next
         * stage; close the write end (only the child uses it). */
        if (prev_read >= 0) close(prev_read);
        prev_read = fds[0];
        if (fds[1] >= 0) close(fds[1]);
        pids[i] = pid;
    }

    if (!bg) {
        ioctl(STDOUT_FILENO, IOCTL_TTY_SET_FG, (unsigned int)pids[n - 1]);
        int status = 0;
        for (int i = 0; i < n; i++) {
            int st = 0;
            waitpid(pids[i], &st, 0);
            if (i == n - 1) status = st;
        }
        ioctl(STDOUT_FILENO, IOCTL_TTY_SET_FG, 0);
        if (WIFSIGNALED(status) && WTERMSIG(status) != 0)
            return 128 + WTERMSIG(status);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    printf("[%d]\n", pids[n - 1]);
    add_job(pids[n - 1]);
    return 0;
}

/* ============================================================================
 * Execute one ';'-separated segment.
 * ============================================================================ */

/* Find the next unquoted "&&" or "||" in the string (quote-aware).
 * Returns the operator ("&&" or "||") or NULL. */
static const char *find_conditional(char *p, char **out_op_pos)
{
    int quote = 0;
    for (; *p; p++) {
        if (*p == '\'' && quote != 2)       quote = quote ? 0 : 1;
        else if (*p == '"' && quote != 1)    quote = quote ? 0 : 2;
        else if (*p == '\\' && quote != 1) { p++; }
        else if (quote == 0 && p[0] == '&' && p[1] == '&') {
            *out_op_pos = p;
            return "&&";
        } else if (quote == 0 && p[0] == '|' && p[1] == '|') {
            *out_op_pos = p;
            return "||";
        }
    }
    return NULL;
}

/* Simple glob matcher: '*' matches any sequence, '?' any single char. */
static int glob_match(const char *pat, const char *s)
{
    if (!*pat)
        return *s == '\0';
    if (*pat == '*')
        return glob_match(pat + 1, s) || (*s && glob_match(pat, s + 1));
    if (*pat == '?')
        return *s && glob_match(pat + 1, s + 1);
    return *s == *pat && glob_match(pat + 1, s + 1);
}

/* Expand one word: $? substitution and * / ? globbing against the cwd.
 * Fills out[] with 0 or more strings (malloc'd; caller frees). */
static int expand_word(const char *word, char *out[], int max)
{
    if (strstr(word, "$?")) {
        char tmp[512];
        char *d = tmp;
        for (const char *p = word; *p && d < tmp + sizeof(tmp) - 16; ) {
            if (p[0] == '$' && p[1] == '?') {
                int n = snprintf(d, 16, "%d", last_status);
                d += n;
                p += 2;
            } else {
                *d++ = *p++;
            }
        }
        *d = '\0';
        out[0] = strdup(tmp);
        return 1;
    }

    if (!strchr(word, '*') && !strchr(word, '?')) {
        out[0] = strdup(word);
        return 1;
    }

    /* glob: match against entries of the directory part of the word */
    char dirbuf[256], namebuf[128];
    const char *slash = strrchr(word, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - word);
        if (dlen == 0) {
            strcpy(dirbuf, "/");
        } else {
            memcpy(dirbuf, word, dlen);
            dirbuf[dlen] = '\0';
        }
        snprintf(namebuf, sizeof(namebuf), "%s", slash + 1);
    } else {
        strcpy(dirbuf, ".");
        snprintf(namebuf, sizeof(namebuf), "%s", word);
    }

    DIR *d = opendir(dirbuf);
    if (!d) {
        out[0] = strdup(word);
        return 1;
    }

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) && count < max) {
        if (e->d_name[0] == '.' && namebuf[0] != '.')
            continue;   /* don't match hidden files unless pattern is hidden */
        if (glob_match(namebuf, e->d_name)) {
            char full[512];
            if (strcmp(dirbuf, ".") == 0)
                snprintf(full, sizeof(full), "%s", e->d_name);
            else if (strcmp(dirbuf, "/") == 0)
                snprintf(full, sizeof(full), "/%s", e->d_name);
            else
                snprintf(full, sizeof(full), "%s/%s", dirbuf, e->d_name);
            out[count++] = strdup(full);
        }
    }
    closedir(d);

    if (count == 0) {
        out[0] = strdup(word);   /* no match: keep literal */
        return 1;
    }

    /* simple sort (bubble) of matches */
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (strcmp(out[i], out[j]) > 0) {
                char *t = out[i];
                out[i] = out[j];
                out[j] = t;
            }
    return count;
}

/* Run a single command (simple or pipeline). Returns the exit status. */
static int run_subsegment(char *seg)
{
    char *words[MAX_WORDS];
    int n = tokenize(seg, words, MAX_WORDS);
    if (n == 0)
        return 0;

    /* Word expansion: $? and globbing.  Redirection operators (<, >, >>)
     * are copied through verbatim (they are never expanded); only real
     * operands get $?/glob treatment. */
    char *ewords[MAX_WORDS * 2];
    int en = 0;
    for (int i = 0; i < n && en < MAX_WORDS * 2 - 1; i++) {
        if (strchr(words[i], '<') || strchr(words[i], '>')) {
            ewords[en++] = strdup(words[i]);   /* keep redirection op */
            continue;
        }
        int k = expand_word(words[i], &ewords[en], MAX_WORDS * 2 - en - 1);
        en += k;
    }

    command_t cmds[MAX_STAGES];
    int ns = build_commands(ewords, en, cmds, MAX_STAGES);
    for (int i = 0; i < en; i++)
        free(ewords[i]);
    if (ns <= 0)
        return 127;

    if (ns == 1)
        return run_simple(&cmds[0]);
    return run_pipeline(cmds, ns);
}

/* Execute a ';'-segment, honouring '&&' and '||' short-circuiting. */
static int run_segment(char *seg)
{
    int status = 0;
    int run_next = 1;   /* run the first subsegment */
    char *p = seg;
    while (p) {
        char *op_pos = NULL;
        const char *op = find_conditional(p, &op_pos);
        if (op_pos) *op_pos = '\0';

        if (run_next)
            status = run_subsegment(p);

        if (!op) break;
        /* determine whether to run the next subsegment */
        if (strcmp(op, "&&") == 0)
            run_next = (status == 0);
        else /* || */
            run_next = (status != 0);
        p = op_pos + 2;
    }
    return status;
}

static void run_line(char *line)
{
    char *p = line;
    while (p) {
        char *semi = strchr(p, ';');
        if (semi) *semi = '\0';
        last_status = run_segment(p);
        if (!semi) break;
        p = semi + 1;
    }
}

/* ============================================================================
 * Shell main loop
 * ============================================================================ */

int main(int argc, char *argv[], char *envp[])
{
    (void)argc;
    (void)argv;
    (void)envp;

    /* Sensible environment defaults. */
    if (!getenv("PATH"))
        setenv("PATH", "/bin", 0);
    if (!getenv("HOME"))
        setenv("HOME", "/", 0);
    if (!getenv("SHELL"))
        setenv("SHELL", "/bin/sh", 0);

    /* Print /etc/motd if present. */
    int motd = open("/etc/motd", O_RDONLY);
    if (motd >= 0) {
        char buf[512];
        long r;
        while ((r = read(motd, buf, sizeof(buf))) > 0)
            write(STDOUT_FILENO, buf, (size_t)r);
        close(motd);
    } else {
        printf("\nScepter OS - /bin/sh (type 'help' for builtins)\n");
    }

    /* Non-interactive mode: sh SCRIPT [ARG...] */
    if (argc > 1) {
        setenv("0", argv[1], 1);
        return run_script_file(argv[1]);
    }

    for (;;) {
        char cwd[256];
        char line[MAX_LINE];

        reap_jobs();

        /* The shell does its own line editing: raw mode while reading. */
        set_term_raw();

        if (getcwd(cwd, sizeof cwd))
            printf("%s $ ", cwd);
        else
            printf("$ ");

        int r = read_line(line, sizeof line);
        if (r < 0) {
            write(STDOUT_FILENO, "\n", 1);
            set_term_cooked();   /* leave the tty in a sane state */
            break;
        }
        if (r == 0)
            continue;
        /* Children get the cooked terminal (canonical + echo). */
        set_term_cooked();
        run_line(line);
    }

    printf("shell exiting\n");
    return 0;
}
