#!/usr/bin/env bash
# Goosy Comprehensive Test Suite Runner
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "=== 1. Running Pure Goose Engine Unit Tests ==="
(cd "$ROOT_DIR/tests" && goose run test_goosy.goose)

echo -e "\n=== 2. Running Native POSIX PTY, epoll & URL Security Tests ==="
gcc -O2 -Wall -Wextra -Inative $(pkg-config --cflags --libs pixman-1 libpng) \
    tests/test_native_pty_poll.c native/goosy_pty.c native/goosy_poll.c native/goosy_image.c \
    -o /tmp/test_native_pty_poll
/tmp/test_native_pty_poll

echo -e "\n=== 3. Running Security Fuzzing & Resource Boundary Tests ==="
gcc -O2 -Wall -Wextra -Inative $(pkg-config --cflags --libs pixman-1 libpng) \
    tests/test_security_fuzz.c native/goosy_image.c native/goosy_pty.c \
    -o /tmp/test_security_fuzz
/tmp/test_security_fuzz

echo -e "\n=== ALL GOOSY TEST SUITES PASSED SUCCESSFULLY ==="
