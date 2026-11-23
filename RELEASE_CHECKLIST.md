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
  - [x] No layout shift or baseline jump during text selection.
  - [x] Safe resizing with preserved prompt markers and wrapped lines.
  - [x] Safe paste handling without duplicate newlines (`\r\n` normalization).

- [x] **Security & Memory Safety**
  - [x] Sixel dimension caps (4096×4096) and memory bounds enforced.
  - [x] Kitty graphics base64 and dimension bounds validated.
  - [x] PTY non-truncating write queue with backpressure handling.
  - [x] URL scheme validation and direct spawner execution (no shell).

- [x] **Performance & Resource Consumption**
  - [x] Zero CPU usage (0 ticks) while idle on Wayland compositor.
  - [x] Startup time under 50 ms.
  - [x] Resident Set Size (RSS) under 40 MB at idle.
  - [x] Software render frame composite rate > 600 FPS on test matrix (751.9 FPS achieved).
  - [x] Scrollback engine throughput > 25,000 lines/s (30,921 lines/s achieved).

- [x] **Packaging & Documentation**
  - [x] `README.md` updated with accurate empirical benchmark stats.
  - [x] `man/man5/goosy.ini.5` configuration manual up to date.
  - [x] Desktop entry `assets/goosy.desktop` and icons valid.
