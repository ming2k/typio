#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 OUTPUT_LOG [typio args...]" >&2
    exit 1
fi

OUTPUT_LOG="$1"
shift

mkdir -p "$(dirname "$OUTPUT_LOG")"

{
    echo "# typio trace started $(date -Is)"
    echo "# command: typio --verbose $*"
    typio --verbose "$@"
} 2>&1 | tee "$OUTPUT_LOG"
