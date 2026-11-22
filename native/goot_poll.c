/*
 * Goot — Linux epoll + signalfd Event Loop Bridge
 * Blocks cleanly with zero busy-waiting when idle.
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "goot_native.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <time.h>
#include <unistd.h>

enum {
    TAG_WAYLAND = 1,
    TAG_PTY     = 2,
    TAG_SIGNAL  = 3
};

static int g_epfd  = -1;
static int g_sigfd = -1;
static volatile int g_terminated = 0;

int32_t goot_poll_terminated(void) {
    return g_terminated;
}

int32_t goot_poll_init(int32_t wl_fd, int32_t pty_fd) {
    g_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (g_epfd < 0) {
        return -1;
    }

    /* Block SIGCHLD, SIGTERM, SIGINT so they are delivered via signalfd */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        close(g_epfd);
        g_epfd = -1;
        return -1;
    }

    g_sigfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (g_sigfd < 0) {
        close(g_epfd);
        g_epfd = -1;
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));

    ev.events = EPOLLIN;
    ev.data.u32 = TAG_SIGNAL;
    if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, g_sigfd, &ev) < 0) {
        goot_poll_close();
        return -1;
    }

    if (wl_fd >= 0) {
        ev.events = EPOLLIN;
        ev.data.u32 = TAG_WAYLAND;
        if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, wl_fd, &ev) < 0) {
            goot_poll_close();
            return -1;
        }
    }

    if (pty_fd >= 0) {
        ev.events = EPOLLIN;
        ev.data.u32 = TAG_PTY;
        if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, pty_fd, &ev) < 0) {
            goot_poll_close();
            return -1;
        }
    }

    return 0;
}

int32_t goot_poll_wait(int32_t timeout_ms) {
    if (g_epfd < 0) {
        return GOOT_POLL_ERR;
    }

    struct epoll_event events[8];
    int n;
    do {
        n = epoll_wait(g_epfd, events, 8, timeout_ms);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        return GOOT_POLL_ERR;
    }
    if (n == 0) {
        return GOOT_POLL_TIMEOUT;
    }

    int32_t mask = 0;
    for (int i = 0; i < n; i++) {
        uint32_t tag = events[i].data.u32;
        uint32_t evs = events[i].events;
        if (tag == TAG_WAYLAND) {
            mask |= GOOT_POLL_WAYLAND;
        } else if (tag == TAG_PTY) {
            if (evs & EPOLLIN) {
                mask |= GOOT_POLL_PTY;
            }
            if (evs & (EPOLLHUP | EPOLLERR)) {
                mask |= GOOT_POLL_PTY_HUP;
            }
        } else if (tag == TAG_SIGNAL) {
            struct signalfd_siginfo si;
            while (read(g_sigfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
                if (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT) {
                    g_terminated = 1;
                }
                mask |= GOOT_POLL_SIGNAL;
            }
        }
    }
    return mask;
}

void goot_poll_close(void) {
    if (g_sigfd >= 0) {
        close(g_sigfd);
        g_sigfd = -1;
    }
    if (g_epfd >= 0) {
        close(g_epfd);
        g_epfd = -1;
    }
}

int64_t goot_time_monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}
