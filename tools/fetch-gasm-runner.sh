#!/usr/bin/env bash
# Download a released gasm runner (gasm-run) for one platform, for headless runs and CI.
#   tools/fetch-gasm-runner.sh <platform> [dest dir]
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. The version is GASM_VERSION
# from tools/fetch-gasm-sdk.sh (the single pin). Puts gasm-run[.exe], ANGLE (libEGL, libGLESv2 and
# SwiftShader with ANGLE-NOTICES.txt: gasm:gl games need them next to gasm-run) and gasm's LICENSE (MIT; the
# runner archives don't carry it) in <dest dir> (default .deps/gasm-runner-<platform>) and prints the directory.
set -euo pipefail
cd "$(dirname "$0")/.."
PLATFORM=${1:?usage: fetch-gasm-runner.sh <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> [dest dir]}
V=$(tools/fetch-gasm-sdk.sh --version)
DEST=${2:-.deps/gasm-runner-$PLATFORM}
case "$PLATFORM" in
  windows-*) ARCHIVE="gasm-$V-$PLATFORM.zip"; EXE=.exe ;;
  macos-universal|linux-x86_64|linux-arm64) ARCHIVE="gasm-$V-$PLATFORM.tar.gz"; EXE= ;;
  *) echo "unknown platform $PLATFORM" >&2; exit 2 ;;
esac
if [ -x "$DEST/gasm-run$EXE" ] && [ -f "$DEST/LICENSE" ] && [ -f "$DEST/ANGLE-NOTICES.txt" ] && [ "$(cat "$DEST/VERSION" 2>/dev/null)" = "$V" ]; then
  echo "$DEST"; exit 0
fi
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
base=https://github.com/emdzej/gasm/releases/download/$V
echo "fetching $base/$ARCHIVE" >&2
curl -fsSL -o "$TMP/$ARCHIVE" "$base/$ARCHIVE"
case "$ARCHIVE" in
  *.zip) (cd "$TMP" && unzip -q "$ARCHIVE") ;;
  *) tar xzf "$TMP/$ARCHIVE" -C "$TMP" ;;
esac
rm -rf "$DEST"; mkdir -p "$DEST"
cp "$TMP/gasm-$V-$PLATFORM/gasm-run$EXE" "$DEST/"
# ANGLE for gasm:gl (and SwiftShader, its software fallback): every file of it in the archive's top level.
for f in "$TMP/gasm-$V-$PLATFORM"/*EGL* "$TMP/gasm-$V-$PLATFORM"/*GLESv2* "$TMP/gasm-$V-$PLATFORM"/*swiftshader* \
         "$TMP/gasm-$V-$PLATFORM"/ANGLE-NOTICES.txt; do
  if [ -f "$f" ]; then cp "$f" "$DEST/"; fi
done
if [ ! -f "$DEST/ANGLE-NOTICES.txt" ]; then echo "no ANGLE in $ARCHIVE" >&2; exit 1; fi
curl -fsSL -o "$DEST/LICENSE" "https://raw.githubusercontent.com/emdzej/gasm/$V/LICENSE"
echo "$V" > "$DEST/VERSION"
chmod +x "$DEST/gasm-run$EXE"
echo "$DEST"
