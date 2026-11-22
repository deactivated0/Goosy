#!/usr/bin/env sh
# Clean rootless uninstaller for Goot
set -eu

PREFIX="${PREFIX:-$HOME/.local}"
BIN_DIR="$PREFIX/bin"
APP_DIR="$PREFIX/share/applications"
MAN_DIR="$PREFIX/share/man/man5"
CFG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/goot"

rm -f "$BIN_DIR/goot"
rm -f "$APP_DIR/goot.desktop"
rm -f "$MAN_DIR/goot.ini.5"

echo "[goot] Removed $BIN_DIR/goot, $APP_DIR/goot.desktop, and $MAN_DIR/goot.ini.5"
echo "[goot] User configuration in $CFG_DIR was preserved. Remove it manually if desired."
