# Goosy — Architecture

## Design Philosophy
Goosy separates **terminal intelligence** (100% Goose) from **OS/display bindings** (minimal, auditable C99):

```
+-----------------------------------------------------------------------+
|                         Goosy (Goose Core)                             |
|  goosy.goose: epoll event loop, PTY batching, resize & input routing   |
|  src/parser.goose  -> VT100/VT220/ECMA-48 state machine               |
|  src/screen.goose  -> root struct Screen (cells, alt, scrollback)     |
|  src/utf8.goose    -> streaming UTF-8 decoder + wide/combining width  |
|  src/colors.goose  -> 16/256/24-bit palette + hex parser              |
|  src/input.goose   -> keysym -> VT sequences, shortcuts, mouse, paste |
|  src/config.goose  -> zero-heap TOML/INI parser                       |
|  src/renderer.goose-> per-row damage-tracked render coordinator       |
+-----------------------------------------------------------------------+
                           |  src/ffi.goose (extern fn, {data, len})
+-----------------------------------------------------------------------+
|                     Minimal C99 Bridge (native/)                      |
|  goosy_pty.c     -> Linux POSIX PTY (posix_openpt, fork, TIOCSWINSZ)   |
|  goosy_poll.c    -> epoll + signalfd (zero busy-wait idle loop)        |
|  goosy_wayland.c -> libwayland-client, xdg-shell, wl_shm, clipboard    |
|  goosy_xkb.c     -> libxkbcommon keymap & UTF-8 translation            |
|  goosy_font.c    -> FreeType2 + Fontconfig + HarfBuzz + 4096 cache     |
|  goosy_render.c  -> Pixman software compositing + damage union rect    |
+-----------------------------------------------------------------------+
```

## Goose Memory Model Alignment
- Per the [Goose Language Reference](https://github.com/aardappel/goose/blob/master/docs/reference.md) and [Root Structs Design](https://github.com/aardappel/goose/blob/master/docs/design/root_structs.md), Goose has no heap allocator, GC, or destructors; dynamic arrays live on statically assigned data stacks.
- `screen::Screen` is a `root struct` owning independent resizable arrays (`cells`, `alt_cells`, `sb_cells`, `dirty_rows`, `tab_stops`, `reply_buf`) under one owning lifetime in `main()`.
- Each `Cell` is a compact packed value (`cp: u32`, `combining: u32`, `fg: u32`, `bg: u32`, `attrs: u16`, `width: u8`, `flags: u8` = 20 bytes), keeping a 100x30 viewport at ~60 KiB and 4,000 lines of 100-column scrollback at ~8 MiB.
- Fixed-size stack buffers (`pty_buf: u8[16384]`, `clip_buf: u8[65536]`) prevent per-event allocation churn.

## Event-Driven Zero-Idle-CPU Loop
1. Flush pending Wayland requests (`wl_display_dispatch_pending` + `wl_display_flush`).
2. Block indefinitely in `epoll_wait(..., -1)` on three descriptors: Wayland display fd, PTY master fd, and `signalfd` (`SIGCHLD`, `SIGTERM`, `SIGINT`).
3. On PTY readiness, batch up to 4 non-blocking 16 KiB reads (64 KiB) before rendering so heavy output coalesces without redundant frame commits.
4. Repaint only rows flagged in `scr.dirty_rows`, accumulate a single damage bounding box in `goosy_render.c`, and submit via `wl_surface_damage_buffer` + `wl_surface_frame`.

## Security Controls
- **Bounded parser buffers**: `MAX_PARAMS = 16`, `MAX_OSC = 512`, integer parameter clamping (`< 100000`).
- **UTF-8 validation**: Rejects overlong encodings, UTF-16 surrogates (`U+D800..U+DFFF`), and `> U+10FFFF` with `U+FFFD`.
- **OSC 52**: Disabled by default (`allow_osc52 = false`).
- **Clipboard paste**: Strips `NUL` (`0x00`) and raw `ESC` (`0x1B`) to prevent bracketed-paste breakout injection.
- **Dimension guards**: Both Goose (`MAX_COLS = 512`, `MAX_ROWS = 256`) and C (`GOOSY_MAX_DIM = 8192`, multiplication overflow guard in `create_shm_buffer`) reject invalid dimensions.
