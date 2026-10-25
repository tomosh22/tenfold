#!/usr/bin/env bash
# Smoke test for a built package (.tar.gz or .zip): unpack to a temp dir, run `tenfold-cli --version`, then run a
# clangd-backed query against examples/sample_project with PATH stripped of every directory containing a clangd, and
# require that the engine chose the bundled clangd (it logs "clangd: using <path> (bundled)").
#
# usage: tools/package/smoke-test.sh <package.tar.gz|package.zip> [repo-root]
set -euo pipefail

pkg="${1:?usage: smoke-test.sh <package> [repo-root]}"
repo="${2:-$(cd "$(dirname "$0")/../.." && pwd)}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

case "$pkg" in
  *.tar.gz | *.tgz) tar -xzf "$pkg" -C "$tmp" ;;
  *.zip)
    abs="$(cd "$(dirname "$pkg")" && pwd)/$(basename "$pkg")"
    (cd "$tmp" && cmake -E tar xf "$abs") ;;
  *) echo "unsupported package type: $pkg" >&2; exit 2 ;;
esac

root="$(find "$tmp" -maxdepth 2 -type d -name bin -print -quit)"
root="$(dirname "$root")"
exe=""
for e in "$root/bin/tenfold-cli.exe" "$root/bin/tenfold-cli"; do [ -f "$e" ] && exe="$e" && break; done
[ -n "$exe" ] || { echo "FAIL: tenfold-cli not found in package" >&2; exit 1; }
clangd_file=""
for e in "$root/bin/clangd.exe" "$root/bin/clangd"; do [ -f "$e" ] && clangd_file="$e" && break; done
[ -n "$clangd_file" ] || { echo "FAIL: bundled clangd missing from package" >&2; exit 1; }
ls "$root"/lib/clang/*/include/stddef.h >/dev/null || { echo "FAIL: clang builtin headers missing" >&2; exit 1; }
[ -f "$root/licenses/THIRD_PARTY_NOTICES.md" ] || { echo "FAIL: licence notices missing" >&2; exit 1; }

echo "== version"
ver="$("$exe" --version)"
echo "$ver"
case "$ver" in tenfold\ *\ \(*\)) ;; *) echo "FAIL: unexpected --version output" >&2; exit 1 ;; esac
if [ -f "$root/bin/tenfold-ui" ] || [ -f "$root/bin/tenfold-ui.exe" ]; then
  uiexe="$root/bin/tenfold-ui"; [ -f "$uiexe" ] || uiexe="$uiexe.exe"
  [ "$("$uiexe" --version)" = "$ver" ] || { echo "FAIL: tenfold-ui --version differs" >&2; exit 1; }
fi

# PATH without any directory that contains a clangd.
sep=":"  # bash always uses ":" (also on Windows, where it converts the list for native programs)
clean=""
IFS="$sep" read -r -a parts <<<"$PATH"
for d in "${parts[@]}"; do
  if [ -e "$d/clangd" ] || [ -e "$d/clangd.exe" ]; then continue; fi
  clean="${clean:+$clean$sep}$d"
done

echo "== clangd query (bundled clangd, PATH cleaned of other clangd)"
work="$tmp/sample_project"
cp -R "$repo/examples/sample_project" "$work"
rm -rf "$work/.tenfold"  # never reuse a compile database cached by an earlier run (absolute paths)
log="$tmp/cli.log"
out="$tmp/cli.out"
PATH="$clean" "$exe" "$work" >"$out" 2>"$log" <<'EOF' || true
open src/main.cpp:12:17
wait-file 60000
def
status
quit
EOF
cat "$log" | head -20
cat "$out"
grep -q "clangd: using .* (bundled)" "$log" || { echo "FAIL: engine did not choose the bundled clangd" >&2; exit 1; }
grep -q "clangd: using .*$(basename "$root").*clangd" "$log" || { echo "FAIL: chosen clangd is not inside the package" >&2; exit 1; }
grep -q "geometry.h" "$out" || { echo "FAIL: definition query did not resolve into geometry.h" >&2; exit 1; }
grep -q "authoritative" -i "$out" || { echo "FAIL: no authoritative (clangd) answer" >&2; exit 1; }
echo "PASS"
