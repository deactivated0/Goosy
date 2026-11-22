/*
 * Goot — Linux POSIX PTY, Safe Process Spawner & Environment Helper Bridge
 * Minimal, auditable C99 implementation for POSIX pseudoterminals.
 * Zero dynamic heap allocations on hot paths; never constructs shell commands.
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "goot_native.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int     g_master_fd = -1;
static pid_t   g_child_pid = -1;
static int32_t g_exit_code = 0;
static bool    g_child_exited = false;

static int set_cloexec_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        return -1;
    }
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

static const char *resolve_default_shell(char *buf, size_t cap) {
    const char *env_shell = getenv("SHELL");
    if (env_shell && env_shell[0] == '/' && access(env_shell, X_OK) == 0) {
        size_t len = strlen(env_shell);
        if (len < cap) {
            memcpy(buf, env_shell, len + 1);
            return buf;
        }
    }
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_shell && pw->pw_shell[0] == '/' && access(pw->pw_shell, X_OK) == 0) {
        size_t len = strlen(pw->pw_shell);
        if (len < cap) {
            memcpy(buf, pw->pw_shell, len + 1);
            return buf;
        }
    }
    if (access("/bin/sh", X_OK) == 0) {
        return "/bin/sh";
    }
    return "/usr/bin/sh";
}

/* -------------------------------------------------------------------------
 * Command-line Argument Parsing (/proc/self/cmdline)
 * ------------------------------------------------------------------------- */
static bool    g_cmdline_parsed   = false;
static int32_t g_cmdline_cols     = 0;
static int32_t g_cmdline_rows     = 0;
static bool    g_cmdline_has_size = false;
static bool    g_cmdline_hold     = false;
static char    g_cmdline_title[256] = {0};
static char    g_cmdline_cwd[1024]  = {0};
static char   *g_cmdline_exec_argv[128];
static int     g_cmdline_exec_argc  = 0;
static char    g_cmdline_raw_buf[4096];

static void parse_cmdline_if_needed(void) {
    if (g_cmdline_parsed) return;
    g_cmdline_parsed = true;

    int cmd_fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (cmd_fd < 0) return;
    ssize_t n = read(cmd_fd, g_cmdline_raw_buf, sizeof(g_cmdline_raw_buf) - 1);
    close(cmd_fd);
    if (n <= 0) return;
    g_cmdline_raw_buf[n] = '\0';

    char *p = g_cmdline_raw_buf;
    char *end = g_cmdline_raw_buf + n;

    /* Skip argv[0] */
    while (p < end && *p != '\0') p++;
    if (p < end) p++;

    while (p < end && *p != '\0') {
        if (strncmp(p, "--window-size-chars=", 20) == 0) {
            int c = 0, r = 0;
            if (sscanf(p + 20, "%dx%d", &c, &r) == 2 && c >= 10 && r >= 3) {
                g_cmdline_cols = c;
                g_cmdline_rows = r;
                g_cmdline_has_size = true;
            }
        } else if (strcmp(p, "--window-size-chars") == 0 || strcmp(p, "-w") == 0) {
            while (p < end && *p != '\0') p++;
            if (p < end) p++;
            if (p < end && *p != '\0') {
                int c = 0, r = 0;
                if (sscanf(p, "%dx%d", &c, &r) == 2 && c >= 10 && r >= 3) {
                    g_cmdline_cols = c;
                    g_cmdline_rows = r;
                    g_cmdline_has_size = true;
                }
            } else {
                break;
            }
        } else if (strncmp(p, "-w=", 3) == 0) {
            int c = 0, r = 0;
            if (sscanf(p + 3, "%dx%d", &c, &r) == 2 && c >= 10 && r >= 3) {
                g_cmdline_cols = c;
                g_cmdline_rows = r;
                g_cmdline_has_size = true;
            }
        } else if (strncmp(p, "--title=", 8) == 0) {
            strncpy(g_cmdline_title, p + 8, sizeof(g_cmdline_title) - 1);
        } else if (strcmp(p, "--title") == 0 || strcmp(p, "-T") == 0) {
            while (p < end && *p != '\0') p++;
            if (p < end) p++;
            if (p < end && *p != '\0') {
                strncpy(g_cmdline_title, p, sizeof(g_cmdline_title) - 1);
            } else {
                break;
            }
        } else if (strncmp(p, "-T=", 3) == 0) {
            strncpy(g_cmdline_title, p + 3, sizeof(g_cmdline_title) - 1);
        } else if (strncmp(p, "--working-directory=", 20) == 0) {
            strncpy(g_cmdline_cwd, p + 20, sizeof(g_cmdline_cwd) - 1);
        } else if (strcmp(p, "--working-directory") == 0 || strcmp(p, "-D") == 0) {
            while (p < end && *p != '\0') p++;
            if (p < end) p++;
            if (p < end && *p != '\0') {
                strncpy(g_cmdline_cwd, p, sizeof(g_cmdline_cwd) - 1);
            } else {
                break;
            }
        } else if (strncmp(p, "-D=", 3) == 0) {
            strncpy(g_cmdline_cwd, p + 3, sizeof(g_cmdline_cwd) - 1);
        } else if (strcmp(p, "--hold") == 0) {
            g_cmdline_hold = true;
        } else if (strcmp(p, "-e") == 0 || strcmp(p, "--") == 0) {
            while (p < end && *p != '\0') p++;
            if (p < end) p++;
            while (p < end && *p != '\0' && g_cmdline_exec_argc < 120) {
                g_cmdline_exec_argv[g_cmdline_exec_argc++] = p;
                while (p < end && *p != '\0') p++;
                if (p < end) p++;
            }
            break;
        } else if (p[0] != '-') {
            while (p < end && *p != '\0' && g_cmdline_exec_argc < 120) {
                g_cmdline_exec_argv[g_cmdline_exec_argc++] = p;
                while (p < end && *p != '\0') p++;
                if (p < end) p++;
            }
            break;
        }

        while (p < end && *p != '\0') p++;
        if (p < end) p++;
    }

    g_cmdline_exec_argv[g_cmdline_exec_argc] = NULL;
}

