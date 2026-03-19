#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build-asan}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build "$BUILD_DIR"

ASAN_OPTIONS="detect_leaks=1:suppressions=$ROOT_DIR/tests/asan_suppressions.txt" \
LSAN_OPTIONS="suppressions=$ROOT_DIR/tests/asan_suppressions.txt" \
ctest --test-dir "$BUILD_DIR" --output-on-failure
