# Goot — Progress Log

## Environment Summary
- **Sandbox OS**: Debian GNU/Linux 12 (bookworm), `x86_64`, Linux `6.12.105+`
- **Available in sandbox**: `gcc`, `make`, `python3`, `git`, `tar`, `unzip`
- **Unavailable in sandbox** (offline container, no package installation): `goose` compiler, `pkg-config`, `wayland-scanner`, `libwayland-client`, `wayland-protocols`, `pixman-1`, `freetype2`, `fontconfig`, `harfbuzz`, `libxkbcommon`, Wayland compositor (`labwc`)

## Files Created
- **Goose application & modules**:
  - `goot.goose` (entry point, `epoll` event loop, batched PTY reads, resize/input/render dispatch)
  - `goot.build` (Goose sidecar build configuration)
  - `src/ffi.goose` (`extern fn` declarations matching `native/goot_native.h`)
  - `src/utf8.goose` (streaming UTF-8 decoder, combining marks, East Asian wide-char cell width)
  - `src/colors.goose` (16-color, 256-color xterm, 24-bit truecolor, hex parser)
  - `src/config.goose` (lightweight TOML/INI parser for `~/.config/goot/goot.toml`)
  - `src/screen.goose` (`root struct Screen`, primary/alt grids, bounded scrollback ring buffer, damage flags, tab stops, selection)
  - `src/parser.goose` (VT100/VT220/ECMA-48 state machine, CSI/OSC/SGR, DEC private modes, OSC 52 blocked by default)
  - `src/input.goose` (XKB keysym translation, shortcuts, bracketed paste sanitization, SGR/X10 mouse reporting)
  - `src/renderer.goose` (damage-tracked cell & cursor render coordinator)
- **Minimal C99 native bridge**:
  - `native/goot_native.h`
  - `native/goot_pty.c`
  - `native/goot_poll.c`
  - `native/goot_xkb.c`
  - `native/goot_wayland.c`
  - `native/goot_font.c`
  - `native/goot_render.c`
- **Build, install, config, tests & benchmarks**:
  - `scripts/build.sh`, `scripts/install.sh`, `scripts/uninstall.sh`
  - `config/goot.toml`
  - `tests/test_goot.goose`, `tests/test_native_pty_poll.c`
  - `benchmarks/run_benchmarks.sh`
- **Documentation**:
  - `README.md`, `PLAN.md`, `PROGRESS.md`, `ARCHITECTURE.md`, `BENCHMARKS.md`, `KNOWN_ISSUES.md`

## Verification Results
- **Passed (Sandbox)**:
  - `gcc -O2 -Wall -Wextra -Werror -std=c99 -Inative native/goot_pty.c native/goot_poll.c tests/test_native_pty_poll.c`: compiled cleanly with zero warnings.
  - Executed `test_native_pty_poll`: caught and fixed a `TIOCSWINSZ` startup race condition (moved initial `ioctl(TIOCSWINSZ)` onto `master_fd` before `fork()`), then passed all assertions (invalid dimension rejection, `/bin/sh` PTY spawn, `TERM=xterm-256color`, `COLORTERM=truecolor`, `120x40` resize via `stty size`, `epoll`/`signalfd` wakeup, 1 ms round-trip, clean exit-code-0 reaping).
- **Pending Target Artix Linux Host**:
  - `wayland-scanner` generation of `native/generated/xdg-shell-client-protocol.h` and `xdg-shell-protocol.c`
  - `goose check goot.goose` and `goose build -O2 -o build/goot goot.goose`
  - `goose run tests/test_goot.goose`
  - Live Wayland (`labwc`) rendering, interactive app compatibility suite (Bash, Fish, Zsh, Vim, Neovim, Nano, Htop, Btop, Less, Tmux, Fastfetch, OpenCode, Claude Code), and `benchmarks/run_benchmarks.sh`.
