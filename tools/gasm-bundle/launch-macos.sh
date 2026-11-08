#!/bin/bash
# Launcher of Oni (gasm).app: OpenOni @VERSION@ (openoni.wasm) on the bundled gasm-run @GASM_VERSION@.
# Installed as Contents/MacOS/OpenOni by tools/package-gasm.sh. macOS ships bash 3.2: no bash 4 features.
#
# Finds the game data (remembered in ~/Library/Application Support/OpenOni/data-location), asks for it
# with a dialog when it is missing, then runs gasm-run --asset-dir <folder> or --asset cd=<image>; its output goes to
# ~/Library/Logs/OpenOni/gasm.log.
# Test hooks (no dialogs): OPENONI_GAME=<folder|image> uses that data without saving it (and turns missing data into
# exit 2), OPENONI_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it. --help: the options.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
RES="$(cd "$HERE/../Resources" && pwd)"
CONF="$HOME/Library/Application Support/OpenOni"
LOCFILE="$CONF/data-location"
LOGDIR="$HOME/Library/Logs/OpenOni"
LOG="$LOGDIR/gasm.log"
TITLE="Oni (gasm)"
EXE_SIZE=1527808   # Oni.exe 1.0 (CRC-32 a8223704), when the folder holds an installed game
DRY=${OPENONI_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
Oni (gasm).app: OpenOni @VERSION@ on gasm-run @GASM_VERSION@

  open "Oni (gasm).app" [--args [options] [DATA] [gasm-run options...]]
  "Oni (gasm).app/Contents/MacOS/OpenOni" [options] [DATA] [gasm-run options...]

DATA is your Oni CD (Windows, v1.0): an .iso image of it (or a raw .bin), or a folder with its
files (ONISETUP.EXE, ONISETUP.002), or an installed game's folder (Oni.exe 1.0, GameDataFolder/).
It is remembered in
  $LOCFILE
Without one the app asks for it. Hold Option while opening the app (or pass --change-data) to pick
another; --forget-data deletes the saved location.

Options:
  --change-data  ask for the game data even if a location is saved
  --forget-data  delete the saved location and exit
  --dry-run      print the gasm-run command instead of running it (also OPENONI_DRY_RUN=1)
  --help         this text
Anything after DATA goes to gasm-run, e.g. --window 1280x960, --fullscreen, --mute.
OPENONI_GAME=<DATA> uses that data for one run without saving it.

Log: $LOG
More: https://openoni.emdzej.pl/guide/game-data
EOF
}

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_data <path>: exit 0 if usable; otherwise prints why
check_data() {
  local p=$1 exe size
  if [ -f "$p" ]; then
    case "$(printf '%s' "$p" | tr '[:upper:]' '[:lower:]')" in
      *.iso | *.bin) return 0 ;;   # the game itself checks what is inside (exit 1 and its message if it isn't the CD)
      *.cue) echo "A .cue sheet only names the image: choose the .bin file next to it."; echo "$p" ;;
      *.mdf | *.nrg) echo "OpenOni reads .iso and raw .bin images only: convert it, or choose a folder with the CD's files."; echo "$p" ;;
      *) echo "This is a file: choose the CD image (.iso) or a folder with the CD's files."; echo "$p" ;;
    esac
    return 1
  fi
  if [ ! -d "$p" ]; then
    echo "The game data was not found at:"
    echo "$p"
    echo "Choose it again."
    return 1
  fi
  exe=$(ci_find "$p" Oni.exe f)
  if [ -n "$exe" ]; then
    if [ -z "$(ci_find "$p" GameDataFolder d)" ]; then
      echo "This folder has Oni.exe but no GameDataFolder:"; echo "$p"; return 1
    fi
    size=$(wc -c <"$exe" | tr -d ' ')
    if [ "$size" != "$EXE_SIZE" ]; then
      echo "Oni.exe is $size bytes, not 1,527,808: OpenOni needs the Windows version 1.0 (the CD's installer has it)."
      echo "$p"; return 1
    fi
    return 0
  fi
  if [ -z "$(ci_find "$p" ONISETUP.EXE f)" ] || [ -z "$(ci_find "$p" ONISETUP.002 f)" ]; then
    echo "This folder has neither the CD's installer (ONISETUP.EXE, ONISETUP.002) nor Oni.exe:"; echo "$p"; return 1
  fi
  return 0
}

