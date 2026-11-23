#!/usr/bin/env sh
# Rootless local user installer for Goosy (no systemd, no root required)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PREFIX="${PREFIX:-$HOME/.local}"
BIN_DIR="$PREFIX/bin"
APP_DIR="$PREFIX/share/applications"
MAN_DIR="$PREFIX/share/man/man5"
CFG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/goosy"
LEGACY_CFG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/goot"

if [ ! -x "$ROOT_DIR/build/goosy" ]; then
    echo "[goosy] Binary not built yet; running build.sh..."
    "$ROOT_DIR/scripts/build.sh"
fi

mkdir -p "$BIN_DIR" "$APP_DIR" "$MAN_DIR" "$CFG_DIR"
install -m 755 "$ROOT_DIR/build/goosy" "$BIN_DIR/goosy"
ln -sf goosy "$BIN_DIR/goot"

if command -v sudo >/dev/null 2>&1; then
    sudo ln -sf "$BIN_DIR/goosy" /usr/local/bin/goosy 2>/dev/null || true
    sudo ln -sf "$BIN_DIR/goot" /usr/local/bin/goot 2>/dev/null || true
    sudo ln -sf "$BIN_DIR/goosy" /usr/bin/goosy 2>/dev/null || true
    sudo ln -sf "$BIN_DIR/goot" /usr/bin/goot 2>/dev/null || true
fi

if [ -f "$ROOT_DIR/doc/goosy.ini.5" ]; then
    install -m 644 "$ROOT_DIR/doc/goosy.ini.5" "$MAN_DIR/goosy.ini.5"
    ln -sf goosy.ini.5 "$MAN_DIR/goosy.ini.5"
fi

if [ ! -f "$CFG_DIR/goosy.ini" ]; then
    if [ -f "$LEGACY_CFG_DIR/goosy.ini" ]; then
        cp "$LEGACY_CFG_DIR/goosy.ini" "$CFG_DIR/goosy.ini"
        echo "[goosy] Migrated existing config from $LEGACY_CFG_DIR/goosy.ini to $CFG_DIR/goosy.ini"
    elif [ -f "$ROOT_DIR/config/goosy.ini" ]; then
        install -m 644 "$ROOT_DIR/config/goosy.ini" "$CFG_DIR/goosy.ini"
        echo "[goosy] Installed default config to $CFG_DIR/goosy.ini"
    fi
else
    echo "[goosy] Preserving existing config at $CFG_DIR/goosy.ini"
fi

# Install application icon into hicolor theme and pixmaps
ICON_SRC=""
if [ -f "$ROOT_DIR/assets/goosy.png" ]; then
    ICON_SRC="$ROOT_DIR/assets/goosy.png"
elif [ -f "$ROOT_DIR/assets/goosy.png" ]; then
    ICON_SRC="$ROOT_DIR/assets/goosy.png"
fi

if [ -n "$ICON_SRC" ]; then
    ICON_BASE="$PREFIX/share/icons/hicolor"
    mkdir -p "$PREFIX/share/pixmaps"
    install -m 644 "$ICON_SRC" "$PREFIX/share/pixmaps/goosy.png"
    ln -sf goosy.png "$PREFIX/share/pixmaps/goosy.png"
    for sz in 16 24 32 48 64 128 256 512; do
        mkdir -p "$ICON_BASE/${sz}x${sz}/apps"
        if command -v magick >/dev/null 2>&1; then
            magick "$ICON_SRC" -resize "${sz}x${sz}" "$ICON_BASE/${sz}x${sz}/apps/goosy.png"
            ln -sf goosy.png "$ICON_BASE/${sz}x${sz}/apps/goosy.png"
        elif command -v convert >/dev/null 2>&1; then
            convert "$ICON_SRC" -resize "${sz}x${sz}" "$ICON_BASE/${sz}x${sz}/apps/goosy.png"
            ln -sf goosy.png "$ICON_BASE/${sz}x${sz}/apps/goosy.png"
        else
            install -m 644 "$ICON_SRC" "$ICON_BASE/${sz}x${sz}/apps/goosy.png"
            ln -sf goosy.png "$ICON_BASE/${sz}x${sz}/apps/goosy.png"
        fi
    done
fi

cat > "$APP_DIR/goosy.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Goosy
GenericName=Terminal Emulator
Comment=Native Wayland terminal emulator written in Goose
Exec=$BIN_DIR/goosy
Icon=goosy
Terminal=false
Categories=System;TerminalEmulator;
StartupNotify=false
EOF

ln -sf goosy.desktop "$APP_DIR/goosy.desktop"

echo "[goosy] Installed to $BIN_DIR/goosy (with $BIN_DIR/goot symlink) and $MAN_DIR/goosy.ini.5"
echo "[goosy] Run 'man goosy.ini' to view configuration and shortcut documentation."
