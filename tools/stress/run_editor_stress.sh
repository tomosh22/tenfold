#!/usr/bin/env bash
# Editor-level stress (W9i): generate a large project with known answers into a temp dir, drive the real editor through
# scripted UI sessions (tests/ui/test_stress_ui.cpp: hundreds of tabs, find / replace in files, 10k carets, folding a
# 120k-line file, a terminal flood while indexing, typing while the index builds, configuration switches and
# open / close while indexing), record keystroke-to-paint p50 / p99 / max and peak RSS (editor and clangd), delete the
# generated data.
#
#   tools/stress/run_editor_stress.sh [PROFILE] [SIZE] [TESTS...]
#
#   PROFILE   game | service | lib | c | mixed   (default game)
#   SIZE      small | medium | large | N files   (default large, ~20k files)
#   TESTS     names of tenfold-ui-tests to run   (default: every stress_ui_* test of the cmake variant, then the two vs
#             ones on a medium vs project of the same profile)
#
# Environment:
#   UI_TESTS   the tenfold-ui-tests binary   (default: first of build-ui, build/release-ui, build that exists)
#   OUT        report directory              (default: stress-reports/editor-PROFILE-SIZE)
#   SEED       generator seed                (default 1)
#   WORK       parent of the temporary projects (default: $TMPDIR or /tmp); deleted afterwards unless KEEP=1
#   TENFOLD_STRESS_TABS / _KEYS / _CYCLES / _SWITCHES / _FLOOD_SECONDS / _CARET_KEYS / _NEEDLE   scenario sizes (tests/ui/test_stress_ui.cpp)
#   Each test runs in its own process, so the peak RSS figures are not polluted by the earlier scenarios.
#   TENFOLD_ENFORCE_PERF=1             make the 8 ms budgets fail the run (they are only reported otherwise)
# Needs xvfb-run (or a display), clangd on PATH, python3 for the generator.
# Output: OUT/NAME.txt (test log), OUT/NAME.jsonl (one JSON line per metric), OUT/summary.md (run_editor_stress.py).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
PROFILE=${1:-game}
SIZE=${2:-large}
shift 2 2>/dev/null || shift $# 2>/dev/null
TESTS=${*:-}
SEED=${SEED:-1}
OUT=${OUT:-$ROOT/stress-reports/editor-$PROFILE-$SIZE}
BIN=${UI_TESTS:-}
if [ -z "$BIN" ]; then
  for c in build-ui build/release-ui build; do
    if [ -x "$ROOT/$c/tenfold-ui-tests" ]; then BIN="$ROOT/$c/tenfold-ui-tests"; break; fi
  done
fi
[ -x "${BIN:-}" ] || { echo "tenfold-ui-tests not found (build with -DTENFOLD_BUILD_UI=ON); set UI_TESTS" >&2; exit 2; }
command -v clangd >/dev/null || { echo "clangd not on PATH" >&2; exit 2; }
RUN=("$BIN")
if [ -z "${DISPLAY:-}" ]; then command -v xvfb-run >/dev/null && RUN=(xvfb-run -a "$BIN"); fi
mkdir -p "$OUT"
WORKDIR=$(mktemp -d "${WORK:-${TMPDIR:-/tmp}}/tenfold-editor-stress.XXXXXX")
cleanup() { if [ "${KEEP:-0}" = "1" ]; then echo "kept $WORKDIR"; else rm -rf "$WORKDIR"; fi; }
trap cleanup EXIT
echo "== generating $PROFILE $SIZE (cmake) and $PROFILE medium (vs) into $WORKDIR"
python3 "$HERE/gen_codebase.py" "$WORKDIR/cmake" --profile "$PROFILE" --size "$SIZE" --seed "$SEED" --variant cmake --jobs 4 >/dev/null || exit 2
python3 "$HERE/gen_codebase.py" "$WORKDIR/vs" --profile "$PROFILE" --size medium --seed "$SEED" --variant vs --jobs 4 >/dev/null || exit 2
export TENFOLD_STRESS_DIR="$WORKDIR/cmake" TENFOLD_STRESS_VS_DIR="$WORKDIR/vs"
export TENFOLD_STRESS_NOCOPY=1  # the generated directories are scratch data already: the tests work in them directly
export TMPDIR="$WORKDIR"
if [ -z "$TESTS" ]; then
  TESTS="stress_ui_tabs stress_ui_find stress_ui_10k stress_ui_fold stress_ui_terminal stress_ui_type stress_ui_open_close_workspace stress_ui_switch stress_ui_open_close_solution"
fi
rc=0
for t in $TESTS; do
  echo "== $t   (load $(cut -d' ' -f1-3 /proc/loadavg 2>/dev/null))"
  TENFOLD_STRESS_JSON="$OUT/$t.jsonl"; export TENFOLD_STRESS_JSON
  rm -f "$TENFOLD_STRESS_JSON"
  "${RUN[@]}" "$t" > "$OUT/$t.txt" 2>&1 || rc=1
  grep -E "STRESS: ---|FAIL|failed|CHECK|PERF" "$OUT/$t.txt" | head -n 40
done
python3 "$HERE/run_editor_stress.py" "$OUT" > "$OUT/summary.md" && cat "$OUT/summary.md"
exit $rc