int32_t goot_cmdline_cols(void) {
    parse_cmdline_if_needed();
    return g_cmdline_cols;
}

int32_t goot_cmdline_rows(void) {
    parse_cmdline_if_needed();
    return g_cmdline_rows;
}

int32_t goot_cmdline_has_custom_size(void) {
    parse_cmdline_if_needed();
    return g_cmdline_has_size ? 1 : 0;
}

int32_t goot_cmdline_hold(void) {
    parse_cmdline_if_needed();
    return g_cmdline_hold ? 1 : 0;
}

const char *goot_cmdline_title(void) {
    parse_cmdline_if_needed();
    return g_cmdline_title[0] != '\0' ? g_cmdline_title : NULL;
}

const char *goot_cmdline_cwd(void) {
    parse_cmdline_if_needed();
    return g_cmdline_cwd[0] != '\0' ? g_cmdline_cwd : NULL;
}

int32_t goot_pty_spawn(goot_const_bytes_t shell_override,
                       goot_const_bytes_t term_env,
                       int32_t cols, int32_t rows,
                       int32_t width_px, int32_t height_px) {
    if (cols <= 0 || cols > 4096 || rows <= 0 || rows > 4096) {
        return -1;
    }
    if (width_px < 0 || height_px < 0) {
        width_px = 0;
        height_px = 0;
    }

    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        return -1;
    }
    if (grantpt(master_fd) != 0 || unlockpt(master_fd) != 0) {
        close(master_fd);
        return -1;
    }

    char slave_name[256];
    if (ptsname_r(master_fd, slave_name, sizeof(slave_name)) != 0) {
        close(master_fd);
        return -1;
    }

    /* Set initial PTY dimensions on master_fd BEFORE fork() so later parent
       TIOCSWINSZ calls can never race with child startup. */
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    ws.ws_xpixel = (unsigned short)(width_px > 65535 ? 65535 : width_px);
    ws.ws_ypixel = (unsigned short)(height_px > 65535 ? 65535 : height_px);
    (void)ioctl(master_fd, TIOCSWINSZ, &ws);

    char shell_buf[512];
    const char *shell_path = NULL;
    if (shell_override.data && shell_override.len > 0 &&
        shell_override.len < (int64_t)sizeof(shell_buf)) {
        memcpy(shell_buf, shell_override.data, (size_t)shell_override.len);
        shell_buf[shell_override.len] = '\0';
        if (shell_buf[0] == '/' && access(shell_buf, X_OK) == 0) {
            shell_path = shell_buf;
        }
    }
    if (!shell_path) {
        shell_path = resolve_default_shell(shell_buf, sizeof(shell_buf));
    }

    char term_buf[128];
    const char *term_str = "xterm-256color";
    if (term_env.data && term_env.len > 0 && term_env.len < (int64_t)sizeof(term_buf)) {
        memcpy(term_buf, term_env.data, (size_t)term_env.len);
        term_buf[term_env.len] = '\0';
        term_str = term_buf;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        signal(SIGCHLD, SIG_DFL);
        signal(SIGHUP,  SIG_DFL);
        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGALRM, SIG_DFL);

        sigset_t empty_mask;
        sigemptyset(&empty_mask);
        sigprocmask(SIG_SETMASK, &empty_mask, NULL);

        close(master_fd);
        if (setsid() < 0) {
            _exit(126);
        }

        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) {
            _exit(126);
        }
        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
            _exit(126);
        }

        if (dup2(slave_fd, STDIN_FILENO) < 0 ||
            dup2(slave_fd, STDOUT_FILENO) < 0 ||
            dup2(slave_fd, STDERR_FILENO) < 0) {
            _exit(126);
        }
        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        unsetenv("COLUMNS");
        unsetenv("LINES");
        unsetenv("TERMCAP");
        setenv("TERM", term_str, 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("TERM_PROGRAM", "goosy", 1);
        setenv("TERM_PROGRAM_VERSION", "0.2.0", 1);

        /* Apply pre-parsed CLI arguments (--working-directory, -e, trailing command) */
        parse_cmdline_if_needed();

        if (g_cmdline_cwd[0] != '\0') {
            const char *target_cwd = g_cmdline_cwd;
            if (target_cwd[0] == '~') {
                const char *h = getenv("HOME");
                if (h && target_cwd[1] == '/') {
                    char expanded[1024];
                    snprintf(expanded, sizeof(expanded), "%s%s", h, target_cwd + 1);
                    (void)chdir(expanded);
                } else if (h && target_cwd[1] == '\0') {
                    (void)chdir(h);
                }
            } else {
                (void)chdir(target_cwd);
            }
        }

        if (g_cmdline_exec_argc > 0) {
            execvp(g_cmdline_exec_argv[0], g_cmdline_exec_argv);
            _exit(127);
        }

        const char *base = strrchr(shell_path, '/');
        const char *arg0 = base ? base + 1 : shell_path;
        char *const argv[] = { (char *)arg0, NULL };
        execv(shell_path, argv);
        _exit(127);
    }

    if (set_cloexec_nonblock(master_fd) != 0) {
        close(master_fd);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }

    g_master_fd = master_fd;
    g_child_pid = pid;
    g_exit_code = 0;
    g_child_exited = false;
    return 0;
}

