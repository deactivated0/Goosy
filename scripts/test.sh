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

echo -e "\n=== 4. Running Multi-Backend Renderer Tests (Pixman, OpenGL, Vulkan) ==="
gcc -O2 -Wall -Wextra -Inative -Inative/generated \
    $(pkg-config --cflags pixman-1 freetype2 harfbuzz xkbcommon libpng) \
    tests/test_backends.c \
    native/goosy_render.c native/goosy_render_pixman.c native/goosy_render_gl.c native/goosy_render_vk.c \
    native/goosy_wayland.c native/goosy_pty.c native/goosy_poll.c native/goosy_font.c native/goosy_image.c native/goosy_xkb.c \
    native/generated/xdg-shell-protocol.c native/generated/xdg-decoration-unstable-v1-protocol.c \
    native/generated/primary-selection-unstable-v1-protocol.c native/generated/fractional-scale-v1-protocol.c \
    native/generated/viewporter-protocol.c \
    $(pkg-config --libs wayland-client wayland-cursor wayland-egl pixman-1 freetype2 fontconfig harfbuzz xkbcommon libpng) \
    -lEGL -lGLESv2 -lvulkan -lm -lpthread -lrt \
    -o /tmp/test_backends
/tmp/test_backends

echo -e "\n=== ALL GOOSY TEST SUITES PASSED SUCCESSFULLY ==="
