#!/usr/bin/env sh
# Clean rootless uninstaller for Goot
set -eu

PREFIX="${PREFIX:-$HOME/.local}"
BIN_DIR="$PREFIX/bin"
APP_DIR="$PREFIX/share/applications"
MAN_DIR="$PREFIX/share/man/man5"
CFG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/goot"

rm -f "$BIN_DIR/goosy" "$BIN_DIR/goot"
rm -f "$APP_DIR/goosy.desktop" "$APP_DIR/goot.desktop"
rm -f "$MAN_DIR/goosy.ini.5" "$MAN_DIR/goot.ini.5"
rm -f "$PREFIX/share/pixmaps/goosy.png" "$PREFIX/share/pixmaps/goot.png"

echo "[goosy] Removed $BIN_DIR/goosy, $APP_DIR/goosy.desktop, and $MAN_DIR/goosy.ini.5"
echo "[goosy] User configuration in $CFG_DIR was preserved. Remove it manually if desired."
