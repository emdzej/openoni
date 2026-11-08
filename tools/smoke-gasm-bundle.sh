#!/usr/bin/env bash
# Check a gasm bundle from tools/package-gasm.sh without game data (CI runs it on each bundle's own OS):
#   tools/smoke-gasm-bundle.sh <openoni-gasm-...-macos-universal.zip | ...-linux-<arch>.tar.gz>
# Unpacks it, checks the files, runs the bundled gasm-run without data (it must stop with OpenOni's "game
# data not found" message and exit code 1, not crash), and the launcher's --help and --dry-run paths against
# fake data (a CD folder with ONISETUP.EXE and ONISETUP.002, an installed folder with a 1,527,808-byte
# Oni.exe and GameDataFolder/, folders and files that must be refused, and an .iso that goes to the game as
# asset cd; no dialogs: OPENONI_GAME, --dry-run, a scratch HOME).
# With OPENONI_SMOKE_DATA=<the CD's files or an .iso of the CD> it also runs the game through the
# launcher (headless) and checks that its hash line equals a direct gasm-run on the same module and
# parameters (the bundled gasm-run, or SMOKE_GASM_RUN=<another gasm-run>). Several, separated by ':', must
# all give the same hashes: OPENONI_SMOKE_DATA=cd:/path/Oni.iso checks the image against the folder.
set -euo pipefail
ARCHIVE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
case "$ARCHIVE" in
  *.zip) (cd "$T" && unzip -q "$ARCHIVE") ;;
  *.tar.gz) tar xzf "$ARCHIVE" -C "$T" ;;
  *) fail "unknown archive $ARCHIVE" ;;
esac
DIR=$(find "$T" -mindepth 1 -maxdepth 1 -type d -name 'openoni-gasm-*' | head -n 1)
[ -n "$DIR" ] || fail "no openoni-gasm-* folder in the archive"
for f in README.txt LICENSE LICENSE-gasm; do [ -s "$DIR/$f" ] || fail "missing $f"; done
if [ -d "$DIR/Oni (gasm).app" ]; then
  APP="$DIR/Oni (gasm).app"
  RUN="$APP/Contents/MacOS/gasm-run"; WASM="$APP/Contents/Resources/openoni.wasm"; L="$APP/Contents/MacOS/OpenOni"
  codesign --verify --strict "$APP" || fail "code signature"
  [ "$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP/Contents/Info.plist")" = pl.emdzej.openoni.gasm ] ||
    fail "bundle id"
  [ -s "$APP/Contents/Resources/openoni.icns" ] || fail "icon"
  lipo -info "$RUN"
else
  RUN="$DIR/gasm-run"; WASM="$DIR/openoni.wasm"; L="$DIR/openoni.sh"
  [ -s "$DIR/openoni.png" ] || fail "icon"
  [ -s "$DIR/openoni-gasm.desktop" ] || fail "desktop entry"
fi
if [ ! -x "$RUN" ] || [ ! -s "$WASM" ] || [ ! -x "$L" ]; then fail "gasm-run, openoni.wasm or the launcher missing"; fi
grep -q "gasm-run [0-9]" "$DIR/README.txt" || fail "README does not name the gasm version"
grep -q "version 1.0" "$DIR/README.txt" || fail "README does not name the game version"
if grep -qi "freeware" "$DIR/README.txt"; then fail "README calls the game freeware"; fi

# 1. No data: a clean error from the game, not a crash.
set +e
out=$(cd "$T" && "$RUN" "$WASM" --headless 5 --mute 2>&1); rc=$?
set -e
echo "$out"
[ "$rc" = 1 ] || fail "gasm-run without data: exit $rc, expected 1"
echo "$out" | grep -q "game data not found" || fail "no 'game data not found' message"

# 2. Launcher: help, and the command it builds (no dialogs, scratch HOME so nothing real is touched).
REAL_HOME=$HOME
export HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" XDG_STATE_HOME="$T/home/.local/state"
mkdir -p "$HOME"
"$L" --help | grep -q "OpenOni" || fail "--help"
# fakes: the CD's files and an installed game (any letter case, a space in the path), and wrong ones
G="$T/My Oni CD"; mkdir -p "$G"; : >"$G/onisetup.exe"; : >"$G/ONISETUP.002"
I="$T/installed"; mkdir -p "$I/GameDataFolder"
dd if=/dev/zero of="$I/oni.EXE" bs=1527808 count=1 2>/dev/null
mkdir -p "$T/empty" "$T/wrongexe/GameDataFolder" "$T/no002" "$T/nogdf"
printf 'MZ' >"$T/wrongexe/Oni.exe"
: >"$T/no002/ONISETUP.EXE"; dd if=/dev/zero of="$T/nogdf/Oni.exe" bs=1527808 count=1 2>/dev/null
: >"$T/Oni CD.iso"; : >"$T/Oni.cue"; : >"$T/Oni.mdf"
OPENONI_GAME="$I" "$L" --dry-run | grep -q -- "--asset-dir" || fail "dry run with an installed folder"
OPENONI_GAME="$G" "$L" --dry-run --mute | tee "$T/cmd"
{ grep -q -- "--asset-dir" "$T/cmd" && grep -q "My" "$T/cmd" && grep -q -- "--mute" "$T/cmd"; } || fail "dry run with an installed folder"
for bad in empty wrongexe no002 nogdf Oni.cue Oni.mdf missing; do
  set +e
  OPENONI_GAME="$T/$bad" "$L" --dry-run >/dev/null 2>"$T/err"; rc=$?
  set -e
  [ "$rc" = 2 ] || fail "$bad accepted (exit $rc, expected 2)"
  echo "refused $bad: $(tr '\n' ' ' <"$T/err")"
