# Goosy Production Readiness & Operational Reliability

## 1. Production Architecture Principles

Goosy has been engineered to meet stringent production reliability, security, and performance standards for native Wayland desktop environments:

1. **Minimal Dependency Surface:**
   - Goosy does not depend on GTK, Qt, KDE, GNOME, Electron, SDL, GLFW, or XWayland.
   - Core terminal logic is implemented in safe, statically typed Goose.
   - The native bridge is written in strict C99, requiring only foundational system libraries (`libwayland-client`, `pixman-1`, `libxkbcommon`, `freetype2`, `harfbuzz`, `libpng`, and optionally `vulkan` / `egl`).
2. **Universal Compatibility Fallback:**
   - The software Pixman backend (`wl_shm`) requires zero GPU acceleration, guaranteeing full operation inside headless VMs, containers, CI runners, and minimal chroots.
3. **Strict Input Sanitization & Threat Boundaries:**
   - All ANSI/VT sequences, Sixel/Kitty graphics streams, and OSC payloads from PTY shells and clipboards are treated as untrusted.

---

## 2. Production Reliability Verification

### A. Process & PTY Lifecycle
- **Child Process Reaping:** Monitored via Linux `signalfd` integrated into the central epoll event loop. Child exit (`SIGCHLD`) is detected immediately, avoiding zombie processes.
- **Window Geometry Synchronization:** Immediate `ioctl(TIOCSWINSZ)` dispatch on configure events guarantees child programs (`vim`, `tmux`, `htop`) reflect accurate rows and columns.
- **Descriptor Leaks:** All file descriptors (PTY master, epoll fd, signal fd, timer fd, shm fds) are created with `O_CLOEXEC` to prevent descriptor leakage into child processes.

### B. Memory Safety & Resource Caps
- **Sixel Graphics Cap:** Clamped to a maximum resolution of 4096×4096 pixels with repeat count verification to prevent memory exhaustion attacks.
- **Kitty Graphics Protocol:** Maximum decoded dimensions capped at 4096×4096 pixels; maximum payload size capped at 32 MiB. Chunked transfer reassembly buffer (`m=1`/`m=0`) enforces strict size ceilings.
- **PNG Decompressor:** Dimension pre-flight inspection via `png_read_info` rejects oversized or malformed image chunks.
- **Dynamic PTY Buffer:** Bounded dynamically up to 16 MiB with non-blocking write backpressure, eliminating silent paste truncation while preventing unbounded memory consumption.

### C. Wayland Protocol Resilience
- **Fractional Scaling:** Decouples 1:1 buffer rendering with `wp_viewport` to prevent compositor double-scaling artifacts on modern high-DPI displays (e.g. 125%, 150%, 175%).
- **Compositor Teardown:** Clean unbinding and release of Wayland registry listeners, fractional scale listeners, and frame callbacks on session exit.
- **Decoration Negotiation:** Seamless fallback between server-side decorations (`zxdg_decoration_manager_v1`) and client-side title bar rendering (`CSD`).

---

## 3. Deployment & Operational Checklist

| Operational Dimension | Status | Verification Method |
|:---|:---:|:---|
| **Compilation** | Production Ready | Builds cleanly with GCC 15 / Goose v2026.4 `-O2` |
| **Compiler Warnings** | Zero Warnings | `-Wall -Wextra` verified across all native C modules |
| **Unit Test Coverage** | 100% Passing | `tests/test_goosy.goose` pure Goose engine suite |
| **Native Integration** | 100% Passing | `tests/test_native_pty_poll.c` real PTY + epoll loop |
| **Security Fuzzing** | 100% Passing | `tests/test_security_fuzz.c` Sixel, Kitty, PNG bounds |
| **Multi-Backend Tests**| 100% Passing | `tests/test_backends.c` Pixman, OpenGL, Vulkan cycles |
| **Reproducible Benchmarks**| Verified | `benchmarks/comprehensive_benchmarks.py` |
| **Zero Idle Power** | Verified | 0 scheduler ticks during idle state |
| **CLI Management** | Complete | `--renderer`, `--renderer-info`, `--hold`, `-e`, `-D` |
