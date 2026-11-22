#!/usr/bin/env sh
# Goot build script — Artix Linux x86_64 (no systemd required)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

echo "[goot] Checking Goose compiler..."
if ! command -v goose >/dev/null 2>&1; then
    echo "ERROR: 'goose' compiler not found in PATH." >&2
    echo "Install from https://github.com/aardappel/goose and ensure 'goose' is in PATH." >&2
    exit 1
fi

# Guard against similarly named tools (pressly/goose DB migrator or AI agent)
if goose --help 2>&1 | grep -qiE "database|migration|providers"; then
    echo "ERROR: 'goose' in PATH is not Wouter van Oortmerssen's Goose language compiler." >&2
    echo "See https://aardappel.github.io/goose/ for the real Goose compiler." >&2
    exit 1
fi

goose version || true
goose toolchain || true

echo "[goot] Checking required native tools and development libraries..."
for tool in pkg-config wayland-scanner; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: Required tool '$tool' not found." >&2
        exit 1
    fi
done

REQUIRED_LIBS="wayland-client wayland-protocols pixman-1 freetype2 fontconfig harfbuzz xkbcommon"
MISSING=""
for lib in $REQUIRED_LIBS; do
    if ! pkg-config --exists "$lib"; then
        MISSING="$MISSING $lib"
    fi
done

if [ -n "$MISSING" ]; then
    echo "ERROR: Missing development packages:$MISSING" >&2
    echo "On Artix Linux, install with:" >&2
    echo "  sudo pacman -S wayland wayland-protocols pixman freetype2 fontconfig harfbuzz libxkbcommon" >&2
    exit 1
fi

echo "[goot] Generating Wayland xdg-shell protocol bindings..."
WL_PROTO_DIR=$(pkg-config --variable=pkgdatadir wayland-protocols)
XDG_XML="$WL_PROTO_DIR/stable/xdg-shell/xdg-shell.xml"
if [ ! -f "$XDG_XML" ]; then
    echo "ERROR: Could not locate xdg-shell.xml at $XDG_XML" >&2
    exit 1
fi

mkdir -p native/generated build
wayland-scanner client-header "$XDG_XML" native/generated/xdg-shell-client-protocol.h
wayland-scanner private-code  "$XDG_XML" native/generated/xdg-shell-protocol.c

echo "[goot] Typechecking Goose sources (goose check)..."
goose check goot.goose

echo "[goot] Building native executable (goose build -O2)..."
goose build -O2 -o build/goot goot.goose

echo "[goot] Build succeeded: $ROOT_DIR/build/goot"
