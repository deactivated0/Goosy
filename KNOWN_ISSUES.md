# Goot — Known Issues & Resolution Status

## Resolved in v0.2.0

1. **Native Wayland Title Bar & Window Management**:
   - **Resolved**: Integrated client-side decorations (CSD) and server-side decoration negotiation (`zxdg_decoration_manager_v1`). Goot now provides a functional Pixman-rendered title bar featuring window drag/move (`xdg_toplevel.move`), minimize (`xdg_toplevel.set_minimized`), maximize/restore toggle (`xdg_toplevel.set_maximized` / `unset_maximized`), double-click title bar to maximize, button hover/press states, and interactive cursor shape switching (`default`, `pointer`, `text`) via `wayland-cursor`.
2. **Fastfetch Output Formatting & Alignment**:
   - **Resolved**: Addressed all root causes:
     - Set initial terminal geometry to `110x32` so modern system information strings (e.g. 73-char font strings) and 44-col logos fit cleanly without premature line wrapping.
     - Properly subtracted title bar height (`title_h`) from effective terminal rows so PTY rows report exact content geometry (`ws_row`, `ws_ypixel`).
     - Added procedural zero-gap rendering for all Box-Drawing (`U+2500..U+257F`) and Block Elements (`U+2580..U+259F`), ensuring progress bars (`━━━━━───────────────`) connect seamlessly without subpixel gaps.
     - Added Fontconfig dynamic fallback face resolution (`find_fallback_face`) for missing symbols.
     - Cleared `wrap_pending` state upon erase commands (`ED`/`EL`).
3. **Primary Selection Integration**:
   - **Resolved**: Bound `zwp_primary_selection_device_manager_v1` to support true inter-client Linux primary selection copy and paste (middle-click paste / `Shift+Insert`).
4. **PTY Backpressure & Write Ring Buffer**:
   - **Resolved**: Implemented a 64 KiB non-blocking write ring buffer in `goot_pty.c` and integrated `EPOLLOUT` notification in `goot_poll.c` to prevent data loss or blocking on high-volume terminal output.
5. **Config Hex Color Parsing**:
   - **Resolved**: Fixed INI parser to distinguish hex color hash prefixes (`#1a1b26`) from inline comments, preserving custom palette colors.
6. **Double-Buffering Damage Synchronization**:
   - **Resolved**: Implemented 2-frame dirty row lifetime (`s.dirty_rows[r] = 2`) and 2-frame full redraw tracking (`s.full_redraw_frames = 2`) ensuring both active and inactive Wayland SHM buffers remain synchronized across partial redraws with zero flickering.

## Current Technical Characteristics
1. **Fractional HiDPI Scaling**: Integer buffer scaling (`wl_surface_set_buffer_scale`) is fully supported; compositor fractional scaling protocol (`wp-fractional-scale-v1`) is planned for future extension.
2. **HarfBuzz Complex Script Shaping**: Individual codepoint glyph rasterization with HarfBuzz advance caching is used for monospaced cells; complex multi-codepoint RTL / Arabic cursive shaping across cell boundaries is a future roadmap item.
