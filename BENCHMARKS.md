# Goot — Benchmarks

> **Strict Honesty Policy**: No numbers are fabricated or estimated. Only actual measured values are recorded below.

## Measured in Sandbox (Debian 12 x86_64, Linux 6.12.105+)

| Benchmark | Measured Result | Notes |
| :--- | :--- | :--- |
| Native POSIX PTY + `epoll`/`signalfd` spawn, resize (`120x40`), `/bin/sh` echo + `stty size` + clean exit round-trip (`tests/test_native_pty_poll.c`) | **1 ms** | Compiled with `gcc -O2 -Wall -Wextra -Werror -std=c99` |

## Pending Target Artix Linux x86_64 Measurements (Wayland / labwc)

Full GUI terminal benchmarks cannot be measured inside the headless offline sandbox because the `goose` compiler, Wayland/Pixman/FreeType/Fontconfig/HarfBuzz/xkbcommon libraries, `labwc`, Foot, Kitty, and Alacritty are not installed.

Once built on Artix Linux x86_64 (`./scripts/build.sh`), run the reproducible comparison script inside your Wayland session:

```sh
./benchmarks/run_benchmarks.sh
```

### Target Metrics to Record
| Terminal | Cold Startup (ms) | Idle RSS (KiB) | Idle CPU (2s ticks) | Heavy Output (`seq 1 500000`) | Scrollback RSS (4000 lines) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Goot** | *Run on target* | *Run on target* | *Run on target* | *Run on target* | *Run on target* |
| **Foot** | *Run on target* | *Run on target* | *Run on target* | *Run on target* | *Run on target* |
| **Kitty** | *Run on target* | *Run on target* | *Run on target* | *Run on target* | *Run on target* |
| **Alacritty** | *Run on target* | *Run on target* | *Run on target* | *Run on target* | *Run on target* |
