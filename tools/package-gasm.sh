#!/usr/bin/env bash
# Package openoni.wasm with a released gasm-run into a double-clickable bundle (no game data inside).
#   tools/package-gasm.sh <version> <platform> <gasm-run dir> <out dir>
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. <gasm-run dir> holds gasm-run[.exe]
# and gasm's LICENSE (tools/fetch-gasm-runner.sh <platform> makes one). OPENONI_WASM=<file> picks the module
# (default build-gasm/openoni.wasm); it is always bundled as openoni.wasm, which is also gasm's storage
# namespace for the saves (the same as the browser player's, "openoni"). Produces in <out dir>:
#   macos-universal  Oni (gasm).app, openoni-gasm-<version>-macos-universal.zip
#   linux-<arch>     openoni-gasm-<version>-linux-<arch>.tar.gz
#   windows-x86_64   openoni-gasm-<version>-windows-x86_64.zip
# each archive with a .sha256 next to it. The macOS app is signed ad hoc when codesign is available.
# The launchers are in tools/gasm-bundle/, the icon is drawn by tools/gasm-bundle/icon.py (tools/make-icns.sh on macOS).
# No AOT .cwasm: gasm-run --compile only targets the host it runs on, so it can't be made for the other
# platforms (or both halves of the universal app) at packaging time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# ANGLE (libEGL, libGLESv2, SwiftShader + its ICD file) for gasm:gl, from the runner dir
# (tools/fetch-gasm-runner.sh), with its notices. gasm-run finds it next to itself (Linux, Windows); the .app keeps it in Resources/angle/ (--gl-lib).
angle() {
  local dest=$1 f n=0
  for f in "$RUNDIR"/*EGL* "$RUNDIR"/*GLESv2* "$RUNDIR"/*swiftshader* "$RUNDIR"/ANGLE-NOTICES.txt; do
    if [ -f "$f" ]; then cp "$f" "$dest/"; n=$((n + 1)); fi
  done
  if [ "$n" -lt 3 ]; then echo "no ANGLE in $RUNDIR (tools/fetch-gasm-runner.sh fetches it)" >&2; exit 1; fi
}
usage="usage: package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <gasm-run dir> <out dir>"
VERSION=${1:?$usage}; PLATFORM=${2:?$usage}; RUNDIR=${3:?$usage}; OUT=${4:?$usage}
WASM=${OPENONI_WASM:-$ROOT/build-gasm/openoni.wasm}
GASM_VERSION=$(cat "$RUNDIR/VERSION" 2>/dev/null || "$ROOT/tools/fetch-gasm-sdk.sh" --version)
SRC="$ROOT/tools/gasm-bundle"
EXE=""; [[ "$PLATFORM" == windows-* ]] && EXE=.exe
case "$PLATFORM" in macos-universal | linux-x86_64 | linux-arm64 | windows-x86_64) ;; *) echo "$usage" >&2; exit 2 ;; esac
for f in "$WASM" "$RUNDIR/gasm-run$EXE" "$RUNDIR/LICENSE" "$ROOT/LICENSE"; do
  [ -f "$f" ] || { echo "missing $f (tools/fetch-gasm-runner.sh $PLATFORM fetches the runner and its LICENSE)" >&2; exit 1; }
done
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
NAME="openoni-gasm-$VERSION-$PLATFORM"

# fill <template> <out>: substitute the versions
fill() { sed -e "s|@VERSION@|$VERSION|g" -e "s|@GASM_VERSION@|$GASM_VERSION|g" "$1" >"$2"; }
crlf() { sed -e 's/\r*$/\r/' "$1" >"$1.tmp" && mv "$1.tmp" "$1"; }
sha() { (cd "$OUT" && if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi >"$1.sha256"); }

# readme <out file>: README.txt for this platform (~, $USER, %APPDATA% and backslashes are meant literally)
# shellcheck disable=SC2088,SC2016,SC1003
readme() {
  local start data_how change saves logs lic=""
  [ -n "$EXE" ] && lic=.txt
  case "$PLATFORM" in
    macos-*)
      start='Open "Oni (gasm).app". It is signed ad hoc, not notarized: the first time,
right-click it and choose Open (on macOS 15 and later: open it once, then System Settings, Privacy &
Security, Open Anyway), or run: xattr -dr com.apple.quarantine "Oni (gasm).app"'
      data_how='The first time, the app asks for your game data: choose the image of your Oni CD (.iso),
or a folder with the CD files (ONISETUP.EXE, ONISETUP.002), or an installed game (Oni.exe, GameDataFolder).'
      change='Hold Option while opening the app to choose other game data, or delete
  ~/Library/Application Support/OpenOni/data-location
From Terminal: "Oni (gasm).app/Contents/MacOS/OpenOni" --help'
      saves='~/Library/Application Support/gasm/openoni/'
      logs='~/Library/Logs/OpenOni/gasm.log' ;;
    linux-*)
      start='Run ./openoni.sh (from a terminal or your file manager). ./openoni.sh --install-desktop adds
a menu entry. gasm-run needs ALSA (libasound2, package libasound2t64 on newer Debian and Ubuntu) and a
Vulkan capable graphics driver.'
      data_how='Give it your game data once: ./openoni.sh /path/to/Oni.iso (an image of the CD), or a
folder with the CD files (ONISETUP.EXE, ONISETUP.002) or an installed game (Oni.exe, GameDataFolder).
Without an argument it opens a chooser
(zenity or kdialog) if one is installed.'
      change='./openoni.sh --change-data, or give it another folder as the argument, or delete
  ${XDG_CONFIG_HOME:-~/.config}/openoni/data-location
./openoni.sh --help lists the options; anything after the data goes to gasm-run.'
      saves='~/.local/share/gasm/openoni/'
      logs='the terminal, or ~/.local/state/openoni/gasm.log when started from a menu' ;;
    windows-*)
      start='Double-click OpenOni.cmd. (If Windows SmartScreen warns about gasm-run.exe or the script:
More info, Run anyway.)'
      data_how='The first time, it asks for your game data: the image of the CD (.iso), or a folder with
the CD files (ONISETUP.EXE, ONISETUP.002): the CD drive itself works, or an installed game (Oni.exe,
GameDataFolder). From a command prompt:
OpenOni.cmd D:\'
      change='OpenOni.cmd --change-data, or delete %APPDATA%\OpenOni\data-location.
OpenOni.cmd --help lists the options; anything after the data goes to gasm-run.'
      saves='%APPDATA%\gasm\openoni\'
      logs='the console window, and %LOCALAPPDATA%\OpenOni\gasm.log' ;;
  esac
  cat >"$1" <<TXT
OpenOni $VERSION for gasm ($PLATFORM)
https://openoni.emdzej.pl

OpenOni is a faithful, function-by-function reimplementation of Oni (Bungie West, 2001), ported
from the Windows game (version 1.0). This bundle runs it as a WebAssembly module
(openoni.wasm) in the gasm runtime: gasm-run $GASM_VERSION is included (https://gasm.emdzej.pl). It is
the same game as the browser player.

See https://openoni.emdzej.pl/guide/status for what works.

You need your own copy of Oni: the Windows CD (an image of it, or its files; version 1.0
is on the CD). None of the original game's files are included.

START
$start

YOUR GAME DATA
$data_how
The choice is remembered (the folder must still be there next time).

CHANGE THE GAME DATA
$change

LAUNCH OPTIONS
Anything after the data goes to gasm-run: --window 1280x960, --fullscreen, --mute.
Full guide: https://openoni.emdzej.pl/guide/

SAVES (profiles, options)
$saves

LOG
$logs

LICENSES
OpenOni is GPL-3.0 (LICENSE$lic). gasm-run $GASM_VERSION is MIT (LICENSE-gasm$lic),
https://github.com/emdzej/gasm. ANGLE and SwiftShader (the OpenGL ES libraries) are under their own
licenses (ANGLE-NOTICES.txt). Oni is (c) 2001 Bungie and Take-Two Interactive. OpenOni contains no
original code or assets and is not affiliated with Bungie or Take-Two.
TXT
}

case "$PLATFORM" in
macos-*)
  APP="$OUT/Oni (gasm).app"
  rm -rf "$APP"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
  cp "$RUNDIR/gasm-run" "$APP/Contents/MacOS/gasm-run"
  # ANGLE stays together in Resources/angle/ (the launcher passes --gl-lib): gasm-run reads SwiftShader's
  # ICD file from the same folder, and Frameworks/ may hold only code (codesign --strict).
  mkdir -p "$APP/Contents/Resources/angle"
  angle "$APP/Contents/Resources/angle"
  fill "$SRC/launch-macos.sh" "$APP/Contents/MacOS/OpenOni"
  chmod +x "$APP/Contents/MacOS/OpenOni" "$APP/Contents/MacOS/gasm-run"
  cp "$WASM" "$APP/Contents/Resources/openoni.wasm"
  cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
  cp "$RUNDIR/LICENSE" "$APP/Contents/Resources/LICENSE-gasm"
  "$ROOT/tools/make-icns.sh" "$APP/Contents/Resources/openoni.icns"
  cat >"$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Oni (gasm)</string>
  <key>CFBundleDisplayName</key><string>Oni (gasm)</string>
  <key>CFBundleIdentifier</key><string>pl.emdzej.openoni.gasm</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleGetInfoString</key><string>OpenOni $VERSION on gasm-run $GASM_VERSION</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>OpenOni</string>
  <key>CFBundleIconFile</key><string>openoni</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>OpenOni contributors, GPL-3.0; gasm-run MIT. Oni is (c) 2001 Bungie and Take-Two Interactive.</string>
</dict></plist>
PLIST
  if command -v codesign >/dev/null; then
    for f in "$APP/Contents/Resources/angle"/*.dylib; do codesign --force --sign - "$f"; done
    codesign --force --sign - "$APP/Contents/MacOS/gasm-run"
    codesign --force --sign - "$APP"   # ad hoc (not notarized)
    codesign --verify --strict "$APP"
  fi
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  mkdir "$STAGE/$NAME"
  cp -R "$APP" "$STAGE/$NAME/"
  readme "$STAGE/$NAME/README.txt"
  cp "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"
  cp "$RUNDIR/LICENSE" "$STAGE/$NAME/LICENSE-gasm"
  rm -f "$OUT/$NAME.zip"
  if command -v ditto >/dev/null; then (cd "$STAGE" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$NAME" "$OUT/$NAME.zip")
  else (cd "$STAGE" && zip -qry "$OUT/$NAME.zip" "$NAME"); fi
  sha "$NAME.zip"
  echo "$APP"; echo "$OUT/$NAME.zip" ;;
linux-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run" "$D/"
  angle "$D"
  cp "$WASM" "$D/openoni.wasm"
  fill "$SRC/openoni.sh" "$D/openoni.sh"
  cp "$SRC/openoni-gasm.desktop" "$D/"
  python3 "$ROOT/tools/gasm-bundle/icon.py" 256 "$D/openoni.png"
  chmod 755 "$D/gasm-run" "$D/openoni.sh"; chmod 644 "$D/openoni.wasm" "$D/openoni-gasm.desktop" "$D/openoni.png"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm"
  if tar --version 2>/dev/null | grep -q GNU; then own=(--owner=0 --group=0 --numeric-owner); else own=(--uid 0 --gid 0 --no-xattrs --no-mac-metadata); fi
  COPYFILE_DISABLE=1 tar "${own[@]}" -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"
  sha "$NAME.tar.gz"
  echo "$OUT/$NAME.tar.gz" ;;
windows-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run.exe" "$D/"
  angle "$D"
  cp "$WASM" "$D/openoni.wasm"
  fill "$SRC/OpenOni.cmd" "$D/OpenOni.cmd"; fill "$SRC/openoni.ps1" "$D/openoni.ps1"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE.txt"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm.txt"
  for f in OpenOni.cmd openoni.ps1 README.txt LICENSE.txt LICENSE-gasm.txt; do crlf "$D/$f"; done
  rm -f "$OUT/$NAME.zip"
  (cd "$STAGE" && zip -qr "$OUT/$NAME.zip" "$NAME")
  sha "$NAME.zip"
  echo "$OUT/$NAME.zip" ;;
esac
