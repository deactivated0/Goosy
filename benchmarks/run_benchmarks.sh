#!/usr/bin/env sh
# Goosy Reproducible Benchmark Harness
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 "$ROOT_DIR/benchmarks/comprehensive_benchmarks.py" "$@"
