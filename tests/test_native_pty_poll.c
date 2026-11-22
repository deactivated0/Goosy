/*
 * Goot — Native C Bridge Unit Tests (POSIX PTY + epoll/signalfd + URL validation)
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "goot_native.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static goot_const_bytes_t cstr(const char *s) {
    goot_const_bytes_t b = { (const uint8_t *)s, (int64_t)strlen(s) };
    return b;
}

int main(void) {
    printf("[test_native] 1. Testing URL scheme validation (security)...\n");
    assert(goot_validate_url(cstr("https://aardappel.github.io/goose/")) == 1);
    assert(goot_validate_url(cstr("http://example.com/test")) == 1);
    assert(goot_validate_url(cstr("javascript:alert(1)")) == 0);
    assert(goot_validate_url(cstr("https://evil.com/a;rm -rf /")) == 0);
    assert(goot_validate_url(cstr("short")) == 0);

    printf("[test_native] 2. Testing invalid PTY dimension rejection...\n");
    assert(goot_pty_spawn(cstr("/bin/sh"), cstr("xterm-256color"), 0, 24, 0, 0) < 0);
    assert(goot_pty_spawn(cstr("/bin/sh"), cstr("xterm-256color"), 80, -5, 0, 0) < 0);
    assert(goot_pty_spawn(cstr("/bin/sh"), cstr("xterm-256color"), 9999, 24, 0, 0) < 0);

    printf("[test_native] 3. Spawning real POSIX PTY with /bin/sh...\n");
    assert(goot_pty_spawn(cstr("/bin/sh"), cstr("xterm-256color"), 80, 24, 640, 384) == 0);
    int32_t pfd = goot_pty_fd();
    assert(pfd >= 0);
    assert(goot_pty_child_pid() > 0);

    printf("[test_native] 4. Initializing epoll + signalfd & testing TIOCSWINSZ...\n");
    assert(goot_poll_init(-1, pfd) == 0);
    assert(goot_pty_resize(120, 40, 960, 640) == 0);

    printf("[test_native] 5. Executing command over PTY via epoll...\n");
    const char *cmd = "echo GOOT_PTY_OK:$TERM:$COLORTERM; stty size; exit 0\n";
    assert(goot_pty_write(cstr(cmd)) == (int64_t)strlen(cmd));

    uint8_t acc[4096];
    size_t acc_len = 0;
    uint8_t tmp[512];
    int64_t start_ns = goot_time_monotonic_ns();
    bool exited = false;

    for (int iter = 0; iter < 50 && !exited; iter++) {
        int32_t mask = goot_poll_wait(200);
        assert(mask != GOOT_POLL_ERR);
        if (mask & GOOT_POLL_PTY) {
            for (;;) {
                goot_bytes_t buf = { tmp, (int64_t)sizeof(tmp) };
                int64_t rn = goot_pty_read(buf);
                if (rn > 0) {
                    size_t copy = (size_t)rn;
                    if (acc_len + copy >= sizeof(acc)) copy = sizeof(acc) - 1 - acc_len;
                    memcpy(acc + acc_len, tmp, copy);
                    acc_len += copy;
                } else if (rn == 0) {
                    exited = true;
                    break;
                } else {
                    break;
                }
            }
        }
        if (mask & (GOOT_POLL_SIGNAL | GOOT_POLL_PTY_HUP)) {
            if (goot_pty_check_child() == 1) exited = true;
        }
    }

    acc[acc_len] = '\0';
    int64_t elapsed_ms = (goot_time_monotonic_ns() - start_ns) / 1000000LL;
    printf("[test_native] PTY round-trip completed in %lld ms\n", (long long)elapsed_ms);

    assert(strstr((const char *)acc, "GOOT_PTY_OK:xterm-256color:truecolor") != NULL);
    assert(strstr((const char *)acc, "40 120") != NULL);

    for (int i = 0; i < 20 && goot_pty_check_child() == 0; i++) usleep(5000);
    assert(goot_pty_check_child() == 1);
    assert(goot_pty_exit_code() == 0);

    goot_poll_close();
    goot_pty_close();
    printf("[test_native] ALL NATIVE PTY + EPOLL + URL SECURITY TESTS PASSED.\n");
    return 0;
}
