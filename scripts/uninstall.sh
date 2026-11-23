#!/usr/bin/env sh
# Clean rootless uninstaller for Goosy
set -eu

PREFIX="${PREFIX:-$HOME/.local}"
BIN_DIR="$PREFIX/bin"
APP_DIR="$PREFIX/share/applications"
MAN_DIR="$PREFIX/share/man/man5"
CFG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/goosy"

rm -f "$BIN_DIR/goosy" "$BIN_DIR/goot"
rm -f "$APP_DIR/goosy.desktop" "$APP_DIR/goosy.desktop"
rm -f "$MAN_DIR/goosy.ini.5" "$MAN_DIR/goosy.ini.5"
rm -f "$PREFIX/share/pixmaps/goosy.png" "$PREFIX/share/pixmaps/goosy.png"

echo "[goosy] Removed $BIN_DIR/goosy, $APP_DIR/goosy.desktop, and $MAN_DIR/goosy.ini.5"
echo "[goosy] User configuration in $CFG_DIR was preserved. Remove it manually if desired."
