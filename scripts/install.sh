#!/usr/bin/env sh
# Rootless local user installer for Goot (no systemd, no root required)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PREFIX="${PREFIX:-$HOME/.local}"
BIN_DIR="$PREFIX/bin"
APP_DIR="$PREFIX/share/applications"
MAN_DIR="$PREFIX/share/man/man5"
CFG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/goot"

if [ ! -x "$ROOT_DIR/build/goot" ]; then
    echo "[goot] Binary not built yet; running build.sh..."
    "$ROOT_DIR/scripts/build.sh"
fi

mkdir -p "$BIN_DIR" "$APP_DIR" "$MAN_DIR" "$CFG_DIR"
install -m 755 "$ROOT_DIR/build/goot" "$BIN_DIR/goot"
install -m 644 "$ROOT_DIR/doc/goot.ini.5" "$MAN_DIR/goot.ini.5"

if [ ! -f "$CFG_DIR/goot.ini" ]; then
    install -m 644 "$ROOT_DIR/config/goot.ini" "$CFG_DIR/goot.ini"
    echo "[goot] Installed default config to $CFG_DIR/goot.ini"
else
    echo "[goot] Preserving existing config at $CFG_DIR/goot.ini"
fi

# Install application icon into hicolor theme
if [ -f "$ROOT_DIR/assets/goot.png" ]; then
    ICON_BASE="$PREFIX/share/icons/hicolor"
    mkdir -p "$PREFIX/share/pixmaps"
    install -m 644 "$ROOT_DIR/assets/goot.png" "$PREFIX/share/pixmaps/goot.png"
    for sz in 32 48 64 128 256 512; do
        mkdir -p "$ICON_BASE/${sz}x${sz}/apps"
        if command -v magick >/dev/null 2>&1; then
            magick "$ROOT_DIR/assets/goot.png" -resize "${sz}x${sz}" "$ICON_BASE/${sz}x${sz}/apps/goot.png"
        elif command -v convert >/dev/null 2>&1; then
            convert "$ROOT_DIR/assets/goot.png" -resize "${sz}x${sz}" "$ICON_BASE/${sz}x${sz}/apps/goot.png"
        else
            install -m 644 "$ROOT_DIR/assets/goot.png" "$ICON_BASE/${sz}x${sz}/apps/goot.png"
        fi
    done
fi

cat > "$APP_DIR/goot.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Goot
GenericName=Terminal Emulator
Comment=Native Wayland terminal emulator written in Goose
Exec=$BIN_DIR/goot
Icon=goot
Terminal=false
Categories=System;TerminalEmulator;
StartupNotify=false
EOF

echo "[goot] Installed to $BIN_DIR/goot and $MAN_DIR/goot.ini.5"
echo "[goot] Run 'man goot.ini' to view configuration and shortcut documentation."
