#!/usr/bin/env bash
# Regenerate the committed small corpora tests/corpus/gen-*/ (W9g) from the generator and keep the engine's known wrong
# answers as `xfail` lines:  generate -> run tenfold-cli corpus -> xfail_from_report.py -> run again (must be clean).
#   tools/stress/regen_committed.sh [path/to/tenfold-cli]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CLI=${1:-${TENFOLD_CLI:-$ROOT/build/release/tenfold-cli}}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
gen() { # name profile variant seed
  local dir="$ROOT/tests/corpus/$1"
  python3 "$HERE/gen_codebase.py" "$dir" --profile "$2" --size small --variant "$3" --seed "$4" --impl-position first
  for pass in 1 2 3; do
    "$CLI" corpus "$dir" --json "$TMP/$1.json" --timeout 300 > "$TMP/$1.txt" 2>&1 || true
    tail -n 1 "$TMP/$1.txt"
    python3 "$HERE/xfail_from_report.py" "$dir" "$TMP/$1.json" | grep -q '^0 lines changed' && break || true
  done
}
gen gen-game game cmake 1
gen gen-service service vs 2
gen gen-mixed mixed cmake 3