int32_t goot_pty_fd(void) {
    return (int32_t)g_master_fd;
}

int32_t goot_pty_child_pid(void) {
    return (int32_t)g_child_pid;
}

int64_t goot_pty_read(goot_bytes_t buf) {
    if (g_master_fd < 0 || !buf.data || buf.len <= 0) {
        return -1;
    }
    ssize_t n = read(g_master_fd, buf.data, (size_t)buf.len);
    if (n > 0) {
        return (int64_t)n;
    }
    if (n == 0) {
        return 0;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return -2;
    }
    if (errno == EIO) {
        return 0;
    }
    return -1;
}

#define PTY_INITIAL_CAP (64 * 1024)
#define PTY_MAX_CAP     (16 * 1024 * 1024)

static uint8_t *g_write_buf  = NULL;
static size_t   g_write_cap  = 0;
static size_t   g_write_head = 0;
static size_t   g_write_tail = 0;

static size_t pty_write_buf_len(void) {
    if (!g_write_buf || g_write_cap == 0) return 0;
    if (g_write_tail >= g_write_head) return g_write_tail - g_write_head;
    return g_write_cap - g_write_head + g_write_tail;
}

static size_t pty_write_buf_free(void) {
    if (!g_write_buf || g_write_cap == 0) return 0;
    return g_write_cap - 1 - pty_write_buf_len();
}

