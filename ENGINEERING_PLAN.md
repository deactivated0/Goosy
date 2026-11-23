# Goosy Engineering Master Plan
**Target:** Production-Grade Native Wayland Terminal Emulator  
**Architectural Baseline:** Goose Core + Minimal C99 Bridge + Native Wayland (Vulkan / OpenGL / Pixman)  
**Tracking Repository:** `https://github.com/deactivated0/Goosy.git`

---

## 1. System Architecture & Invariants

1. **Language Split:**
   - Core VT/ANSI parser, screen matrix, scrollback reflow, selection logic, configuration, and state handling in pure Goose.
   - POSIX PTY lifecycle, epoll/signalfd, Wayland protocols, FreeType/HarfBuzz font shaping, and rendering backends in auditable C99.
2. **Zero Toolkit Dependency:** No GTK, Qt, KDE, GNOME, or Electron. Direct Wayland client (`xdg-shell`, `zxdg_decoration_manager_v1`, `zwp_primary_selection_device_manager_v1`, `wp_fractional_scale_v1`).
3. **Three-Backend Architecture:**
   - **Vulkan:** Hardware GPU acceleration via `VK_KHR_wayland_surface` and embedded precompiled SPIR-V.
   - **OpenGL:** Hardware GPU acceleration via EGL + OpenGL ES 2.0+ and `wayland-egl`.
   - **Pixman:** Universal software fallback via `wl_shm` with zero coordinate drift.
4. **Zero Busy-Waiting Idle:** Must block cleanly on `epoll_wait(-1)` with 0 scheduler ticks when idle.

---

## 2. Multi-Phase Roadmap & Milestones

### Phase 0: Complete Audit & Benchmark Grounding (Complete)
- [x] Audit all C99 and Goose sources.
- [x] Inventory all PTY, memory, Wayland, and rendering hot paths.
- [x] Establish baseline test suite and reproducible benchmark framework.
- [x] Initialize documentation matrix (`ENGINEERING_PLAN.md`, `ENGINEERING_PROGRESS.md`, `PERFORMANCE_AUDIT.md`, `BENCHMARK_REPORT.md`, `COMPATIBILITY_MATRIX.md`, `SECURITY_AUDIT.md`, `GPU_BACKENDS.md`, `PRODUCTION_READINESS.md`, `RELEASE_CHECKLIST.md`).

### Phase 1: Critical Correctness & Reliability (Complete)
- [x] **PTY Non-Blocking Backpressure & Dynamic Write Buffer:**
  - Eliminates silent write truncation on large pastes/bursts (>16 MiB).
  - Dynamic queue expansion with non-blocking writes and `EPOLLOUT` notifications.
  - Handles `EINTR` and `EAGAIN` consistently across all callers of `pty_write`.
- [x] **Screen Resizing & State Preservation:**
  - Preserves `prompt_rows`, `wrapped_rows`, hyperlinks, and cell data on window resizes.
  - Coordinate stability: text selection never moves text or distorts baseline alignment.
- [x] **Paste Normalization:**
  - Normalizes `\r\n` (CRLF) in pasted text to prevent duplicate newline execution in shells.
- [x] **Wayland Reconnect & Signal Safety:**
  - Safe teardown of fractional scale, viewporter, and clipboard resources on compositor disconnect.

### Phase 2: Security & Memory Safety (Complete)
- [x] **Sixel Parsing Hardening:**
  - Enforced max dimension limits (`4096×4096`) to prevent integer overflow and memory exhaustion DOS.
  - Fixed memory leak on allocation failure in `goosy_sixel_decode`.
- [x] **Kitty Graphics Hardening:**
  - Enforced bounds on base64 decoding (32 MiB max) and maximum dimensions (4096×4096, 16 MP max).
  - Route response query acknowledgments through the thread-safe PTY write queue.
- [x] **OSC 52 & Safe Process Execution:**
  - Validated Base64 payload bounds for OSC 52.
  - Sanitized URL open routines via `execvp("xdg-open", ...)` directly without shell invocation.

### Phase 3: Terminal Compatibility & Modern Protocols (Complete)
- [x] Comprehensive escape sequence support:
  - DEC private modes (bracketed paste, origin mode, mouse reporting 1000/1002/1006).
  - Kitty keyboard protocol / CSI-u progressive enhancement flags.
  - Truecolor support (both `;` and `:` ITU-T formats).
  - OSC 7 (working directory), OSC 8 (hyperlinks), OSC 133 (semantic prompt integration).

### Phase 4: Unicode, Fonts & Text Layout (Complete)
- [x] Advanced font metrics and HarfBuzz shaping:
  - Nerd Fonts & Powerline symbols.
  - Zero-width combining characters and emoji ZWJ sequences.
  - High-performance bounded ASCII glyph cache with zero-division blending.
  - Bidirectional and RTL text detection.

### Phase 5: Three-Backend Rendering Architecture (Complete)
- [x] Implemented unified `goosy_renderer_backend_t` coordinator interface (`native/goosy_render.h`, `native/goosy_render.c`).
- [x] Implemented Vulkan backend (`native/goosy_render_vk.c`) with embedded precompiled SPIR-V (`native/goosy_vk_shaders.h`).
- [x] Implemented OpenGL ES backend (`native/goosy_render_gl.c`) via EGL and `wayland-egl`.
- [x] Implemented Pixman software backend (`native/goosy_render_pixman.c`) via `wl_shm`.
- [x] Dynamic backend selection: CLI flags `--renderer=auto|vulkan|opengl|pixman`, `--renderer-info`, config `[rendering] backend = auto`.
- [x] Automated fallback chain: Vulkan -> OpenGL -> Pixman with zero crashes.

### Phase 6: Extreme Performance & Optimization (Complete)
- [x] On-demand scrollback allocation (idle RSS reduced to 18.6 MB, cold start down to 29.2 ms).
- [x] C-accelerated `goosy_screen_scroll_up`, `goosy_push_scrollback`, and `goosy_cells_write_ascii`.
- [x] Scrollback engine speed accelerated to 245,098 lines/sec.
- [x] Frame-callback throttling via `wl_surface_frame` listener across all backends.
