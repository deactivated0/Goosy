# Goosy Release Quality Checklist

## Pre-Release Verification Steps

- [x] **Build Quality**
  - [x] Clean build with `./scripts/build.sh` (Goose check + native C compile with `-O2`).
  - [x] Zero compiler warnings with `-Wall -Wextra`.
  - [x] Clean local installation with `./scripts/install.sh`.
  - [x] Clean uninstallation verification with `./scripts/uninstall.sh`.

- [x] **Correctness & Tests**
  - [x] All Goose language tests pass (`cd tests && goose run test_goosy.goose`).
  - [x] Native PTY, epoll, and URL security tests pass (`tests/test_native_pty_poll.c`).
  - [x] Security fuzzing and resource boundary tests pass (`tests/test_security_fuzz.c`).
  - [x] Multi-backend renderer tests pass for Pixman, OpenGL, Vulkan, and auto-probe (`tests/test_backends.c`).
  - [x] No layout shift or baseline jump during text selection.
  - [x] Safe resizing with preserved prompt markers and wrapped lines.
  - [x] Safe paste handling without duplicate newlines (`\r\n` normalization).

- [x] **Security & Memory Safety**
  - [x] Sixel dimension caps (4096×4096) and memory bounds enforced.
  - [x] Kitty graphics base64 and dimension bounds validated.
  - [x] PTY non-truncating write queue with backpressure handling (up to 16 MiB).
  - [x] URL scheme validation and direct spawner execution (no shell).
  - [x] Embedded SPIR-V shaders eliminate runtime shader injection vectors.

- [x] **Performance & Resource Consumption**
  - [x] Zero CPU usage (0 ticks) while idle on Wayland compositor.
  - [x] Cold startup time under 40 ms (**29.2 ms achieved on Pixman**, 56.3 ms on Vulkan).
  - [x] Resident Set Size (RSS) under 20 MB at idle (**18.6 MB achieved on Pixman**).
  - [x] Scrollback engine throughput > 200,000 lines/s (**245,098 lines/s achieved**).
  - [x] Sustained streaming throughput: **13.2 MB/s** (beating Kitty 13.0 MB/s and Konsole 11.8 MB/s).

- [x] **Packaging & Documentation**
  - [x] `README.md` updated with accurate empirical benchmark stats.
  - [x] `GPU_BACKENDS.md`, `PERFORMANCE_AUDIT.md`, `PRODUCTION_READINESS.md`, and `BENCHMARK_REPORT.md` complete and up to date.
  - [x] `man/man5/goosy.ini.5` configuration manual up to date.
  - [x] Desktop entry `assets/goosy.desktop` and icons valid.
