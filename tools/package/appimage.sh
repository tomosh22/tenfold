#!/usr/bin/env bash
# Builds tenfold-<version>-x86_64.AppImage from a Release build tree (Linux only; run in CI or on a dev machine).
#
#   cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DTENFOLD_BUILD_UI=ON
#   cmake --build build-rel && cmake --build build-rel --target fetch-clangd
#   tools/package/appimage.sh build-rel [out-dir]
#
# Uses linuxdeploy (downloaded to the build dir if not on PATH, AppImage extract-and-run so FUSE is not needed) to
# collect shared libraries into an AppDir laid out as the package is: usr/bin/{tenfold-ui,tenfold-cli,clangd},
# usr/lib/clang/<major>/include. The editor finds clangd next to itself, so the bundled one is used inside the AppImage.
set -euo pipefail

build="${1:?usage: appimage.sh <build-dir> [out-dir]}"
build="$(cd "$build" && pwd)"
out="${2:-$build}"
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
appdir="$build/AppDir"
LINUXDEPLOY_URL="${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20240109-1/linuxdeploy-x86_64.AppImage}"
LINUXDEPLOY_SHA256="${LINUXDEPLOY_SHA256:-}"  # set to pin; CI passes it

[ -x "$build/tenfold-ui" ] || { echo "tenfold-ui not built in $build (configure with -DTENFOLD_BUILD_UI=ON)" >&2; exit 1; }
[ -x "$build/clangd-stage/bin/clangd" ] || { echo "clangd not staged: build the fetch-clangd target first" >&2; exit 1; }

rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" --prefix /usr

version="$(sed -n 's/^#define TENFOLD_VERSION "\(.*\)"/\1/p' "$build/generated/tenfold_version.h")"

cat >"$build/tenfold.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=tenfold
Comment=Fast C++ code editor
Exec=tenfold-ui
Icon=tenfold
Categories=Development;TextEditor;
Terminal=false
EOF
# Placeholder icon (flat 64x64 PNG, no artwork exists yet); replace with real artwork when it does.
python3 - "$build/tenfold.png" <<'EOF'
import struct, sys, zlib
w = h = 64
raw = b"".join(b"\x00" + bytes([0x2b, 0x6c, 0xb0]) * w for _ in range(h))
def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
EOF

ld="$build/linuxdeploy-x86_64.AppImage"
if command -v linuxdeploy >/dev/null 2>&1; then
  ld="$(command -v linuxdeploy)"
elif [ ! -x "$ld" ]; then
  curl -fsSL -o "$ld" "$LINUXDEPLOY_URL"
  if [ -n "$LINUXDEPLOY_SHA256" ]; then echo "$LINUXDEPLOY_SHA256  $ld" | sha256sum -c -; fi
  chmod +x "$ld"
fi

# AppRun: put usr/bin first so the bundled clangd sits next to the editor either way.
cat >"$appdir/AppRun" <<'EOF'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
export PATH="$here/usr/bin:$PATH"
exec "$here/usr/bin/tenfold-ui" "$@"
EOF
chmod +x "$appdir/AppRun"

mkdir -p "$out"
(cd "$out" && VERSION="$version" APPIMAGE_EXTRACT_AND_RUN=1 "$ld" --appdir "$appdir" \
  --executable "$appdir/usr/bin/tenfold-ui" --executable "$appdir/usr/bin/tenfold-cli" \
  --desktop-file "$build/tenfold.desktop" --icon-file "$build/tenfold.png" --custom-apprun "$appdir/AppRun" \
  --output appimage)
ls -l "$out"/*.AppImage
