#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build-asan}"

rm -rf "$BUILD_DIR"
meson setup "$BUILD_DIR" \
    --buildtype=debug \
    -Db_sanitize=address,undefined

ninja -C "$BUILD_DIR"

ASAN_OPTIONS="detect_leaks=1:suppressions=$ROOT_DIR/tests/asan_suppressions.txt" \
LSAN_OPTIONS="suppressions=$ROOT_DIR/tests/asan_suppressions.txt" \
meson test -C "$BUILD_DIR" --print-errorlogs
