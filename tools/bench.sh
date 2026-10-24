#!/usr/bin/env bash
# Benchmark harness wrapper (plan A8).
#   tools/bench.sh                          # examples/sample_project (on a temp copy)
#   tools/bench.sh --large /path/to/godot --file scene/3d/node_3d.cpp:L:C --sym Node3D
# Options other than --large are passed to tools/bench_workspace.py (see its --help).
# Builds build-rel (Release) if tenfold-cli is missing or older than the sources.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
large=""
pass=()
while [ $# -gt 0 ]; do
  case "$1" in
    --large) large="$2"; shift 2 ;;
    *) pass+=("$1"); shift ;;
  esac
done

cli="$root/build-rel/tenfold-cli"
if [ ! -x "$cli" ] || [ -n "$(find "$root/src" "$root/CMakeLists.txt" -newer "$cli" -print -quit)" ]; then
  cmake -S "$root" -B "$root/build-rel" -G Ninja -DCMAKE_BUILD_TYPE=Release >&2
  ninja -C "$root/build-rel" -j"${JOBS:-2}" tenfold-cli >&2
fi

if [ -n "$large" ]; then
  exec python3 "$root/tools/bench_workspace.py" "$large" --cli "$cli" ${pass[@]+"${pass[@]}"}
fi

# sample_project: benchmark a throwaway copy so no .tenfold/.cache lands in the repo.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cp -r "$root/examples/sample_project" "$tmp/sample_project"
python3 "$root/tools/bench_workspace.py" "$tmp/sample_project" --cli "$cli" --wait-index 20000 ${pass[@]+"${pass[@]}"}
