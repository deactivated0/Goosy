/*
 * Goosy — Linux POSIX PTY, Safe Process Spawner & Environment Helper Bridge
 * Minimal, auditable C99 implementation for POSIX pseudoterminals.
 * Zero dynamic heap allocations on hot paths; never constructs shell commands.
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "goosy_native.h"

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
#include <sched.h>

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
static int32_t g_cmdline_renderer   = 0;

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
        } else if (strcmp(p, "--renderer-info") == 0) {
            goosy_renderer_print_info();
            exit(0);
        } else if (strncmp(p, "--renderer=", 11) == 0) {
            const char *val = p + 11;
            if (strcmp(val, "vulkan") == 0 || strcmp(val, "vk") == 0) {
                g_cmdline_renderer = 1;
            } else if (strcmp(val, "opengl") == 0 || strcmp(val, "gl") == 0 || strcmp(val, "gles") == 0) {
                g_cmdline_renderer = 2;
            } else if (strcmp(val, "pixman") == 0 || strcmp(val, "sw") == 0 || strcmp(val, "software") == 0) {
                g_cmdline_renderer = 3;
            } else {
                g_cmdline_renderer = 0;
            }
        } else if (strcmp(p, "--renderer") == 0 || strcmp(p, "-r") == 0) {
            while (p < end && *p != '\0') p++;
            if (p < end) p++;
            if (p < end && *p != '\0') {
                if (strcmp(p, "vulkan") == 0 || strcmp(p, "vk") == 0) {
                    g_cmdline_renderer = 1;
                } else if (strcmp(p, "opengl") == 0 || strcmp(p, "gl") == 0 || strcmp(p, "gles") == 0) {
                    g_cmdline_renderer = 2;
                } else if (strcmp(p, "pixman") == 0 || strcmp(p, "sw") == 0 || strcmp(p, "software") == 0) {
                    g_cmdline_renderer = 3;
                } else {
                    g_cmdline_renderer = 0;
                }
            } else {
                break;
            }
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

int32_t goosy_cmdline_cols(void) {
    parse_cmdline_if_needed();
    return g_cmdline_cols;
}

int32_t goosy_cmdline_rows(void) {
    parse_cmdline_if_needed();
    return g_cmdline_rows;
}

__attribute__((weak)) void goosy_renderer_print_info(void) {
    printf("Goosy Rendering Subsystem Info\n");
}

int32_t goosy_cmdline_has_custom_size(void) {
    parse_cmdline_if_needed();
    return g_cmdline_has_size ? 1 : 0;
}

int32_t goosy_cmdline_renderer(void) {
    parse_cmdline_if_needed();
    return g_cmdline_renderer;
}

int32_t goosy_cmdline_hold(void) {
    parse_cmdline_if_needed();
    return g_cmdline_hold ? 1 : 0;
}

const char *goosy_cmdline_title(void) {
    parse_cmdline_if_needed();
    return g_cmdline_title[0] != '\0' ? g_cmdline_title : NULL;
}

const char *goosy_cmdline_cwd(void) {
    parse_cmdline_if_needed();
    return g_cmdline_cwd[0] != '\0' ? g_cmdline_cwd : NULL;
}

int32_t goosy_pty_spawn(goosy_const_bytes_t shell_override,
                       goosy_const_bytes_t term_env,
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

int32_t goosy_pty_fd(void) {
    return (int32_t)g_master_fd;
}

int32_t goosy_pty_child_pid(void) {
    return (int32_t)g_child_pid;
}

int64_t goosy_pty_read(goosy_bytes_t buf) {
    if (g_master_fd < 0 || !buf.data || buf.len <= 0) {
        return -1;
    }
    size_t total = 0;
    int retries = 0;
    while (total < (size_t)buf.len) {
        ssize_t n = read(g_master_fd, buf.data + total, (size_t)buf.len - total);
        if (n > 0) {
            total += (size_t)n;
            retries = 0;
            continue;
        }
        if (n == 0) {
            return total > 0 ? (int64_t)total : 0;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (total > 0 && total < (size_t)buf.len && retries < 4) {
                retries++;
                sched_yield();
                continue;
            }
            break;
        }
        if (errno == EIO) {
            return total > 0 ? (int64_t)total : 0;
        }
        return total > 0 ? (int64_t)total : -1;
    }
    if (total > 0) {
        return (int64_t)total;
    }
    return -2;
}

#define PTY_INITIAL_CAP (64 * 1024)
#define PTY_MAX_CAP     (16 * 1024 * 1024)

typedef struct pty_chunk {
    struct pty_chunk *next;
    size_t len;
    size_t pos;
    uint8_t data[];
} pty_chunk_t;

static uint8_t     *g_write_buf      = NULL;
static size_t       g_write_cap      = 0;
static size_t       g_write_head     = 0;
static size_t       g_write_tail     = 0;
static pty_chunk_t *g_overflow_head  = NULL;
static pty_chunk_t *g_overflow_tail  = NULL;

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

int32_t goosy_pty_has_pending_write(void) {
    return (pty_write_buf_len() > 0 || g_overflow_head != NULL) ? 1 : 0;
}

int64_t goosy_pty_flush(void) {
    if (g_master_fd < 0) return -1;

    /* 1. Drain circular buffer to master PTY */
    if (g_write_buf && g_write_cap > 0) {
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
    }

    /* 2. Pump pending overflow chunks into circular buffer */
    while (g_overflow_head && pty_write_buf_free() > 0) {
        size_t avail = pty_write_buf_free();
        size_t chunk_rem = g_overflow_head->len - g_overflow_head->pos;
        size_t to_move = chunk_rem < avail ? chunk_rem : avail;
        size_t first = g_write_cap - g_write_tail;
        if (to_move <= first) {
            memcpy(g_write_buf + g_write_tail, g_overflow_head->data + g_overflow_head->pos, to_move);
            g_write_tail = (g_write_tail + to_move) % g_write_cap;
        } else {
            memcpy(g_write_buf + g_write_tail, g_overflow_head->data + g_overflow_head->pos, first);
            memcpy(g_write_buf, g_overflow_head->data + g_overflow_head->pos + first, to_move - first);
            g_write_tail = to_move - first;
        }
        g_overflow_head->pos += to_move;
        if (g_overflow_head->pos >= g_overflow_head->len) {
            pty_chunk_t *next = g_overflow_head->next;
            free(g_overflow_head);
            g_overflow_head = next;
            if (!g_overflow_head) g_overflow_tail = NULL;
        }

        /* Attempt to flush newly moved bytes immediately */
        while (g_write_head != g_write_tail) {
            size_t chunk = (g_write_tail > g_write_head) ? (g_write_tail - g_write_head) : (g_write_cap - g_write_head);
            ssize_t n = write(g_master_fd, g_write_buf + g_write_head, chunk);
            if (n > 0) {
                g_write_head = (g_write_head + (size_t)n) % g_write_cap;
            } else {
                break;
            }
        }
    }

    return (int64_t)(pty_write_buf_len() + (g_overflow_head ? 1024 : 0));
}

