#!/usr/bin/env bash
# Build the macOS app icon (.icns) of Oni (gasm).app: the site's emblem (docs/public/emblem.svg), drawn
# at every iconset size by tools/gasm-bundle/icon.py. Used by tools/package-gasm.sh. Needs macOS iconutil.
#   tools/make-icns.sh <out.icns>
set -euo pipefail
OUT=${1:?usage: make-icns.sh <out.icns>}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
SET="$TMP/openoni.iconset"; mkdir -p "$SET"
for s in 16 32 128 256 512; do
  python3 "$ROOT/tools/gasm-bundle/icon.py" "$s" "$SET/icon_${s}x${s}.png"
  python3 "$ROOT/tools/gasm-bundle/icon.py" $((s * 2)) "$SET/icon_${s}x${s}@2x.png"
done
iconutil -c icns "$SET" -o "$OUT"
