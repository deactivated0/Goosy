# Goot

**Goot** is a native Wayland terminal emulator written primarily in the [Goose programming language](https://aardappel.github.io/goose/) by Wouter van Oortmerssen ([aardappel/goose](https://github.com/aardappel/goose)), with a minimal, auditable C99 interoperability layer for Linux POSIX PTY, `epoll`/`signalfd`, `libwayland-client`, `wayland-cursor`, `xdg-shell`, `zxdg_decoration_manager_v1`, `zwp_primary_selection_device_manager_v1`, Pixman software rendering, FreeType/Fontconfig/HarfBuzz font rasterization, `libxkbcommon`, and `wl_touch`.

Designed for **Artix Linux (x86_64, no systemd)** and lightweight Wayland compositors such as **labwc** + QuickShell, Goot targets Foot-class efficiency (idle 0 CPU ticks, ~27 MB RSS) and Foot-compatible keyboard, mouse, and touchscreen ergonomics.

## Features

- **Native Wayland Window Management & Title Bar**:
  - Full client-side decorations (CSD) with window move, minimize, maximize/restore toggle, and close.
  - Double-click title bar to maximize or restore.
  - Button hover and pressed visual states.
  - Automatic Wayland cursor shape changes (`text`, `default`, `pointer`).
  - Automatic CSD / SSD negotiation via `zxdg_decoration_manager_v1` (avoids duplicate title bars on SSD compositors like labwc).
- **Pixel-Perfect Font & Box Rendering**:
  - FreeType2, HarfBuzz advance metrics, and Fontconfig fallback face resolution.
  - Zero-gap procedural Pixman rendering for all Box-Drawing (`U+2500..U+257F`) and Block Elements (`U+2580..U+259F`).
  - Flawless 2-column alignment and zero line-wrap displacement for `fastfetch` and complex TUIs.
  - ITU 6-parameter colon (`38:2::r:g:b`) and standard semicolon (`38;2;r;g;b`) 24-bit truecolor support.
- **Wayland Selections & Clipboard**:
  - Standard clipboard (`Ctrl+Shift+C` / `Ctrl+Shift+V` / OSC 52).
  - True Wayland primary selection copy on text release and paste (`Shift+Insert` / middle-click) via `zwp_primary_selection_device_manager_v1`.
- **PTY Backpressure & Zero-Idle-CPU Architecture**:
  - 64 KiB outgoing write ring buffer with dynamic `EPOLLOUT` notifications.
  - 2-frame double buffer damage tracking to prevent visual artifacts across buffer flips.
  - Blocks completely on `epoll_wait(-1)` when idle.

## Prerequisites (Artix Linux x86_64)

1. Install the real [Goose compiler](https://github.com/aardappel/goose) (`goose` in `PATH`).
2. Install required native development libraries (no systemd):

```sh
sudo pacman -S base-devel pkgconf wayland wayland-protocols wayland-cursor pixman freetype2 fontconfig harfbuzz libxkbcommon
```

## Build, Run & Install

```sh
# 1. Generate Wayland protocol bindings, typecheck with goose check, and build native binary
./scripts/build.sh

# 2. Run directly inside Wayland (e.g., labwc)
./build/goot

# 3. Rootless local user install (~/.local/bin/goot, ~/.config/goot/goot.ini, man goot.ini)
./scripts/install.sh

# 4. Clean uninstall (preserves ~/.config/goot/goot.ini)
./scripts/uninstall.sh
```

## Shortcuts

These are the default shortcuts. See `man goot.ini` and [`config/goot.ini`](config/goot.ini) for configuration details.

### Keyboard — Normal Mode
- `Shift+PageUp` / `Shift+PageDown`: Scroll up/down in history
- `Ctrl+Shift+C`, `XF86Copy`: Copy selected text to the clipboard
- `Ctrl+Shift+V`, `XF86Paste`: Paste from clipboard
- `Shift+Insert`: Paste from primary selection
- `Ctrl+Shift+R`: Start scrollback search
- `Ctrl++`, `Ctrl+=`: Increase font size
- `Ctrl+-`: Decrease font size
- `Ctrl+0`: Reset font size
- `Ctrl+Shift+N`: Spawn a new Goot terminal (starts in current working directory when shell emits OSC 7)
- `Ctrl+Shift+O`: Enter URL mode (jump labels open URLs safely via `xdg-open`)
- `Ctrl+Shift+U`: Enter Unicode input mode
- `Ctrl+Shift+Z` / `Ctrl+Shift+X`: Jump to previous/next prompt (requires OSC 133;A shell integration)

### Keyboard — Scrollback Search
- `Ctrl+R` / `Ctrl+S`: Search backward/forward for next match
- `Ctrl+W`: Extend current selection/search to end of word (or next word)
- `Ctrl+Shift+W`: Extend current selection/search to next whitespace boundary
- `Ctrl+V`, `Ctrl+Shift+V`, `Ctrl+Y`, `XF86Paste`: Paste clipboard into search buffer
- `Shift+Insert`: Paste primary selection into search buffer
- `Escape`, `Ctrl+G`: Cancel search
- `Return`: Finish search and copy current match to primary selection

### Keyboard — URL Mode
- `t`: Toggle whether the URL is displayed inside the jump label
- `Escape`, `Ctrl+C`, `Ctrl+G`, `Ctrl+D`: Exit URL mode without launching any URLs

### Mouse & Window Management
- `Title bar click & drag`: Move window
- `Title bar double click`: Maximize / restore window
- `Title bar buttons`: Minimize, Maximize / Restore, Close
- `Left` single-click + drag: Select text (copied to primary selection on release; hold `Shift` to select while client mouse tracking is active; hold `Ctrl` for block selection)
- `Left` double-click: Select word (hold `Ctrl` to select up to whitespace)
- `Left` triple-click: Select between enclosing quotes, or entire row if not inside quotes
- `Left` quad-click: Select entire row
- `Middle`: Paste from primary selection
- `Right` / `Ctrl+Right`: Extend current selection (click or drag; `Ctrl` forces character-wise selection)
- `Wheel` / `Ctrl+Wheel`: Scroll up/down in history / increase or decrease font size

### Touchscreen
- `Tap`: Emulates mouse left button click
- `Drag`: Scrolls up/down in history
- `Long-press + Drag` (`[touch] long-press-delay` in `goot.ini`, default 400 ms): Emulates mouse dragging with left button held

## Testing & Benchmarking

```sh
# Native POSIX PTY + epoll/signalfd + URL security C unit tests
gcc -O2 -Wall -Wextra -Werror -std=c99 -Inative \
    native/goot_pty.c native/goot_poll.c tests/test_native_pty_poll.c \
    -o /tmp/test_native_pty_poll && /tmp/test_native_pty_poll

# Goose terminal engine unit tests (requires Goose compiler)
goose check tests/test_goot.goose
goose run tests/test_goot.goose

# Reproducible Wayland benchmark vs Foot / Kitty / Alacritty
./benchmarks/run_benchmarks.sh
```
