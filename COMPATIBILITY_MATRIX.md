# Goosy Compatibility Matrix

## Supported Standards & Protocols

| Protocol / Standard | Support Status | Implementation Details |
|:---|:---:|:---|
| **ECMA-48 / ANSI Escape Codes** | Full | SGR 0..107, cursor movement, erase commands (ED/EL), margins |
| **24-bit Truecolor** | Full | Semicolon syntax (`38;2;r;g;bm`) & colon ITU-T syntax (`38:2::r:g:bm`) |
| **DEC Private Modes** | Full | 1 (Application Cursor), 6 (Origin), 7 (Autowrap), 25 (Cursor Visibility), 47/1047/1049 (Alt Screen), 2004 (Bracketed Paste) |
| **Mouse Tracking** | Full | X10 compatibility (1000), Button-event tracking (1002), SGR extended coordinates (1006) |
| **OSC 7** | Full | Current Working Directory notification for child terminal spawning (`Ctrl+Shift+N`) |
| **OSC 8** | Full | Hyperlink specification with clickable URLs (`Ctrl+Click`) |
| **OSC 52** | Full | Clipboard read/write query with base64 validation |
| **OSC 133** | Full | Semantic Shell Integration prompt marking with prompt jumping (`Ctrl+Shift+Z/X`) and command output selection (`Ctrl+Shift+A`) |
| **Sixel Graphics** | Operational | Software decoding via internal color palette and Pixman placement (4096×4096 bounded) |
| **Kitty Graphics Protocol** | Operational | RGB, RGBA, PNG, chunked multi-packet assembly (`m=1`/`m=0`), targeted deletion by ID (`a=d, i=<id>`), query response |
| **Rendering: Vulkan Backend** | Full | Hardware GPU acceleration via `VK_KHR_wayland_surface` and embedded precompiled SPIR-V |
| **Rendering: OpenGL Backend** | Full | Hardware GPU acceleration via EGL and OpenGL ES 2.0+ via `wayland-egl` |
| **Rendering: Pixman Backend** | Full | High-performance software universal fallback using `wl_shm` |
| **Wayland xdg-shell** | Full | Direct Wayland client integration with ping/pong, configure, maximize/minimize |
| **Wayland CSD / SSD** | Full | `zxdg_decoration_manager_v1` decoration negotiation with custom CSD fallback |
| **Wayland Primary Selection** | Full | `zwp_primary_selection_device_manager_v1` copy-on-select & middle-click paste |
| **Wayland Fractional Scale** | Full | `wp_fractional_scale_v1` & `wp_viewporter` with decoupled 1:1 buffer scaling |
| **Font Rasterization** | Full | FreeType 2.x, Fontconfig, and HarfBuzz OpenType shaping with combining mark overlay |

---

## Verified Terminal Applications
- `bash`, `zsh`, `fish`, `sh`
- `neovim`, `vim`, `nano`
- `tmux`, `screen`
- `fastfetch`, `btop`, `htop`
- `less`, `more`, `fzf`
- `git` (diff, log, status)
