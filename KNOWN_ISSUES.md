# Goot — Known Issues & Remaining Blockers

## Environmental Blockers (Sandbox)
1. **Goose compiler not installed in sandbox**: `goose check` and `goose build` could not be executed in the offline Debian sandbox container. All `.goose` files and `goot.build` were written strictly against the official Goose documentation ([tutorial](https://aardappel.github.io/goose/docs/tutorial.html), [reference](https://github.com/aardappel/goose/blob/master/docs/reference.md), [packages](https://aardappel.github.io/goose/docs/packages.html)), but first-compile typecheck adjustments may be needed if minor upstream compiler syntax changes landed after November 2026.
2. **Wayland / Pixman / FreeType / Fontconfig / HarfBuzz / xkbcommon dev libraries absent in sandbox**: `native/goot_wayland.c`, `native/goot_xkb.c`, `native/goot_font.c`, and `native/goot_render.c` could not be compiled in the sandbox; only `native/goot_pty.c` and `native/goot_poll.c` were compiled and unit-tested in-container.
3. **Interactive Wayland compatibility & benchmarks pending**: Live testing under `labwc` with Bash, Fish, Zsh, Vim, Neovim, Nano, Htop, Btop, Less, Tmux, Fastfetch, OpenCode, and Claude Code must be executed on the target Artix Linux x86_64 machine.

## Current Technical Limitations (v0.1.0)
1. **HiDPI fractional scaling**: Integer `wl_surface_set_buffer_scale` is wired; Wayland `wp-fractional-scale-v1` is not yet bound.
2. **Server-side decorations (`xdg-decoration-unstable-v1`)**: Currently uses minimal borderless `xdg-shell` toplevel (compositor-managed borders in `labwc`). Adding `zxdg_decoration_manager_v1` is a straightforward follow-up.
3. **HarfBuzz complex script shaping**: `hb_ft_font_t` is initialized in `native/goot_font.c`, but the cell loop currently renders per-cell codepoints (base + single combining mark). Full ligature / RTL line shaping across cells is deferred to v0.2.
4. **Key repeat**: `wl_keyboard.repeat_info` is received; compositor-rate key repeat timer via `timerfd` in `epoll` is not yet wired (single keypresses work immediately).
5. **Double-buffer partial damage synchronization**: When `s.full_redraw == false`, only dirty rows are redrawn onto the acquired buffer; on compositors that ping-pong between two `wl_shm` buffers, tracking per-buffer dirty age (or copying previous damaged rows) should be enabled if artifacts appear during partial updates.
6. **Scrollback reflow on resize**: Resizing currently preserves top-left cells without line-wrapping reflow from scrollback.
