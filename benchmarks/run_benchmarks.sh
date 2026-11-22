#!/usr/bin/env sh
# Goot Reproducible Benchmark Harness (Artix Linux x86_64, native Wayland / labwc)
# Compares Goot against installed Foot, Kitty, and Alacritty when available.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GOOT_BIN="${GOOT_BIN:-$ROOT_DIR/build/goot}"

if [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "ERROR: WAYLAND_DISPLAY is not set. Run inside a Wayland compositor (e.g., labwc)." >&2
    exit 1
fi

measure_terminal() {
    name="$1"
    bin="$2"
    if ! command -v "$bin" >/dev/null 2>&1 && [ ! -x "$bin" ]; then
        printf "%-12s | NOT INSTALLED\n" "$name"
        return
    fi

    # Launch terminal, let it settle for 2 seconds, sample RSS & CPU from /proc
    "$bin" >/dev/null 2>&1 &
    pid=$!
    sleep 2

    if kill -0 "$pid" 2>/dev/null; then
        rss_kb=$(awk '/^Rss:/{print $2}' "/proc/$pid/smaps_rollup" 2>/dev/null || \
                 awk '/^VmRSS:/{print $2}' "/proc/$pid/status" 2>/dev/null || echo 0)
        cpu1=$(awk '{print $14 + $15}' "/proc/$pid/stat")
        sleep 2
        cpu2=$(awk '{print $14 + $15}' "/proc/$pid/stat")
        cpu_ticks=$((cpu2 - cpu1))
        printf "%-12s | Idle RSS: %6s KiB | Idle CPU (2s): %3d ticks\n" "$name" "$rss_kb" "$cpu_ticks"
        kill -TERM "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    else
        printf "%-12s | EXITED EARLY\n" "$name"
    fi
}

echo "=== Goot vs Native Wayland Terminals (Measured on $(uname -sr)) ==="
measure_terminal "goot"      "$GOOT_BIN"
measure_terminal "foot"      "foot"
measure_terminal "kitty"     "kitty"
measure_terminal "alacritty" "alacritty"
