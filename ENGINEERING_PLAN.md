# Goosy Engineering Master Plan
**Target:** Production-Grade Native Wayland Terminal Emulator  
**Architectural Baseline:** Goose Core + Minimal C99 Bridge + Native Wayland (Pixman)  
**Tracking Repository:** `https://github.com/deactivated0/Goosy.git`

---

## 1. System Architecture & Invariants

1. **Language Split:**
   - Core VT/ANSI parser, screen matrix, scrollback reflow, selection logic, configuration, and state handling in pure Goose.
   - POSIX PTY lifecycle, epoll/signalfd, Wayland protocols, FreeType/HarfBuzz/Fontconfig font shaping, and Pixman rasterization in auditable C99.
2. **Zero Toolkit Dependency:** No GTK, Qt, KDE, GNOME, or Electron. Direct Wayland client (`xdg-shell`, `zxdg_decoration_manager_v1`, `zwp_primary_selection_device_manager_v1`, `wp_fractional_scale_v1`).
3. **Software-Render First:** Pixman software rasterization is always available and fully functional, with zero coordinate-drift, zero baseline jumps, and zero font scaling artifacts.
4. **Zero Busy-Waiting Idle:** Must block cleanly on `epoll_wait(-1)` with 0 scheduler ticks when idle.

---

## 2. Multi-Phase Roadmap & Milestones

### Phase 0: Complete Audit & Benchmark Grounding (Current)
- [x] Audit all C99 and Goose sources.
- [x] Inventory all PTY, memory, Wayland, and rendering hot paths.
- [x] Establish baseline test suite and reproducible benchmark framework.
- [ ] Initialize documentation matrix (`ENGINEERING_PLAN.md`, `ENGINEERING_PROGRESS.md`, `BENCHMARK_REPORT.md`, `COMPATIBILITY_MATRIX.md`, `SECURITY_AUDIT.md`, `RELEASE_CHECKLIST.md`).

### Phase 1: Critical Correctness & Reliability
- [ ] **PTY Non-Blocking Backpressure & Dynamic Write Buffer:**
  - Eliminate silent write truncation on large pastes/bursts (>64 KiB).
  - Dynamic queue expansion or blocking retry loop with `poll()` on `EPOLLOUT`.
  - Handle `EINTR` and `EAGAIN` consistently across all callers of `pty_write`.
- [ ] **Screen Resizing & State Preservation:**
  - Preserve `prompt_rows`, `wrapped_rows`, hyperlinks, and cell data on height-only and width resizes.
  - Coordinate stability: Ensure selecting text never triggers visual displacement or font layout jitter.
- [ ] **Paste Normalization:**
  - Normalize `\r\n` (CRLF) in pasted text to prevent duplicate newline execution in shells.
- [ ] **Wayland Reconnect & Signal Safety:**
  - Safe teardown of fractional scale, viewporter, and clipboard resources on compositor disconnect.

### Phase 2: Security & Memory Safety
- [ ] **Sixel Parsing Hardening:**
  - Enforce max dimension limits (e.g. 4096×4096) to prevent integer overflow and memory exhaustion DOS.
  - Fix memory leak on `tight` allocation failure in `goosy_sixel_decode`.
- [ ] **Kitty Graphics Hardening:**
  - Enforce bounds on base64 decoding and payload lengths.
  - Route response query acknowledgments through the thread-safe PTY write queue instead of direct raw `write()`.
  - Max dimension and pixel count bounds for raw RGB/RGBA and PNG decodes.
- [ ] **OSC 52 & Safe Process Execution:**
  - Validate Base64 payload bounds for OSC 52.
  - Reject dangerous schemes (`file://`, shell scripts) in URL open routines; use sanitized `xdg-open` with `execv` (never `system()` or shell expansion).

### Phase 3: Terminal Compatibility & Modern Protocols
- [ ] Comprehensive escape sequence support:
  - DEC private modes (bracketed paste, origin mode, mouse reporting 1000/1002/1006).
  - Kitty keyboard protocol / CSI-u progressive enhancement flags.
  - Truecolor support (both `;` and `:` ITU-T formats).
  - OSC 7 (current working directory), OSC 8 (hyperlinks), OSC 133 (semantic prompt integration).

### Phase 4: Unicode, Fonts & Text Layout
- [ ] Advanced font metrics and HarfBuzz shaping:
  - Nerd Fonts & Powerline symbols.
  - Zero-width combining characters and emoji ZWJ sequences.
  - High-performance bounded ASCII glyph cache with zero-division blending.
  - Bidirectional and RTL text detection.

### Phase 5: Wayland Compositor & Surface Rendering
- [ ] True fractional scaling (1.0, 1.25, 1.5, 1.75, 2.0) with `wp_fractional_scale_v1` and `wp_viewport`.
- [ ] Pixman damaged-tile and row compositing optimizations.
- [ ] Clean multi-seat and focus handling across output transitions.

### Phase 6: Graphics Protocols (Sixel & Kitty)
- [ ] Complete chunked Kitty graphics transmissions.
- [ ] Targeted deletion by ID, placement IDs, memory eviction policies.

### Phase 7: Benchmarks & Reproducibility
- [ ] Eliminate hardcoded user paths (`/home/pc/...`) in benchmark harness.
- [ ] Provide statistical distributions (median, p95, standard deviation) across multi-run benchmarks.
- [ ] Compare against Foot, Alacritty, Kitty, and Konsole under identical Wayland environments.

### Phase 8 & 9: UX, Features & Quality
- [ ] Search improvements (interactive reverse search with match highlighting).
- [ ] URL picker mode and code point entry mode.
- [ ] Clean CSD title bar with double-click toggle maximize and button hover states.
- [ ] Config reloads and custom palettes.