static bool pty_write_buf_grow(size_t needed) {
    if (needed > PTY_MAX_CAP) needed = PTY_MAX_CAP;
    size_t new_cap = g_write_cap ? g_write_cap : PTY_INITIAL_CAP;
    while (new_cap < needed) {
        new_cap *= 2;
        if (new_cap > PTY_MAX_CAP) { new_cap = PTY_MAX_CAP; break; }
    }
    if (new_cap <= g_write_cap) return false;

    uint8_t *new_buf = (uint8_t *)malloc(new_cap);
    if (!new_buf) return false;

    size_t len = 0;
    if (g_write_buf) {
        if (g_write_tail >= g_write_head) {
            len = g_write_tail - g_write_head;
            if (len > 0) memcpy(new_buf, g_write_buf + g_write_head, len);
        } else {
            size_t part1 = g_write_cap - g_write_head;
            memcpy(new_buf, g_write_buf + g_write_head, part1);
            memcpy(new_buf + part1, g_write_buf, g_write_tail);
            len = part1 + g_write_tail;
        }
        free(g_write_buf);
    }
    g_write_buf = new_buf;
    g_write_cap = new_cap;
    g_write_head = 0;
    g_write_tail = len;
    return true;
}

int32_t goot_pty_has_pending_write(void) {
    return pty_write_buf_len() > 0 ? 1 : 0;
}

int64_t goot_pty_flush(void) {
    if (g_master_fd < 0 || !g_write_buf || g_write_cap == 0) return -1;
    while (g_write_head != g_write_tail) {
        size_t chunk = (g_write_tail > g_write_head) ? (g_write_tail - g_write_head) : (g_write_cap - g_write_head);
        ssize_t n = write(g_master_fd, g_write_buf + g_write_head, chunk);
        if (n > 0) {
            g_write_head = (g_write_head + (size_t)n) % g_write_cap;
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                break;
            }
            return -1;
        } else {
            break;
        }
    }
    return (int64_t)pty_write_buf_len();
}

int64_t goot_pty_write(goot_const_bytes_t data) {
    if (g_master_fd < 0 || !data.data || data.len <= 0) {
        return 0;
    }
    if (!g_write_buf) {
        g_write_buf = (uint8_t *)malloc(PTY_INITIAL_CAP);
        g_write_cap = g_write_buf ? PTY_INITIAL_CAP : 0;
        g_write_head = 0;
        g_write_tail = 0;
    }
    if (g_write_head != g_write_tail) {
        goot_pty_flush();
    }
    size_t written = 0;
    if (g_write_head == g_write_tail) {
        ssize_t n = write(g_master_fd, data.data, (size_t)data.len);
        if (n > 0) {
            written = (size_t)n;
        } else if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            return -1;
        }
    }
    if (written < (size_t)data.len) {
        size_t rem = (size_t)data.len - written;
        if (rem > pty_write_buf_free()) {
            size_t needed = pty_write_buf_len() + rem + 1;
            pty_write_buf_grow(needed);
        }
        size_t avail = pty_write_buf_free();
        size_t to_queue = rem < avail ? rem : avail;
        if (to_queue > 0 && g_write_buf) {
            size_t first = g_write_cap - g_write_tail;
            if (to_queue <= first) {
                memcpy(g_write_buf + g_write_tail, data.data + written, to_queue);
                g_write_tail = (g_write_tail + to_queue) % g_write_cap;
            } else {
                memcpy(g_write_buf + g_write_tail, data.data + written, first);
                memcpy(g_write_buf, data.data + written + first, to_queue - first);
                g_write_tail = to_queue - first;
            }
            written += to_queue;
        }
    }
    return (int64_t)written;
}

int32_t goot_pty_resize(int32_t cols, int32_t rows, int32_t width_px, int32_t height_px) {
    if (g_master_fd < 0 || cols <= 0 || cols > 4096 || rows <= 0 || rows > 4096) {
        return -1;
    }
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    ws.ws_xpixel = (unsigned short)(width_px > 0 && width_px <= 65535 ? width_px : 0);
    ws.ws_ypixel = (unsigned short)(height_px > 0 && height_px <= 65535 ? height_px : 0);
    if (ioctl(g_master_fd, TIOCSWINSZ, &ws) < 0) {
        return -1;
    }
    return 0;
}

int32_t goot_pty_check_child(void) {
    if (g_child_exited) {
        return 1;
    }
    if (g_child_pid <= 0) {
        return 1;
    }
    int status = 0;
    pid_t r = waitpid(g_child_pid, &status, WNOHANG);
    if (r == 0) {
        return 0;
    }
    if (r == g_child_pid) {
        g_child_exited = true;
        if (WIFEXITED(status)) {
            g_exit_code = (int32_t)WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            g_exit_code = 128 + (int32_t)WTERMSIG(status);
        } else {
            g_exit_code = 0;
        }
        g_child_pid = -1;
        return 1;
    }
    if (errno == ECHILD) {
        g_child_exited = true;
        g_child_pid = -1;
        return 1;
    }
    return -1;
}

