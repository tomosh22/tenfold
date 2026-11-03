#!/usr/bin/env bash
# Generate large synthetic projects with known answers into a temp dir, run `tenfold-cli corpus --json` on them,
# measure peak memory of tenfold and clangd, delete the generated data, and write a Markdown summary.   (W9g)
#
#   tools/stress/run_generated.sh SIZE [PROFILE...]
#
#   SIZE      small (~300 files), medium (~3k), large (~20k) or a file count
#   PROFILE   game service lib c mixed (default: all five; large: game service)
#
# Environment:
#   TENFOLD_CLI  the CLI binary         (default: first of build/release, build-rel, build that exists)
#   OUT          report directory       (default: stress-reports/SIZE)
#   VARIANTS     "cmake", "vs" or both  (default: medium "cmake vs", others "cmake")
#   SEED         generator seed         (default 1)
#   TIMEOUT      seconds per project    (default: small 300, medium 3600, large 5400)
#   WORK         parent of the temporary projects (default: $TMPDIR or /tmp); each is deleted after its run unless KEEP=1
#
# Output per run NAME = PROFILE-VARIANT-SIZE:  OUT/NAME.json (runner report), OUT/NAME.meta.json (generator statistics,
# wall time, peak RSS of tenfold and clangd), OUT/NAME.txt (runner log); OUT/summary.md (tools/stress/report_table.py).
# Exit status: 0 when every run finished (expectation FAILs are results, not errors), 2 when a run could not be set up.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SIZE=${1:?usage: run_generated.sh SIZE [PROFILE...]}
shift
case "$SIZE" in
  large) DEF_PROFILES="game service"; DEF_VARIANTS="cmake"; DEF_TIMEOUT=5400 ;;
  medium) DEF_PROFILES="game service lib c mixed"; DEF_VARIANTS="cmake vs"; DEF_TIMEOUT=3600 ;;
  *) DEF_PROFILES="game service lib c mixed"; DEF_VARIANTS="cmake"; DEF_TIMEOUT=300 ;;
esac
PROFILES=${*:-$DEF_PROFILES}
VARIANTS=${VARIANTS:-$DEF_VARIANTS}
TIMEOUT=${TIMEOUT:-$DEF_TIMEOUT}
SEED=${SEED:-1}
OUT=${OUT:-$ROOT/stress-reports/$SIZE}
CLI=${TENFOLD_CLI:-}
if [ -z "$CLI" ]; then
  for c in build/release build-rel build; do
    if [ -x "$ROOT/$c/tenfold-cli" ]; then CLI="$ROOT/$c/tenfold-cli"; break; fi
  done
fi
[ -x "${CLI:-}" ] || { echo "tenfold-cli not found; set TENFOLD_CLI" >&2; exit 2; }
command -v clangd >/dev/null || { echo "clangd not on PATH" >&2; exit 2; }
mkdir -p "$OUT"
rc=0
for profile in $PROFILES; do
  for variant in $VARIANTS; do
    name="$profile-$variant-$SIZE"
    dir=$(mktemp -d "${WORK:-${TMPDIR:-/tmp}}/tenfold-gen-$name.XXXXXX")
    echo "== $name: generating into $dir"
    if ! python3 "$HERE/gen_codebase.py" "$dir/$name" --profile "$profile" --size "$SIZE" --seed "$SEED" --variant "$variant" \
        --stats "$OUT/$name.stats.json"; then
      echo "generator failed for $name" >&2; rc=2; rm -rf "$dir"; continue
    fi
    echo "   disk: $(du -sh "$dir/$name" | cut -f1) in $(find "$dir/$name" -type f | wc -l) files"
    python3 "$HERE/measure_run.py" --out "$OUT/$name.meta.json" --merge "$OUT/$name.stats.json" -- \
      "$CLI" corpus "$dir/$name" --json "$OUT/$name.json" --timeout "$TIMEOUT" > "$OUT/$name.txt" 2>&1
    tail -n 1 "$OUT/$name.txt"
    if [ "${KEEP:-0}" = "1" ]; then echo "   kept $dir"; else rm -rf "$dir"; fi
  done
done
python3 "$HERE/report_table.py" "$OUT" --title "Generated codebases ($SIZE, seed $SEED)" > "$OUT/summary.md" || rc=2
echo "summary: $OUT/summary.md"
exit $rc