done
grep -q "1,527,808" <(OPENONI_GAME="$T/wrongexe" "$L" --dry-run 2>&1 || true) || fail "no exe size message"
grep -q "\.bin file" <(OPENONI_GAME="$T/Oni.cue" "$L" --dry-run 2>&1 || true) || fail "no .cue message"
# an image goes to the game as asset cd (what is inside is the game's to check)
OPENONI_GAME="$T/Oni CD.iso" "$L" --dry-run | tee "$T/cmd-iso"
{ grep -q -- "--asset" "$T/cmd-iso" && grep -q "cd=" "$T/cmd-iso" && ! grep -q -- "--asset-dir" "$T/cmd-iso"; } || fail "dry run with an .iso"
# the data as an argument (saved, but not by a dry run), then --forget-data
"$L" --dry-run "$G" --mute | grep -q -- "--mute" || fail "dry run with the data as an argument"
if "$L" --dry-run </dev/null >/dev/null 2>&1; then fail "no data, dry run: should exit non-zero"; fi
LOCS=("$XDG_CONFIG_HOME/openoni/data-location" "$HOME/Library/Application Support/OpenOni/data-location")
for f in "${LOCS[@]}"; do [ ! -e "$f" ] || fail "a dry run saved the data location"; done
"$L" --forget-data | grep -q "forgot" || fail "--forget-data"

# 3. Optional: the real game with the user's data, through the launcher, against a direct gasm-run.
if [ -n "${OPENONI_SMOKE_DATA:-}" ]; then
  REF=${SMOKE_GASM_RUN:-$RUN}
  case "$REF" in /*) ;; *) REF="$PWD/$REF" ;; esac
  ARGS=(--headless 400 --mute)   # each run on fresh storage: same start, no real saves touched
  n=0
  first=""
  IFS=: read -r -a SOURCES <<<"$OPENONI_SMOKE_DATA"
  for src in "${SOURCES[@]}"; do
    if [ -d "$src" ]; then DATA=$(cd "$src" && pwd); SRC=(--asset-dir "$DATA")
    else DATA=$(cd "$(dirname "$src")" && pwd)/$(basename "$src"); SRC=(--asset "cd=$DATA"); fi
    rm -f "$HOME/Library/Logs/OpenOni/gasm.log" "$XDG_STATE_HOME/openoni/gasm.log"
    n=$((n + 1)); rm -rf "$T/st$n-l" "$T/st$n-d"
    OPENONI_GAME="$DATA" "$L" "${ARGS[@]}" --storage-dir "$T/st$n-l" >"$T/launch.out" 2>&1 || fail "the launcher's run on $DATA failed: $(tail -n 3 "$T/launch.out")"
    # the launcher logs when there is no terminal (always on macOS)
    cat "$T/launch.out" "$HOME/Library/Logs/OpenOni/gasm.log" "$XDG_STATE_HOME/openoni/gasm.log" 2>/dev/null >"$T/launch.all" || true
    HOME=$REAL_HOME "$REF" "$WASM" "${SRC[@]}" "${ARGS[@]}" --storage-dir "$T/st$n-d" >"$T/direct.out" 2>&1 || fail "the direct run on $DATA failed"
    grep -q "game data from" "$T/launch.all" || fail "the game did not find the data in $DATA"
    a=$(grep -E "^video_fnv32=" "$T/launch.all" | tail -n 1); b=$(grep -E "^video_fnv32=" "$T/direct.out" | tail -n 1)
    echo "$(basename "$DATA"): launcher: $a"; echo "$(basename "$DATA"): direct:   $b"
    grep -q "frames=400 " "$T/launch.all" || fail "the game did not run 400 frames"
    if [ -z "$a" ] || [ "$a" != "$b" ]; then fail "the launcher's hashes differ from gasm-run's ($DATA)"; fi
    if [ -z "$first" ]; then first=$a; elif [ "$a" != "$first" ]; then fail "$DATA hashes differently from ${SOURCES[0]}"; fi
  done
fi
echo "PASS $(basename "$ARCHIVE")"