int32_t goot_pty_exit_code(void) {
    return g_exit_code;
}

void goot_pty_close(void) {
    if (g_write_buf) {
        free(g_write_buf);
        g_write_buf = NULL;
    }
    g_write_cap = 0;
    g_write_head = 0;
    g_write_tail = 0;

    if (g_master_fd >= 0) {
        close(g_master_fd);
        g_master_fd = -1;
    }
    if (g_child_pid > 0 && !g_child_exited) {
        kill(-g_child_pid, SIGHUP);
        kill(g_child_pid, SIGHUP);
        int status = 0;
        for (int i = 0; i < 10; i++) {
            pid_t r = waitpid(g_child_pid, &status, WNOHANG);
            if (r == g_child_pid || (r < 0 && errno == ECHILD)) {
                g_child_exited = true;
                g_child_pid = -1;
                return;
            }
            struct timespec ts = { 0, 5000000L };
            nanosleep(&ts, NULL);
        }
        kill(g_child_pid, SIGTERM);
        waitpid(g_child_pid, &status, WNOHANG);
        g_child_exited = true;
        g_child_pid = -1;
    }
}

/* -------------------------------------------------------------------------
 * Safe New-Terminal Spawner (Ctrl+Shift+N with OSC 7 CWD) & URL Opener
 * ------------------------------------------------------------------------- */
int32_t goot_spawn_self(goot_const_bytes_t cwd) {
    char exe_path[1024];
    ssize_t elen = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (elen <= 0) {
        return -1;
    }
    exe_path[elen] = '\0';

    char cwd_buf[1024];
    bool has_cwd = false;
    if (cwd.data && cwd.len > 0 && cwd.len < (int64_t)sizeof(cwd_buf)) {
        memcpy(cwd_buf, cwd.data, (size_t)cwd.len);
        cwd_buf[cwd.len] = '\0';
        has_cwd = (cwd_buf[0] == '/');
    }

    /* Double-fork so the spawned terminal is adopted by init and never zombies */
    pid_t p1 = fork();
    if (p1 < 0) return -1;
    if (p1 == 0) {
        if (setsid() < 0) _exit(126);
        pid_t p2 = fork();
        if (p2 < 0) _exit(126);
        if (p2 > 0) _exit(0);

        sigset_t empty_mask;
        sigemptyset(&empty_mask);
        sigprocmask(SIG_SETMASK, &empty_mask, NULL);

        if (has_cwd) {
            (void)chdir(cwd_buf);
        }
        char *const argv[] = { exe_path, NULL };
        execv(exe_path, argv);
        _exit(127);
    }
    int st = 0;
    (void)waitpid(p1, &st, 0);
    return 0;
}

int32_t goot_validate_url(goot_const_bytes_t url) {
    if (!url.data || url.len < 8 || url.len >= 2048) {
        return 0;
    }
    /* Allow only safe, explicit URL schemes */
    bool ok_scheme = false;
    if (url.len >= 8 && memcmp(url.data, "https://", 8) == 0) ok_scheme = true;
    else if (url.len >= 7 && memcmp(url.data, "http://", 7) == 0) ok_scheme = true;
    else if (url.len >= 6 && memcmp(url.data, "ftp://", 6) == 0) ok_scheme = true;
    else if (url.len >= 7 && memcmp(url.data, "file://", 7) == 0) ok_scheme = true;
    if (!ok_scheme) return 0;

    for (int64_t i = 0; i < url.len; i++) {
        uint8_t c = url.data[i];
        if (c <= 0x20 || c == 0x7F) return 0;
    }
    return 1;
}

int32_t goot_open_url(goot_const_bytes_t url) {
    if (!goot_validate_url(url)) {
        return -1;
    }
    char url_buf[2048];
    memcpy(url_buf, url.data, (size_t)url.len);
    url_buf[url.len] = '\0';

    pid_t p1 = fork();
    if (p1 < 0) return -1;
    if (p1 == 0) {
        if (setsid() < 0) _exit(126);
        pid_t p2 = fork();
        if (p2 < 0) _exit(126);
        if (p2 > 0) _exit(0);

        sigset_t empty_mask;
        sigemptyset(&empty_mask);
        sigprocmask(SIG_SETMASK, &empty_mask, NULL);

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }

        /* Never invoke a shell: execvp xdg-open directly with validated URL */
        char *const argv[] = { "xdg-open", url_buf, NULL };
        execvp("xdg-open", argv);
        _exit(127);
    }
    int st = 0;
    (void)waitpid(p1, &st, 0);
    return 0;
}

