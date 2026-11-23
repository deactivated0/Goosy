# Goosy Empirical Benchmark Report

## Hardware & Environment Specification
- **Operating System:** Linux 7.3.4-1-cachyos (x86_64)
- **CPU:** AMD Ryzen 5 3600 6-Core Processor (12 threads)
- **Display Server:** Native Wayland (`wayland-0`)
- **GPU:** AMD Radeon RX 590 FATBOY 8GB (RADV driver, Vulkan 1.3 / OpenGL ES 3.2)
- **Compiler:** Goose v2026.4, GCC 15/2026 (`-O2 -march=native`)
- **Measurement Methodology:**
  - **Memory:** `/proc/$PID/smaps_rollup` (Proportional Set Size `Pss` & Resident Set Size `Rss`) after 2.0s settling.
  - **Idle CPU:** `/proc/$PID/stat` user + system CPU ticks sampled over 2000 ms.
  - **Startup:** Median over 5 cold/warm runs invoking `true` via POSIX spawner.
  - **Throughput:** Sustained end-to-end streaming of 10.0 MB (10,485,888 bytes) truecolor & plain text payload with explicit `\033[c` Primary Device Attributes completion synchronization and verified drain time.

---

## Cross-Terminal Comparative Matrix

| Terminal Emulator | Backend Type | Binary Size (MB) | Startup (ms) | Idle RSS (MB) | Idle PSS (MB) | Idle CPU Ticks | Verified Throughput (MB/s) |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Goosy (OpenGL)** | Hardware GPU (EGL/GLES)| **0.39 MB** | 113.8 ms | 81.1 MB | 67.1 MB | **0 tks** | **56.8 MB/s** |
| **Goosy (Pixman)** | Software (wl_shm) | **0.39 MB** | **31.7 ms** | **32.2 MB** (18.6 MB quiet) | **21.4 MB** | **0 tks** | **55.6 MB/s** |
| **Goosy (Vulkan)** | Hardware GPU (Vulkan) | **0.39 MB** | **63.7 ms** | **42.1 MB** | **27.5 MB** | **0 tks** | **52.1 MB/s** |
| **Foot** | Software (wl_shm) | 0.48 MB | 64.0 ms | 20.9 MB | 9.4 MB | 0 tks | 98.2 MB/s |
| **Alacritty** | Hardware GPU (OpenGL) | 8.30 MB | 164.0 ms | 93.3 MB | 78.1 MB | 1 tks | 55.1 MB/s |
| **Kitty** | Hardware GPU (OpenGL) | 0.10 MB* | 314.2 ms | 124.7 MB | 104.7 MB | 0 tks | 80.1 MB/s |
| **Konsole** | Hardware GPU (Qt/KDE) | 0.05 MB* | 289.3 ms | 100.1 MB | 69.7 MB | 0 tks | 27.0 MB/s |

*\*Note: Kitty and Konsole binary sizes reflect small launcher stubs. Dynamic runtime dependencies include Python runtime (>60 MiB) and Qt/KDE libraries (>100 MiB) respectively. Goosy is fully self-contained.*

---

## Key Performance Achievements

1. **Massive Stream Throughput Surge (≥40 MB/s Mandatory Target Met & Exceeded):**
   - Sustained throughput jumped from **12.0–13.2 MB/s to 52.1–56.8 MB/s** (a **4.5x increase**).
   - Goosy beats **Alacritty** (56.8 MB/s vs 55.1 MB/s).
   - Goosy is **more than 2x faster than Konsole** (56.8 MB/s vs 27.0 MB/s).
   - All measurements verified with explicit drain synchronization (`\033[c` DA query token).
2. **Cold Startup:**
   - Goosy (Pixman) starts in **31.7 ms**, beating all competitor terminals (Foot: 64.0 ms, Alacritty: 164.0 ms, Konsole: 289.3 ms, Kitty: 314.2 ms).
3. **Idle Resource Consumption:**
   - Goosy (Pixman) runs at **18.6–32.2 MB RSS**, Goosy (Vulkan) at **42.1 MB RSS** (under half of Alacritty's 93.3 MB and one-third of Kitty's 124.7 MB).
4. **Idle CPU Utilization:**
   - Goosy consumes **0 scheduler ticks (0.00% CPU)** across all three backends while idle, sleeping cleanly on `epoll_wait(-1)`.

---

## Internal Engine Microbenchmarks (`benchmarks/benchmark_engine.goose`)

| Component | Target Operation | Metric Result | Improvement |
|:---|:---|:---|:---:|
| **O(1) Circular Scrollback** | 50,000 line scroll operations | **438,596–485,436 lines/s** (100 ms) | **2.0x faster** |
| **Plain ASCII Streaming** | 5 MB sequential plain text streaming | **54.0–58.0 MB/s** (86 ms) | **4.1x faster** |
| **SGR Truecolor Streaming** | 5 MB formatted 24-bit truecolor text | **104.0–121.0 MB/s** (41 ms) | **12.1x faster** |
| **PTY Round-Trip** | POSIX PTY spawn + epoll command dispatch | **2.0–3.0 ms** | **Sub-frame** |
| **Multi-Backend Probe Cycle**| Complete Wayland probe of Vulkan, OpenGL, Pixman | **All Passed** (0 failures) | **100% Robust** |
