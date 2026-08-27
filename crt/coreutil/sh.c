/* ============================================================================
 * sh - an interactive shell for Scepter OS
 *
 * Features:
 *   - prompt with the current directory
 *   - line editing (echo, backspace, Ctrl-C aborts the line, Ctrl-D exits)
 *   - tokenizer honouring single/double quotes and backslash escapes
 *   - command separation with ';', pipelines with '|'
 *   - redirection '<', '>', '>>'
 *   - background jobs with '&' (reaped at the next prompt with WNOHANG)
 *   - builtins: cd, pwd, echo, exit, clear, help, export, unset
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

#define MAX_LINE   512
#define MAX_WORDS  96
#define MAX_ARGS   32
#define MAX_STAGES 16
#define MAX_JOBS   16

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
         * prompt.  Never overwrite it: a child (e.g. tertest) may have
         * left the tty in raw mode, and we must not treat that as the
         * "cooked" state we restore for the next foreground job. */
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

static int is_builtin(const char *cmd)
{
    return strcmp(cmd, "cd") == 0 || strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "echo") == 0 || strcmp(cmd, "exit") == 0 ||
           strcmp(cmd, "clear") == 0 || strcmp(cmd, "help") == 0 ||
           strcmp(cmd, "export") == 0 || strcmp(cmd, "unset") == 0;
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
        printf("syntax:   cmd | cmd    (pipe)\n");
        printf("          cmd < f      (input redirection)\n");
        printf("          cmd > f, >> f (output redirection)\n");
        printf("          cmd &        (background)\n");
        printf("          cmd ; cmd    (sequential)\n");
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

    return 1;
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

    if (WIFSIGNALED(status) && WTERMSIG(status) != 0)
        fprintf(stderr, "sh: %s: terminated by signal %d\n", c->argv[0], WTERMSIG(status));
    return status;
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
        return status;
    }

    printf("[%d]\n", pids[n - 1]);
    add_job(pids[n - 1]);
    return 0;
}

/* ============================================================================
 * Execute one ';'-separated segment.
 * ============================================================================ */

/* Find the next unquoted "&&" in the string (quote-aware). */
static char *find_double_amp(char *p)
{
    int quote = 0;
    for (; *p; p++) {
        if (*p == '\'' && quote != 2)       quote = quote ? 0 : 1;
        else if (*p == '"' && quote != 1)    quote = quote ? 0 : 2;
        else if (*p == '\\' && quote != 1) { p++; }
        else if (quote == 0 && p[0] == '&' && p[1] == '&')
            return p;
    }
    return NULL;
}

/* Run a single command (simple or pipeline). Returns the exit status. */
static int run_subsegment(char *seg)
{
    char *words[MAX_WORDS];
    int n = tokenize(seg, words, MAX_WORDS);
    if (n == 0)
        return 0;

    command_t cmds[MAX_STAGES];
    int ns = build_commands(words, n, cmds, MAX_STAGES);
    if (ns <= 0)
        return 127;

    if (ns == 1)
        return run_simple(&cmds[0]);
    return run_pipeline(cmds, ns);
}

/* Execute a ';'-segment, honouring '&&' short-circuiting. */
static int run_segment(char *seg)
{
    int status = 0;
    char *p = seg;
    while (p) {
        char *amp = find_double_amp(p);
        if (amp) *amp = '\0';
        if (status == 0)
            status = run_subsegment(p);
        if (!amp) break;
        p = amp + 2;
    }
    return status;
}

static void run_line(char *line)
{
    char *p = line;
    while (p) {
        char *semi = strchr(p, ';');
        if (semi) *semi = '\0';
        run_segment(p);
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

    printf("\nScepter OS - /bin/sh (type 'help' for builtins)\n");

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
