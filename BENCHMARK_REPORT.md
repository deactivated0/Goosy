# Goosy Empirical Benchmark Report

## Hardware & Environment Specification
- **Operating System:** Linux 7.3.4-1-cachyos (x86_64)
- **Display Server:** Native Wayland (Wayland 1.23, `wayland-0`)
- **CPU:** AMD Ryzen (x86_64)
- **Compiler:** Goose v2026.4, GCC 15/2026 (`-O2 -march=native`)
- **Measurement Methodology:**
  - Memory: `/proc/$PID/smaps_rollup` (Proportional Set Size `Pss` & Resident Set Size `Rss`) after 2.0s settling.
  - Idle CPU: `/proc/$PID/stat` user + system CPU ticks sampled over 2000 ms.
  - Startup: Average over 10 cold runs invoking `true` via POSIX spawner.
  - Throughput: Streaming 60,000 lines of structured text (~3.9 MB).

---

## Cross-Terminal Comparative Matrix

| Terminal Emulator | Binary Size (MB) | Cold Startup (ms) | Idle RSS (MB) | Idle PSS (MB) | Idle CPU Ticks | Stream Throughput (MB/s) |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| **Goosy** (`goot`) | **0.30 MB** | **46.1 ms** | **38.6 MB** | **28.5 MB** | **0 ticks** | **10.4 MB/s** |
| **Foot** | 0.48 MB | 50.4 ms | 21.0 MB | 8.0 MB | 0 ticks | 37.6 MB/s |
| **Alacritty** | 8.30 MB | 150.3 ms | 94.2 MB | 38.0 MB | 0 ticks | 21.3 MB/s |
| **Kitty** | 0.10 MB* | 260.4 ms | 125.0 MB | 58.0 MB | 0 ticks | 15.1 MB/s |
| **Konsole** | 0.05 MB* | 266.4 ms | 103.6 MB | 49.9 MB | 0 ticks | 10.3 MB/s |

*\*Note: Kitty and Konsole binary sizes are small launcher stubs. Real dynamic runtime library weights are 81.35 MiB and 10.51 MiB respectively. Goosy is fully self-contained.*

---

## Internal Engine Microbenchmarks

| Component | Target Operation | Metric Result |
|:---|:---|:---|
| **Scrollback Engine** | 50,000 line scrollback operations | **30,921 lines/s** (1,617 ms) |
| **Render Compositor** | 100 frames @ 120×40 cells (480,000 cells) | **751.9 FPS** (133 ms, 3,609,022 cells/s) |
| **Font Glyph Cache** | 1,000,000 ASCII glyph lookups | **71,428,571 lookups/s** (14 ms) |
| **PTY Round-Trip** | POSIX PTY spawn + epoll command dispatch | **4.0 ms** |