# absolute <path>: absolute path without a trailing slash
absolute() {
  local p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

option_held() { # the Option key is down (NSEvent modifier flag 1 << 19)
  [ "$(osascript -l JavaScript -e 'ObjC.import("AppKit"); ($.NSEvent.modifierFlags & 0x80000) ? "1" : "0"' 2>/dev/null)" = 1 ]
}

# ask <message>: a dialog, then a folder or file chooser; prints the chosen path (empty on Quit)
ask() {
  local choice
  choice=$(osascript -e 'on run argv' \
    -e 'button returned of (display dialog (item 1 of argv) with title (item 2 of argv) buttons {"Quit", "Choose a folder…", "Choose the CD image…"} default button 3 cancel button 1 with icon note)' \
    -e 'end run' "$1" "$TITLE" 2>/dev/null) || return 0
  case "$choice" in
    "Choose a folder…")
      osascript -e 'POSIX path of (choose folder with prompt "Choose a folder with the files of the CD (ONISETUP.EXE, ONISETUP.002) or the installed game:")' 2>/dev/null ;;
    "Choose the CD image…")
      osascript -e 'POSIX path of (choose file with prompt "Choose the Oni CD image (.iso, or a raw .bin):")' 2>/dev/null ;;
  esac
}

INTRO="OpenOni needs your copy of Oni (it is not included): the Windows CD, v1.0.

Choose an image of the CD (.iso), or a folder with the CD's files (ONISETUP.EXE, ONISETUP.002) or the installed game (Oni.exe, GameDataFolder).

Your choice is remembered. To change it later, hold Option while opening the app."

# Launcher options, then optional game data, then gasm-run's options.
while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-data) CHANGE=1; shift ;;
    --forget-data) rm -f "$LOCFILE"; echo "forgot the game data location ($LOCFILE)"; exit 0 ;;
    -psn_*) shift ;;   # process serial number from older Finder launches
    *) break ;;
  esac
done
DATAP="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then DATAP=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$DATAP" ] && [ -n "${OPENONI_GAME:-}" ]; then DATAP=$(absolute "$OPENONI_GAME"); fi
if [ -z "$DATAP" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then
  DATAP=$(head -n 1 "$LOCFILE")
  if [ "$DRY" = 0 ] && option_held; then DATAP=""; fi
fi

MSG=$INTRO
[ -n "$DATAP" ] && { MSG=$(check_data "$DATAP") || true; }
while [ -z "$DATAP" ] || ! check_data "$DATAP" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENONI_GAME:-}" ]; then   # never open dialogs in test runs
    echo "no usable game data${DATAP:+: $MSG}" >&2
    exit 2
  fi
  DATAP=$(ask "$MSG")
  [ -n "$DATAP" ] || exit 0
  DATAP=$(absolute "$DATAP")
  SAVE=1
  MSG=$(check_data "$DATAP") || true
done
if [ "$SAVE" = 1 ] && [ "$DRY" = 0 ]; then
  mkdir -p "$CONF" && printf '%s\n' "$DATAP" >"$LOCFILE"
fi

if [ -d "$DATAP" ]; then DATA=(--asset-dir "$DATAP"); else DATA=(--asset "cd=$DATAP"); fi
# ANGLE (with SwiftShader for --gl-software) is in Resources/angle/ (see package-gasm.sh).
CMD=("$HERE/gasm-run" "$RES/openoni.wasm" --gl-lib "$RES/angle" "${DATA[@]}" "$@")
if [ "$DRY" = 1 ]; then printf '%q ' "${CMD[@]}"; echo; exit 0; fi

mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenOni @VERSION@, gasm-run @GASM_VERSION@"; printf '%q ' "${CMD[@]}"; echo; } >>"$LOG"
"${CMD[@]}" >>"$LOG" 2>&1
rc=$?
IMGHINT=""
[ -f "$DATAP" ] && IMGHINT="
A disc image must be the Oni CD: hold Option while opening the app to choose another.
"
if [ "$rc" != 0 ] && [ -z "${OPENONI_GAME:-}" ]; then   # no dialog in test runs
  osascript -e 'on run argv' \
    -e 'display alert "OpenOni stopped with an error" message (item 1 of argv) as critical' -e 'end run' \
    "$(tail -n 4 "$LOG")
${IMGHINT}
Full log: $LOG" >/dev/null 2>&1
fi
exit "$rc"
