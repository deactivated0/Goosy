# Goosy — Progress Log

## Target Environment
- **OS**: Artix Linux x86_64, Linux `7.3.4-1-cachyos` (Dinit init system, strictly no systemd)
- **Display Server**: Native Wayland (`labwc 0.20.3` + QuickShell)
- **Compiler**: Goose v2026.4 + GCC C99
- **Libraries**: `wayland-client`, `wayland-protocols`, `wayland-cursor`, `pixman-1`, `freetype2`, `fontconfig`, `harfbuzz`, `libxkbcommon`

## Completed Deliverables

1. **Wayland Window Management & Title Bar**:
   - Implemented CSD title bar in `src/renderer.goose` and `goosy.goose`:
     - Drag to move window (`xdg_toplevel.move` with input serial).
     - Minimize button (`xdg_toplevel.set_minimized`).
     - Maximize / restore button and double-click title bar (`xdg_toplevel.set_maximized` / `unset_maximized`).
     - Close button (`running = false`).
     - Button hover and pressed visual states.
     - Accurate cursor shape switching (`pointer` on buttons, `default` on title bar, `text` in terminal area) via `wayland-cursor`.
     - CSD / SSD negotiation via `zxdg_decoration_manager_v1` protocol.
2. **Fastfetch Formatting & Terminal Grid**:
   - Set default window dimensions to `110x32` cells to accommodate Artix Linux logo (44 cols) + sysinfo lines without wrapping.
   - Accurately accounted for title bar height (`title_h`) in terminal rows calculation (`calc_grid`, `px_to_cell`, `adjust_font`, `pty_resize`).
   - Cleaned environment in PTY child process (`unsetenv("COLUMNS")`, `unsetenv("LINES")`, `unsetenv("TERMCAP")`).
   - Implemented pixel-perfect procedural rendering for all Box-Drawing (`U+2500..U+257F`) and Block Elements (`U+2580..U+259F`) directly via Pixman.
   - Added Fontconfig fallback face caching (`find_fallback_face`) in `native/goosy_font.c`.
   - Extended truecolor parser for ITU 6-param colon syntax (`38:2::r:g:b`).
3. **Wayland Primary Selection**:
   - Integrated `zwp_primary_selection_device_manager_v1` for inter-app primary selection copy and paste.
4. **PTY Backpressure & Write Ring Buffer**:
   - Implemented 64 KiB outgoing write ring buffer in `native/goosy_pty.c` and dynamic `EPOLLOUT` polling in `native/goosy_poll.c`.
5. **Config Parser Bug Fix**:
   - Fixed `#` hex color stripping in `src/config.goose`.
6. **Double-Buffering Synchronization**:
   - Implemented 2-frame dirty row lifetime (`s.dirty_rows[r] = 2`) and 2-frame full redraws (`s.full_redraw_frames = 2`) to ensure both Wayland SHM buffers remain synchronized.
7. **Testing & Benchmarks**:
   - Added Fastfetch alignment test to `tests/test_goosy.goose` (100% pass).
   - Executed native C PTY test `tests/test_native_pty_poll.c` (6 ms round-trip).
   - Executed benchmarks vs Foot: Goosy idle RSS 27.1 MB (0 ticks CPU), Foot idle RSS 23.0 MB (0 ticks CPU).
