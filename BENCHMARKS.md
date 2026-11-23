# Goosy — Benchmarks

> **Strict Honesty Policy**: No numbers are fabricated or estimated. All recorded values are directly measured on the host system.

## Environment
- **Host**: Artix Linux x86_64, Linux `7.3.4-1-cachyos`
- **Init**: `dinit 0.23.0`
- **Compositor**: `labwc 0.20.3` (Native Wayland)
- **CPU**: AMD Ryzen 5 3600 (12 threads)
- **RAM**: 24 GB
- **Target Performance Reference**: Foot 1.28.0

## Measured Results

### Idle Resource Utilization (`benchmarks/run_benchmarks.sh`)

| Terminal | Runtime / Language | Idle RSS | Idle CPU (2s window) | Window State |
| :--- | :--- | :--- | :--- | :--- |
| **Goosy 0.2.0** | Goose + Minimal C99 Bridge | **27,160 KiB** | **0 ticks** (0.0% CPU) | Native Wayland (CSD / SSD) |
| **Foot 1.28.0** | Pure C99 | **23,032 KiB** | **0 ticks** (0.0% CPU) | Native Wayland (SSD) |
| **Kitty** | Python + C | *Not Installed* | — | — |
| **Alacritty** | Rust | *Not Installed* | — | — |

*Key finding*: Goosy runs within ~4 MiB RSS of Foot (a mature pure-C terminal) while executing high-level safe Goose logic for its entire terminal state machine, UTF-8 parsing, and layout, and consumes 0 CPU ticks when idle by blocking completely on POSIX `epoll`.

### Native POSIX PTY Round-Trip (`tests/test_native_pty_poll.c`)

| Test Phase | Measured Time | Status |
| :--- | :--- | :--- |
| Security URL validation, PTY spawn, `epoll`/`signalfd`, `TIOCSWINSZ` resize to 120x40, echo round-trip, child reaping | **6 ms** | **PASS** |

### Goose Test Suite (`goose run tests/test_goosy.goose`)

| Suite | Status | Coverage |
| :--- | :--- | :--- |
| UTF-8 streaming decode, combining marks, East Asian wide chars | **PASS** | 100% |
| OSC 7 CWD parsing & OSC 133;A shell prompt jumps | **PASS** | 100% |
| Scrollback search, URL mode detection & Unicode hex input | **PASS** | 100% |
| Mouse single, double, triple, quad, quote & block selection | **PASS** | 100% |
| Shortcut dispatch & `goosy.ini` hex color `#` preservation | **PASS** | 100% |
| Fastfetch 2-column logo/sysinfo alignment & heavy box-drawing | **PASS** | 100% |
