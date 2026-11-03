#!/usr/bin/env bash
# Runs tenfold-fuzz on every target (or the named ones) for a fixed time each and prints the RESULT lines.
#   tests/fuzz/run_all.sh <path to tenfold-fuzz> <seconds per target> [seed] [target...]
# Crashing / hanging inputs are left in fuzz-out/<target>/ (crash-*, hang-*, slow-*); the exit status is non-zero
# when a target failed. Scratch files go to /dev/shm when it exists (the file-reading harnesses are I/O bound).
set -u -o pipefail
fuzz="$1"
seconds="$2"
seed="${3:-1}"
shift 3 2>/dev/null || shift $#
targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then mapfile -t targets < <("$fuzz" --list); fi
[ -d /dev/shm ] && export TMPDIR=/dev/shm
status=0
for t in "${targets[@]}"; do
  if ! "$fuzz" "$t" --seconds "$seconds" --seed "$seed" --slow-ms 50 --out fuzz-out 2>fuzz-out.$t.log | tee -a fuzz-results.txt; then
    echo "FAILED target=$t (see fuzz-out/$t and fuzz-out.$t.log)"
    tail -n 30 "fuzz-out.$t.log"
    status=1
  else
    rm -f "fuzz-out.$t.log"
  fi
done
exit $status