/* -------------------------------------------------------------------------
 * Safe Environment & Config File Read Helpers
 * ------------------------------------------------------------------------- */
int64_t goot_env_get(goot_const_bytes_t name, goot_bytes_t out_buf) {
    if (!name.data || name.len <= 0 || name.len >= 256 || !out_buf.data || out_buf.len <= 0) {
        return -1;
    }
    char key[256];
    memcpy(key, name.data, (size_t)name.len);
    key[name.len] = '\0';
    const char *val = getenv(key);
    if (!val) {
        return -1;
    }
    size_t vlen = strlen(val);
    if ((int64_t)vlen > out_buf.len) {
        vlen = (size_t)out_buf.len;
    }
    memcpy(out_buf.data, val, vlen);
    return (int64_t)vlen;
}

int64_t goot_file_read(goot_const_bytes_t path, goot_bytes_t out_buf) {
    if (!path.data || path.len <= 0 || path.len >= 1024 || !out_buf.data || out_buf.len <= 0) {
        return -1;
    }
    char pbuf[1024];
    memcpy(pbuf, path.data, (size_t)path.len);
    pbuf[path.len] = '\0';

    int fd = open(pbuf, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    int64_t total = 0;
    while (total < out_buf.len) {
        ssize_t n = read(fd, out_buf.data + total, (size_t)(out_buf.len - total));
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (n == 0) break;
        total += (int64_t)n;
    }
    close(fd);
    return total;
}

/* -------------------------------------------------------------------------
 * Persistent Window Geometry State (~/.local/state/goosy/window_geometry)
 * ------------------------------------------------------------------------- */
static void get_state_geometry_path(char *out, size_t cap) {
    const char *state_home = getenv("XDG_STATE_HOME");
    if (state_home && state_home[0] == '/') {
        snprintf(out, cap, "%s/goosy/window_geometry", state_home);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] != '/') home = "/tmp";
        snprintf(out, cap, "%s/.local/state/goosy/window_geometry", home);
    }
}

static void get_legacy_state_geometry_path(char *out, size_t cap) {
    const char *state_home = getenv("XDG_STATE_HOME");
    if (state_home && state_home[0] == '/') {
        snprintf(out, cap, "%s/goot/window_geometry", state_home);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] != '/') home = "/tmp";
        snprintf(out, cap, "%s/.local/state/goot/window_geometry", home);
    }
}

static int32_t g_saved_cols = 0;
static int32_t g_saved_rows = 0;
static bool    g_state_loaded = false;

static void load_state_if_needed(void) {
    if (g_state_loaded) return;
    g_state_loaded = true;
    char path[1024];
    get_state_geometry_path(path, sizeof(path));
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        get_legacy_state_geometry_path(path, sizeof(path));
        fd = open(path, O_RDONLY | O_CLOEXEC);
    }
    if (fd < 0) return;
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    int c = 0, r = 0;
    if (sscanf(buf, "%d %d", &c, &r) == 2) {
        if (c >= 20 && c <= 500 && r >= 5 && r <= 200) {
            g_saved_cols = (int32_t)c;
            g_saved_rows = (int32_t)r;
        }
    }
}

int32_t goot_state_load_cols(void) {
    load_state_if_needed();
    return g_saved_cols;
}

int32_t goot_state_load_rows(void) {
    load_state_if_needed();
    return g_saved_rows;
}

void goot_state_save_geometry(int32_t cols, int32_t rows) {
    if (cols < 20 || rows < 5 || cols > 500 || rows > 200) return;
    char path[1024];
    get_state_geometry_path(path, sizeof(path));

    char dir[1024];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        char pdir[1024];
        strncpy(pdir, dir, sizeof(pdir) - 1);
        pdir[sizeof(pdir) - 1] = '\0';
        char *pslash = strrchr(pdir, '/');
        if (pslash) {
            *pslash = '\0';
            mkdir(pdir, 0755);
        }
        mkdir(dir, 0755);
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%d %d\n", (int)cols, (int)rows);
    if (len > 0) {
        (void)write(fd, buf, (size_t)len);
    }
    close(fd);
}