int64_t goosy_pty_write(goosy_const_bytes_t data) {
    if (g_master_fd < 0 || !data.data || data.len <= 0) {
        return 0;
    }
    if (!g_write_buf) {
        g_write_buf = (uint8_t *)malloc(PTY_INITIAL_CAP);
        g_write_cap = g_write_buf ? PTY_INITIAL_CAP : 0;
        g_write_head = 0;
        g_write_tail = 0;
    }
    if (g_write_head != g_write_tail || g_overflow_head != NULL) {
        goosy_pty_flush();
    }
    size_t written = 0;
    if (g_write_head == g_write_tail && g_overflow_head == NULL) {
        ssize_t n = write(g_master_fd, data.data, (size_t)data.len);
        if (n > 0) {
            written = (size_t)n;
        } else if (n < 0 && (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            return -1;
        }
    }
    if (written < (size_t)data.len) {
        size_t rem = (size_t)data.len - written;
        if (rem > pty_write_buf_free() && pty_write_buf_free() < PTY_MAX_CAP) {
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
        /* If circular buffer is completely full, queue in overflow chain (zero data loss) */
        if (written < (size_t)data.len) {
            size_t overflow_bytes = (size_t)data.len - written;
            pty_chunk_t *chunk = (pty_chunk_t *)malloc(sizeof(pty_chunk_t) + overflow_bytes);
            if (chunk) {
                chunk->next = NULL;
                chunk->len = overflow_bytes;
                chunk->pos = 0;
                memcpy(chunk->data, data.data + written, overflow_bytes);
                if (g_overflow_tail) {
                    g_overflow_tail->next = chunk;
                    g_overflow_tail = chunk;
                } else {
                    g_overflow_head = chunk;
                    g_overflow_tail = chunk;
                }
                written += overflow_bytes;
            }
        }
    }
    return (int64_t)written;
}

int32_t goosy_pty_resize(int32_t cols, int32_t rows, int32_t width_px, int32_t height_px) {
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

int32_t goosy_pty_check_child(void) {
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

int32_t goosy_pty_exit_code(void) {
    return g_exit_code;
}

void goosy_cells_scroll_up(goosy_cells_slice_t cells, int32_t cols, int32_t top, int32_t bottom, int32_t count) {
    if (!cells.data || cols <= 0 || count <= 0 || top < 0 || bottom < top || count > (bottom - top + 1)) return;
    int32_t row_count = bottom - count + 1 - top;
    if (row_count <= 0) return;
    size_t cell_sz = sizeof(goosy_cell_t);
    size_t copy_bytes = (size_t)row_count * (size_t)cols * cell_sz;
    uint8_t *base = (uint8_t *)cells.data;
    uint8_t *dst = base + (size_t)top * (size_t)cols * cell_sz;
    uint8_t *src = base + (size_t)(top + count) * (size_t)cols * cell_sz;
    memmove(dst, src, copy_bytes);
}

void goosy_cells_scroll_down(goosy_cells_slice_t cells, int32_t cols, int32_t top, int32_t bottom, int32_t count) {
    if (!cells.data || cols <= 0 || count <= 0 || top < 0 || bottom < top || count > (bottom - top + 1)) return;
    int32_t row_count = bottom - count + 1 - top;
    if (row_count <= 0) return;
    size_t cell_sz = sizeof(goosy_cell_t);
    size_t copy_bytes = (size_t)row_count * (size_t)cols * cell_sz;
    uint8_t *base = (uint8_t *)cells.data;
    uint8_t *dst = base + (size_t)(top + count) * (size_t)cols * cell_sz;
    uint8_t *src = base + (size_t)top * (size_t)cols * cell_sz;
    memmove(dst, src, copy_bytes);
}

void goosy_screen_scroll_up(goosy_cells_slice_t cells, int32_t cols, int32_t top, int32_t bottom, int32_t count,
                            goosy_bytes_t prompt_rows, goosy_bytes_t wrapped_rows, goosy_bytes_t dirty_rows,
                            uint32_t fg, uint32_t bg) {
    if (!cells.data || cols <= 0 || count <= 0 || top < 0 || bottom < top || count > (bottom - top + 1)) return;
    int32_t row_count = bottom - count + 1 - top;
    size_t cell_sz = sizeof(goosy_cell_t);
    uint8_t *base = (uint8_t *)cells.data;

    if (row_count > 0) {
        size_t copy_bytes = (size_t)row_count * (size_t)cols * cell_sz;
        uint8_t *dst = base + (size_t)top * (size_t)cols * cell_sz;
        uint8_t *src = base + (size_t)(top + count) * (size_t)cols * cell_sz;
        memmove(dst, src, copy_bytes);

        if (prompt_rows.data && prompt_rows.len > 0) {
            int32_t limit = (int32_t)prompt_rows.len;
            int32_t p_cnt = (top + count + row_count <= limit) ? row_count : (limit > top + count ? limit - (top + count) : 0);
            if (p_cnt > 0) memmove(prompt_rows.data + top, prompt_rows.data + top + count, (size_t)p_cnt);
        }
        if (wrapped_rows.data && wrapped_rows.len > 0) {
            int32_t limit = (int32_t)wrapped_rows.len;
            int32_t w_cnt = (top + count + row_count <= limit) ? row_count : (limit > top + count ? limit - (top + count) : 0);
            if (w_cnt > 0) memmove(wrapped_rows.data + top, wrapped_rows.data + top + count, (size_t)w_cnt);
        }
    }

    if (dirty_rows.data && dirty_rows.len > 0) {
        int32_t d_top = top < dirty_rows.len ? top : (int32_t)dirty_rows.len;
        int32_t d_bot = bottom < dirty_rows.len ? bottom : (int32_t)dirty_rows.len - 1;
        if (d_bot >= d_top) {
            memset(dirty_rows.data + d_top, 2, (size_t)(d_bot - d_top + 1));
        }
    }

    int32_t clear_top = bottom - count + 1;
    if (clear_top <= bottom) {
        int64_t clear_start = (int64_t)clear_top * cols;
        int64_t clear_total = (int64_t)count * cols;
        goosy_cells_fill(cells, clear_start, clear_total, fg, bg);

        if (prompt_rows.data && clear_top < prompt_rows.len) {
            int32_t p_end = bottom < prompt_rows.len ? bottom : (int32_t)prompt_rows.len - 1;
            if (p_end >= clear_top) memset(prompt_rows.data + clear_top, 0, (size_t)(p_end - clear_top + 1));
        }
        if (wrapped_rows.data && clear_top < wrapped_rows.len) {
            int32_t w_end = bottom < wrapped_rows.len ? bottom : (int32_t)wrapped_rows.len - 1;
            if (w_end >= clear_top) memset(wrapped_rows.data + clear_top, 0, (size_t)(w_end - clear_top + 1));
        }
    }
}

void goosy_push_scrollback(goosy_cells_slice_t sb_cells, int32_t sb_cols, int32_t dst_line,
                           goosy_cells_slice_t cells, int32_t cols, int32_t row,
                           goosy_bytes_t sb_prompt, goosy_bytes_t sb_wrapped,
                           goosy_const_bytes_t prompt_rows, goosy_const_bytes_t wrapped_rows) {
    if (!sb_cells.data || !cells.data || sb_cols <= 0 || cols <= 0) return;
    int64_t dst_base = (int64_t)dst_line * sb_cols;
    int64_t src_base = (int64_t)row * cols;
    if (dst_base < 0 || dst_base + sb_cols > sb_cells.len) return;
    if (src_base < 0 || src_base + cols > cells.len) return;

    size_t cell_sz = sizeof(goosy_cell_t);
    int32_t copy_cols = cols < sb_cols ? cols : sb_cols;
    uint8_t *dst = (uint8_t *)sb_cells.data + (size_t)dst_base * cell_sz;
    const uint8_t *src = (const uint8_t *)cells.data + (size_t)src_base * cell_sz;

    memcpy(dst, src, (size_t)copy_cols * cell_sz);
    if (sb_cols > copy_cols) {
        goosy_cell_t blank;
        memset(&blank, 0, sizeof(blank));
        blank.cp = 0x20;
        blank.width = 1;
        goosy_cell_t *pad_dst = (goosy_cell_t *)sb_cells.data + dst_base + copy_cols;
        for (int32_t c = 0; c < sb_cols - copy_cols; c++) pad_dst[c] = blank;
    }

    if (sb_prompt.data && dst_line < sb_prompt.len) {
        sb_prompt.data[dst_line] = (prompt_rows.data && row < prompt_rows.len) ? prompt_rows.data[row] : 0;
    }
    if (sb_wrapped.data && dst_line < sb_wrapped.len) {
        sb_wrapped.data[dst_line] = (wrapped_rows.data && row < wrapped_rows.len) ? wrapped_rows.data[row] : 0;
    }
}

void goosy_cells_copy(goosy_cells_slice_t dst, int64_t dst_offset, goosy_cells_slice_t src, int64_t src_offset, int64_t count) {
    if (!dst.data || !src.data || count <= 0) return;
    if (dst_offset < 0 || dst_offset + count > dst.len) return;
    if (src_offset < 0 || src_offset + count > src.len) return;
    size_t cell_sz = sizeof(goosy_cell_t);
    uint8_t *d = (uint8_t *)dst.data + (size_t)dst_offset * cell_sz;
    const uint8_t *s = (const uint8_t *)src.data + (size_t)src_offset * cell_sz;
    memmove(d, s, (size_t)count * cell_sz);
}

void goosy_cells_fill(goosy_cells_slice_t cells, int64_t start_idx, int64_t count, uint32_t fg, uint32_t bg) {
    if (!cells.data || count <= 0 || start_idx < 0 || start_idx + count > cells.len) return;
    goosy_cell_t blank;
    memset(&blank, 0, sizeof(blank));
    blank.cp = 0x20;
    blank.fg = fg;
    blank.bg = bg;
    blank.width = 1;
    goosy_cell_t *arr = (goosy_cell_t *)cells.data + start_idx;
    for (int64_t i = 0; i < count; i++) {
        arr[i] = blank;
    }
}

void goosy_cells_normalize_grid(goosy_cells_slice_t cells, int32_t cols, int32_t rows, int32_t offset,
                                goosy_bytes_t prompt_rows, goosy_bytes_t wrapped_rows) {
    if (!cells.data || cols <= 0 || rows <= 1 || offset <= 0 || offset >= rows) return;
    int64_t total = (int64_t)rows * cols;
    if (total > cells.len) return;

    size_t cell_sz = sizeof(goosy_cell_t);
    size_t total_bytes = (size_t)total * cell_sz;
    goosy_cell_t *tmp_cells = malloc(total_bytes);
    if (!tmp_cells) return;

    const goosy_cell_t *src_cells = (const goosy_cell_t *)cells.data;
    for (int32_t r = 0; r < rows; r++) {
        int32_t phys_r = (offset + r) % rows;
        memcpy(tmp_cells + (int64_t)r * cols, src_cells + (int64_t)phys_r * cols, (size_t)cols * cell_sz);
    }
    memcpy(cells.data, tmp_cells, total_bytes);
    free(tmp_cells);

    if (prompt_rows.data && prompt_rows.len >= rows) {
        uint8_t tmp_p[512];
        uint8_t *p_buf = rows <= 512 ? tmp_p : malloc((size_t)rows);
        if (p_buf) {
            for (int32_t r = 0; r < rows; r++) {
                p_buf[r] = prompt_rows.data[(offset + r) % rows];
            }
            memcpy(prompt_rows.data, p_buf, (size_t)rows);
            if (p_buf != tmp_p) free(p_buf);
        }
    }

    if (wrapped_rows.data && wrapped_rows.len >= rows) {
        uint8_t tmp_w[512];
        uint8_t *w_buf = rows <= 512 ? tmp_w : malloc((size_t)rows);
        if (w_buf) {
            for (int32_t r = 0; r < rows; r++) {
                w_buf[r] = wrapped_rows.data[(offset + r) % rows];
            }
            memcpy(wrapped_rows.data, w_buf, (size_t)rows);
            if (w_buf != tmp_w) free(w_buf);
        }
    }
}

static inline uint32_t sgr_resolve_256(const uint32_t *ansi, int64_t ansi_len, int32_t idx) {
    if (idx < 0) return 0;
    if (idx < 16 && ansi && idx < ansi_len) return ansi[idx];
    if (idx >= 16 && idx <= 231) {
        int n = idx - 16;
        int r = n / 36;
        int g = (n % 36) / 6;
        int b = n % 6;
        static const uint8_t v[] = { 0x00, 0x5F, 0x87, 0xAF, 0xD7, 0xFF };
        return ((uint32_t)v[r] << 16) | ((uint32_t)v[g] << 8) | (uint32_t)v[b];
    }
    if (idx >= 232 && idx <= 255) {
        uint8_t c = (uint8_t)(8 + (idx - 232) * 10);
        return ((uint32_t)c << 16) | ((uint32_t)c << 8) | (uint32_t)c;
    }
    return 0xEAEAEA;
}

int32_t goosy_fast_sgr(goosy_const_bytes_t data, int64_t pos,
                       goosy_u32_slice_t out_state,
                       goosy_const_u32_slice_t ansi_pal) {
    if (!data.data || pos < 0 || pos + 2 >= data.len || !out_state.data || out_state.len < 7) {
        return 0;
    }
    const uint8_t *src = data.data;
    if (src[pos] != 0x1B || src[pos + 1] != '[') {
        return 0;
    }

    int64_t k = pos + 2;
    int64_t limit = pos + 64 < data.len ? pos + 64 : data.len;
    bool found_m = false;
    while (k < limit) {
        uint8_t c = src[k];
        if (c == 'm') {
            found_m = true;
            break;
        }
        if (!((c >= '0' && c <= '9') || c == ';' || c == ':')) {
            return 0;
        }
        k++;
    }
    if (!found_m) return 0;

    int32_t params[16];
    uint8_t is_sub[16];
    int32_t pcount = 0;

    int32_t cur_val = 0;
    bool has_v = false;
    uint8_t next_sub = 0;
    int64_t idx = pos + 2;

    while (idx < k) {
        uint8_t c = src[idx];
        if (c >= '0' && c <= '9') {
            has_v = true;
            cur_val = cur_val * 10 + (c - '0');
        } else if (c == ';' || c == ':') {
            if (pcount < 16) {
                params[pcount] = cur_val;
                is_sub[pcount] = next_sub;
                pcount++;
            }
            cur_val = 0;
            has_v = false;
            next_sub = (c == ':') ? 1 : 0;
        }
        idx++;
    }
    if (has_v && pcount < 16) {
        params[pcount] = cur_val;
        is_sub[pcount] = next_sub;
        pcount++;
    }

    uint32_t fg = out_state.data[0];
    uint32_t bg = out_state.data[1];
    uint16_t attrs = (uint16_t)out_state.data[2];
    uint8_t  ul_style = (uint8_t)out_state.data[3];
    uint32_t ul_color = out_state.data[4];
    uint32_t def_fg = out_state.data[5];
    uint32_t def_bg = out_state.data[6];

    if (pcount == 0) {
        fg = def_fg;
        bg = def_bg;
        attrs = 0;
        ul_style = 0;
        ul_color = 0;
    } else {
        int32_t i = 0;
        while (i < pcount) {
            int32_t code = params[i];
            if (code == 0) {
                fg = def_fg;
                bg = def_bg;
                attrs = 0;
                ul_style = 0;
                ul_color = 0;
            } else if (code == 1) {
                attrs |= 1; // BOLD
            } else if (code == 2) {
                attrs |= 64; // DIM
            } else if (code == 3) {
                attrs |= 2; // ITALIC
            } else if (code == 4) {
                if (i + 1 < pcount && is_sub[i + 1] && params[i + 1] >= 0 && params[i + 1] <= 5) {
                    int32_t sub = params[i + 1];
                    if (sub == 0) {
                        attrs &= ~4;
                        ul_style = 0;
                    } else {
                        attrs |= 4;
                        ul_style = (uint8_t)sub;
                    }
                    i++;
                } else {
                    attrs |= 4;
                    ul_style = 1;
                }
            } else if (code == 5 || code == 6) {
                attrs |= 8; // BLINK
            } else if (code == 7) {
                attrs |= 16; // INVERSE
            } else if (code == 8) {
                attrs |= 128; // HIDDEN
            } else if (code == 9) {
                attrs |= 32; // STRIKE
            } else if (code == 22) {
                attrs &= ~(1 | 64);
            } else if (code == 23) {
                attrs &= ~2;
            } else if (code == 24) {
                attrs &= ~4;
                ul_style = 0;
                ul_color = 0;
            } else if (code == 25) {
                attrs &= ~8;
            } else if (code == 27) {
                attrs &= ~16;
            } else if (code == 28) {
                attrs &= ~128;
            } else if (code == 29) {
                attrs &= ~32;
            } else if (code >= 30 && code <= 37) {
                if (ansi_pal.data && code - 30 < ansi_pal.len) fg = ansi_pal.data[code - 30];
            } else if (code == 39) {
                fg = def_fg;
            } else if (code >= 40 && code <= 47) {
                if (ansi_pal.data && code - 40 < ansi_pal.len) bg = ansi_pal.data[code - 40];
            } else if (code == 49) {
                bg = def_bg;
            } else if (code >= 90 && code <= 97) {
                if (ansi_pal.data && code - 90 + 8 < ansi_pal.len) fg = ansi_pal.data[code - 90 + 8];
            } else if (code >= 100 && code <= 107) {
                if (ansi_pal.data && code - 100 + 8 < ansi_pal.len) bg = ansi_pal.data[code - 100 + 8];
            } else if (code == 38 || code == 48) {
                bool is_fg = (code == 38);
                if (i + 2 < pcount && params[i + 1] == 5) {
                    uint32_t rgb = sgr_resolve_256(ansi_pal.data, ansi_pal.len, params[i + 2]);
                    if (is_fg) fg = rgb; else bg = rgb;
                    i += 2;
                } else if (i + 4 < pcount && params[i + 1] == 2) {
                    if (i + 5 < pcount && is_sub[i + 1] && is_sub[i + 2]) {
                        uint32_t rgb = ((uint32_t)(params[i + 3] & 0xFF) << 16) |
                                       ((uint32_t)(params[i + 4] & 0xFF) << 8)  |
                                       ((uint32_t)(params[i + 5] & 0xFF));
                        if (is_fg) fg = rgb; else bg = rgb;
                        i += 5;
                    } else {
                        uint32_t rgb = ((uint32_t)(params[i + 2] & 0xFF) << 16) |
                                       ((uint32_t)(params[i + 3] & 0xFF) << 8)  |
                                       ((uint32_t)(params[i + 4] & 0xFF));
                        if (is_fg) fg = rgb; else bg = rgb;
                        i += 4;
                    }
                }
            } else if (code == 58) {
                if (i + 2 < pcount && params[i + 1] == 5) {
                    ul_color = sgr_resolve_256(ansi_pal.data, ansi_pal.len, params[i + 2]);
                    i += 2;
                } else if (i + 4 < pcount && params[i + 1] == 2) {
                    if (i + 5 < pcount && is_sub[i + 1] && is_sub[i + 2]) {
                        ul_color = ((uint32_t)(params[i + 3] & 0xFF) << 16) |
                                   ((uint32_t)(params[i + 4] & 0xFF) << 8)  |
                                   ((uint32_t)(params[i + 5] & 0xFF));
                        i += 5;
                    } else {
                        ul_color = ((uint32_t)(params[i + 2] & 0xFF) << 16) |
                                   ((uint32_t)(params[i + 3] & 0xFF) << 8)  |
                                   ((uint32_t)(params[i + 4] & 0xFF));
                        i += 4;
                    }
                }
            } else if (code == 59) {
                ul_color = 0;
            }
            i++;
        }
    }

    out_state.data[0] = fg;
    out_state.data[1] = bg;
    out_state.data[2] = (uint32_t)attrs;
    out_state.data[3] = (uint32_t)ul_style;
    out_state.data[4] = ul_color;

    return (int32_t)(k - pos + 1);
}

static inline void clear_cells_row(goosy_cell_t *row_cells, int32_t cols, goosy_cell_t blank) {
    static goosy_cell_t s_blank_row[1024];
    static bool s_blank_inited = false;
    static uint32_t s_last_fg = 0;
    static uint32_t s_last_bg = 0;

    if (!s_blank_inited || s_last_fg != blank.fg || s_last_bg != blank.bg) {
        for (int i = 0; i < 1024; i++) s_blank_row[i] = blank;
        s_blank_inited = true;
        s_last_fg = blank.fg;
        s_last_bg = blank.bg;
    }
    if (cols <= 1024) {
        memcpy(row_cells, s_blank_row, (size_t)cols * sizeof(goosy_cell_t));
    } else {
        for (int32_t c = 0; c < cols; c++) row_cells[c] = blank;
    }
}

int64_t goosy_stream_fast(goosy_cells_slice_t cells, int32_t cols, int32_t rows,
                          goosy_i32_slice_t state,
                          goosy_const_u32_slice_t ansi_pal,
                          goosy_cells_slice_t sb_cells, int32_t sb_cols, int32_t sb_cap,
                          goosy_bytes_t sb_prompt, goosy_bytes_t sb_wrapped,
                          goosy_bytes_t prompt_rows, goosy_bytes_t wrapped_rows,
                          goosy_const_bytes_t data, int64_t pos) {
    if (!cells.data || cols <= 0 || rows <= 0 || !state.data || state.len < 17 ||
        !data.data || pos < 0 || pos >= data.len) {
        return 0;
    }

    int32_t cx = state.data[0];
    int32_t cy = state.data[1];
    int32_t grid_off = state.data[2];
    bool wrap_pend = (state.data[3] != 0);
    int32_t sb_head = state.data[4];
    int32_t sb_count = state.data[5];
    int32_t scroll_top = state.data[6];
    int32_t scroll_bottom = state.data[7];
    bool autowrap = (state.data[8] != 0);
    uint32_t fg = (uint32_t)state.data[9];
    uint32_t bg = (uint32_t)state.data[10];
    uint16_t attrs = (uint16_t)state.data[11];
    uint8_t  ul_style = (uint8_t)state.data[12];
    uint32_t ul_color = (uint32_t)state.data[13];
    uint32_t def_fg = (uint32_t)state.data[14];
    uint32_t def_bg = (uint32_t)state.data[15];
    uint32_t link_id = (uint32_t)state.data[16];

    goosy_cell_t tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    tmpl.fg = fg;
    tmpl.bg = bg;
    tmpl.attrs = attrs;
    tmpl.width = 1;
    tmpl.ul_style = ul_style;
    tmpl.ul_color = ul_color;
    tmpl.link_id = link_id;

    goosy_cell_t blank;
    memset(&blank, 0, sizeof(blank));
    blank.cp = 0x20;
    blank.fg = fg;
    blank.bg = bg;
    blank.width = 1;

    int32_t phys_r = (grid_off + cy) % rows;
    goosy_cell_t *cur_row_cells = (goosy_cell_t *)cells.data + (int64_t)phys_r * cols;

    const uint8_t *src = data.data;
    int64_t i = pos;
    int64_t n = data.len;

    while (i < n) {
        uint8_t b = src[i];

        // SGR ANSI sequence \033[...m fast inline handling
        if (b == 0x1B && i + 2 < n && src[i + 1] == '[') {
            uint32_t sgr_buf[7] = { tmpl.fg, tmpl.bg, (uint32_t)tmpl.attrs, (uint32_t)tmpl.ul_style, tmpl.ul_color, def_fg, def_bg };
            goosy_u32_slice_t out_state = { sgr_buf, 7 };
            int32_t consumed = goosy_fast_sgr(data, i, out_state, ansi_pal);
            if (consumed > 0) {
                tmpl.fg = sgr_buf[0];
                tmpl.bg = sgr_buf[1];
                tmpl.attrs = (uint16_t)sgr_buf[2];
                tmpl.ul_style = (uint8_t)sgr_buf[3];
                tmpl.ul_color = sgr_buf[4];
                blank.fg = tmpl.fg;
                blank.bg = tmpl.bg;
                i += consumed;
                continue;
            }
        }

        // Printable ASCII
        if (b >= 0x20 && b < 0x7F) {
            if (wrap_pend) {
                wrap_pend = false;
                if (autowrap) {
                    int32_t pr = (grid_off + cy) % rows;
                    if (wrapped_rows.data && pr < wrapped_rows.len) wrapped_rows.data[pr] = 1;
                    cx = 0;
                    if (cy == scroll_bottom) {
                        if (scroll_top == 0 && scroll_bottom == rows - 1) {
                            int32_t phys_top = grid_off;
                            if (sb_cap > 0 && sb_cells.data) {
                                int32_t dst_line = (sb_head + sb_count) % sb_cap;
                                if (sb_count == sb_cap) {
                                    sb_head = (sb_head + 1) % sb_cap;
                                } else {
                                    sb_count++;
                                }
                                int64_t dst_idx = (int64_t)dst_line * sb_cols;
                                int64_t src_idx = (int64_t)phys_top * cols;
                                if (dst_idx + sb_cols <= sb_cells.len && src_idx + cols <= cells.len) {
                                    int32_t copy_c = cols < sb_cols ? cols : sb_cols;
                                    memcpy((goosy_cell_t *)sb_cells.data + dst_idx,
                                           (goosy_cell_t *)cells.data + src_idx,
                                           (size_t)copy_c * sizeof(goosy_cell_t));
                                }
                                if (sb_prompt.data && dst_line < sb_prompt.len && prompt_rows.data && phys_top < prompt_rows.len)
                                    sb_prompt.data[dst_line] = prompt_rows.data[phys_top];
                                if (sb_wrapped.data && dst_line < sb_wrapped.len && wrapped_rows.data && phys_top < wrapped_rows.len)
                                    sb_wrapped.data[dst_line] = wrapped_rows.data[phys_top];
                            }
                            grid_off = (grid_off + 1) % rows;
                            int64_t clear_base = (int64_t)phys_top * cols;
                            if (clear_base + cols <= cells.len) {
                                clear_cells_row((goosy_cell_t *)cells.data + clear_base, cols, blank);
                            }
                            if (prompt_rows.data && phys_top < prompt_rows.len) prompt_rows.data[phys_top] = 0;
                            if (wrapped_rows.data && phys_top < wrapped_rows.len) wrapped_rows.data[phys_top] = 0;
                        } else {
                            break;
                        }
                    } else if (cy < rows - 1) {
                        cy++;
                    }
                    phys_r = (grid_off + cy) % rows;
                    cur_row_cells = (goosy_cell_t *)cells.data + (int64_t)phys_r * cols;
                }
            }

            // Inner tight loop: consume contiguous printable ASCII into current row
            while (i < n && cx < cols) {
                uint8_t ch = src[i];
                if (ch < 0x20 || ch >= 0x7F) break;
                if (cur_row_cells[cx].flags & 1) {
                    if (cx > 0) cur_row_cells[cx - 1] = blank;
                } else if (cur_row_cells[cx].width == 2) {
                    if (cx + 1 < cols) cur_row_cells[cx + 1] = blank;
                }
                tmpl.cp = (uint32_t)ch;
                cur_row_cells[cx++] = tmpl;
                i++;
            }
            if (cx >= cols) {
                cx = cols - 1;
                if (autowrap) wrap_pend = true;
            }
            continue;
        }

        // CRLF fast handling
        if (b == 0x0D) {
            if (i + 1 < n && src[i + 1] == 0x0A) {
                wrap_pend = false;
                cx = 0;
                if (cy == scroll_bottom) {
                    if (scroll_top == 0 && scroll_bottom == rows - 1) {
                        int32_t phys_top = grid_off;
                        if (sb_cap > 0 && sb_cells.data) {
                            int32_t dst_line = (sb_head + sb_count) % sb_cap;
                            if (sb_count == sb_cap) {
                                sb_head = (sb_head + 1) % sb_cap;
                            } else {
                                sb_count++;
                            }
                            int64_t dst_idx = (int64_t)dst_line * sb_cols;
                            int64_t src_idx = (int64_t)phys_top * cols;
                            if (dst_idx + sb_cols <= sb_cells.len && src_idx + cols <= cells.len) {
                                int32_t copy_c = cols < sb_cols ? cols : sb_cols;
                                memcpy((goosy_cell_t *)sb_cells.data + dst_idx,
                                       (goosy_cell_t *)cells.data + src_idx,
                                       (size_t)copy_c * sizeof(goosy_cell_t));
                            }
                            if (sb_prompt.data && dst_line < sb_prompt.len && prompt_rows.data && phys_top < prompt_rows.len)
                                sb_prompt.data[dst_line] = prompt_rows.data[phys_top];
                            if (sb_wrapped.data && dst_line < sb_wrapped.len && wrapped_rows.data && phys_top < wrapped_rows.len)
                                sb_wrapped.data[dst_line] = wrapped_rows.data[phys_top];
                        }
                        grid_off = (grid_off + 1) % rows;
                        int64_t clear_base = (int64_t)phys_top * cols;
                        if (clear_base + cols <= cells.len) {
                            clear_cells_row((goosy_cell_t *)cells.data + clear_base, cols, blank);
                        }
                        if (prompt_rows.data && phys_top < prompt_rows.len) prompt_rows.data[phys_top] = 0;
                        if (wrapped_rows.data && phys_top < wrapped_rows.len) wrapped_rows.data[phys_top] = 0;
                    } else {
                        break;
                    }
                } else if (cy < rows - 1) {
                    cy++;
                }
                phys_r = (grid_off + cy) % rows;
                cur_row_cells = (goosy_cell_t *)cells.data + (int64_t)phys_r * cols;
                i += 2;
                continue;
            } else {
                wrap_pend = false;
                cx = 0;
                i++;
                continue;
            }
        } else if (b == 0x0A) {
            wrap_pend = false;
            if (cy == scroll_bottom) {
                if (scroll_top == 0 && scroll_bottom == rows - 1) {
                    int32_t phys_top = grid_off;
                    if (sb_cap > 0 && sb_cells.data) {
                        int32_t dst_line = (sb_head + sb_count) % sb_cap;
                        if (sb_count == sb_cap) {
                            sb_head = (sb_head + 1) % sb_cap;
                        } else {
                            sb_count++;
                        }
                        int64_t dst_idx = (int64_t)dst_line * sb_cols;
                        int64_t src_idx = (int64_t)phys_top * cols;
                        if (dst_idx + sb_cols <= sb_cells.len && src_idx + cols <= cells.len) {
                            int32_t copy_c = cols < sb_cols ? cols : sb_cols;
                            memcpy((goosy_cell_t *)sb_cells.data + dst_idx,
                                   (goosy_cell_t *)cells.data + src_idx,
                                   (size_t)copy_c * sizeof(goosy_cell_t));
                        }
                        if (sb_prompt.data && dst_line < sb_prompt.len && prompt_rows.data && phys_top < prompt_rows.len)
                            sb_prompt.data[dst_line] = prompt_rows.data[phys_top];
                        if (sb_wrapped.data && dst_line < sb_wrapped.len && wrapped_rows.data && phys_top < wrapped_rows.len)
                            sb_wrapped.data[dst_line] = wrapped_rows.data[phys_top];
                    }
                    grid_off = (grid_off + 1) % rows;
                    int64_t clear_base = (int64_t)phys_top * cols;
                    if (clear_base + cols <= cells.len) {
                        clear_cells_row((goosy_cell_t *)cells.data + clear_base, cols, blank);
                    }
                    if (prompt_rows.data && phys_top < prompt_rows.len) prompt_rows.data[phys_top] = 0;
                    if (wrapped_rows.data && phys_top < wrapped_rows.len) wrapped_rows.data[phys_top] = 0;
                } else {
                    break;
                }
            } else if (cy < rows - 1) {
                cy++;
            }
            phys_r = (grid_off + cy) % rows;
            cur_row_cells = (goosy_cell_t *)cells.data + (int64_t)phys_r * cols;
            i++;
            continue;
        }

        break;
    }

    state.data[0] = cx;
    state.data[1] = cy;
    state.data[2] = grid_off;
    state.data[3] = wrap_pend ? 1 : 0;
    state.data[4] = sb_head;
    state.data[5] = sb_count;
    state.data[9] = (int32_t)tmpl.fg;
    state.data[10] = (int32_t)tmpl.bg;
    state.data[11] = (int32_t)tmpl.attrs;
    state.data[12] = (int32_t)tmpl.ul_style;
    state.data[13] = (int32_t)tmpl.ul_color;

    return i - pos;
}

int32_t goosy_cells_write_ascii(goosy_cells_slice_t cells, int64_t row_base, int32_t cx,
                                goosy_const_bytes_t data, int64_t data_pos, int32_t max_count,
                                uint32_t fg, uint32_t bg, uint32_t attrs,
                                uint32_t ul_style, uint32_t ul_color, uint32_t link_id) {
    if (!cells.data || !data.data || max_count <= 0 || data_pos < 0 || data_pos >= data.len) return 0;
    int64_t avail = data.len - data_pos;
    int32_t limit = (int32_t)(avail < (int64_t)max_count ? avail : (int64_t)max_count);
    if (limit <= 0) return 0;

    const uint8_t *src = data.data + data_pos;
    int32_t count = 0;
    while (count < limit) {
        uint8_t b = src[count];
        if (b < 0x20 || b >= 0x7F) break;
        count++;
    }
    if (count <= 0) return 0;

    int64_t dst_idx = row_base + cx;
    if (dst_idx < 0 || dst_idx + count > cells.len) return 0;

    goosy_cell_t *dst = (goosy_cell_t *)cells.data + dst_idx;
    goosy_cell_t tmpl;
    memset(&tmpl, 0, sizeof(tmpl));
    tmpl.fg = fg;
    tmpl.bg = bg;
    tmpl.attrs = (uint16_t)attrs;
    tmpl.width = 1;
    tmpl.ul_style = (uint8_t)ul_style;
    tmpl.ul_color = ul_color;
    tmpl.link_id = link_id;

    for (int32_t i = 0; i < count; i++) {
        tmpl.cp = (uint32_t)src[i];
        dst[i] = tmpl;
    }
    return count;
}

void goosy_pty_close(void) {
    while (g_overflow_head) {
        pty_chunk_t *next = g_overflow_head->next;
        free(g_overflow_head);
        g_overflow_head = next;
    }
    g_overflow_tail = NULL;

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
int32_t goosy_spawn_self(goosy_const_bytes_t cwd) {
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

int32_t goosy_validate_url(goosy_const_bytes_t url) {
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

int32_t goosy_open_url(goosy_const_bytes_t url) {
    if (!goosy_validate_url(url)) {
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
int64_t goosy_env_get(goosy_const_bytes_t name, goosy_bytes_t out_buf) {
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

int64_t goosy_file_read(goosy_const_bytes_t path, goosy_bytes_t out_buf) {
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
        snprintf(out, cap, "%s/goosy/window_geometry", state_home);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] != '/') home = "/tmp";
        snprintf(out, cap, "%s/.local/state/goosy/window_geometry", home);
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

int32_t goosy_state_load_cols(void) {
    load_state_if_needed();
    return g_saved_cols;
}

int32_t goosy_state_load_rows(void) {
    load_state_if_needed();
    return g_saved_rows;
}

void goosy_state_save_geometry(int32_t cols, int32_t rows) {
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
