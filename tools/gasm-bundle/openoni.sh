#!/bin/sh
# OpenOni @VERSION@ (openoni.wasm) on the bundled gasm-run @GASM_VERSION@: Linux launcher.
#   ./openoni.sh [options] [DATA] [gasm-run options...]          (./openoni.sh --help)
# The game data location comes from the argument (then saved), else $XDG_CONFIG_HOME/openoni/data-location,
# else a zenity or kdialog chooser (when a display is available), else the usage text. Test hooks (no
# dialogs): OPENONI_GAME=<folder|image> uses that data without saving it (and turns missing data into exit 2),
# OPENONI_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/openoni"
LOCFILE="$CONF/data-location"
LOGDIR="${XDG_STATE_HOME:-$HOME/.local/state}/openoni"
LOG="$LOGDIR/gasm.log"
TITLE="Oni (gasm)"
EXE_SIZE=1527808   # Oni.exe 1.0 (CRC-32 a8223704), when the folder holds an installed game
DRY=${OPENONI_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
OpenOni @VERSION@ on gasm-run @GASM_VERSION@

  $0 [options] [DATA] [gasm-run options...]

DATA is your Oni CD (Windows, v1.0): an .iso image of it (or a raw .bin), or a folder with its
files (ONISETUP.EXE, ONISETUP.002), or an installed game's folder (Oni.exe 1.0, GameDataFolder/).
It is saved in
  $LOCFILE
so later runs need no argument. Without one, a chooser opens (zenity or kdialog).

Options:
  --change-data      ask for the game data even if a location is saved
  --forget-data      delete the saved location and exit
  --install-desktop  add a menu entry (~/.local/share/applications/openoni-gasm.desktop) and exit
  --dry-run          print the gasm-run command instead of running it (also OPENONI_DRY_RUN=1)
  --help             this text
Anything after DATA goes to gasm-run, e.g. --window 1280x960, --fullscreen, --mute.
OPENONI_GAME=<DATA> uses that data for one run without saving it.

More: https://openoni.emdzej.pl/guide/game-data
EOF
}

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_data <path>: exit 0 if usable; otherwise prints why
check_data() {
  if [ -f "$1" ]; then
    case "$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')" in
      *.iso | *.bin) return 0 ;;   # the game itself checks what is inside (exit 1 and its message if it isn't the CD)
      *.cue) printf 'A .cue sheet only names the image: use the .bin file next to it.\n%s\n' "$1" ;;
      *.mdf | *.nrg) printf 'OpenOni reads .iso and raw .bin images only: convert it, or use a folder with the CD files.\n%s\n' "$1" ;;
      *) printf 'This is a file: use the CD image (.iso) or a folder with the CD files.\n%s\n' "$1" ;;
    esac
    return 1
  fi
  if [ ! -d "$1" ]; then
    printf 'The game data was not found at:\n%s\nChoose it again.\n' "$1"
    return 1
  fi
  exe=$(ci_find "$1" Oni.exe f)
  if [ -n "$exe" ]; then
    if [ -z "$(ci_find "$1" GameDataFolder d)" ]; then
      printf 'This folder has Oni.exe but no GameDataFolder:\n%s\n' "$1"; return 1
    fi
    size=$(wc -c <"$exe" | tr -d ' ')
    if [ "$size" != "$EXE_SIZE" ]; then
      printf 'Oni.exe is %s bytes, not 1,527,808: OpenOni needs the Windows version 1.0 (the CD installer has it).\n%s\n' "$size" "$1"
      return 1
    fi
    return 0
  fi
  if [ -z "$(ci_find "$1" ONISETUP.EXE f)" ] || [ -z "$(ci_find "$1" ONISETUP.002 f)" ]; then
    printf 'This folder has neither the CD installer (ONISETUP.EXE, ONISETUP.002) nor Oni.exe:\n%s\n' "$1"; return 1
  fi
  return 0
}

absolute() { # absolute path without a trailing slash
  p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

have_gui() { [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && { command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1; }; }

# ask <message>: prints the chosen folder or image (empty on Quit)
ask() {
  pick="Choose a folder with the CD files (ONISETUP.EXE, ONISETUP.002) or the installed game"
  if command -v zenity >/dev/null 2>&1; then
    choice=$(zenity --question --title "$TITLE" --text "$1" --ok-label "Choose the CD image" \
      --cancel-label Quit --extra-button "Choose a folder" 2>/dev/null)
    rc=$?
    if [ "$rc" = 0 ]; then
      zenity --file-selection --title "Choose the Oni CD image" \
        --file-filter "Disc images | *.iso *.ISO *.bin *.BIN" --file-filter "All files | *" 2>/dev/null
    elif [ "$choice" = "Choose a folder" ]; then
      zenity --file-selection --directory --title "$pick" 2>/dev/null
    fi
  else
    kdialog --title "$TITLE" --yesnocancel "$1" --yes-label "Choose the CD image" \
      --no-label "Choose a folder" --cancel-label Quit 2>/dev/null
    case $? in
      0) kdialog --title "Choose the Oni CD image" --getopenfilename "$HOME" "*.iso *.ISO *.bin *.BIN" 2>/dev/null ;;
      1) kdialog --title "$pick" --getexistingdirectory "$HOME" 2>/dev/null ;;
    esac
  fi
}

error_box() {
  if command -v zenity >/dev/null 2>&1; then zenity --error --title "$TITLE" --text "$1" 2>/dev/null
  elif command -v kdialog >/dev/null 2>&1; then kdialog --title "$TITLE" --error "$1" 2>/dev/null; fi
}

install_desktop() {
  dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  mkdir -p "$dir"
  sed -e "s|@DIR@|$HERE|g" "$HERE/openoni-gasm.desktop" >"$dir/openoni-gasm.desktop"
  echo "installed $dir/openoni-gasm.desktop"
}

INTRO="OpenOni needs your copy of Oni (it is not included): the Windows CD, v1.0.

Choose an image of the CD (.iso), or a folder with the CD files (ONISETUP.EXE, ONISETUP.002) or the installed game (Oni.exe, GameDataFolder).

Your choice is remembered; run openoni.sh --change-data to pick another."

while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-data) CHANGE=1; shift ;;
    --forget-data) rm -f "$LOCFILE"; echo "forgot the game data location ($LOCFILE)"; exit 0 ;;
    --install-desktop) install_desktop; exit 0 ;;
    *) break ;;
  esac
done
DATAP="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then DATAP=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$DATAP" ] && [ -n "${OPENONI_GAME:-}" ]; then DATAP=$(absolute "$OPENONI_GAME"); fi
if [ -z "$DATAP" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then DATAP=$(head -n 1 "$LOCFILE"); fi

MSG=$INTRO
[ -n "$DATAP" ] && { MSG=$(check_data "$DATAP") || true; }
while [ -z "$DATAP" ] || ! check_data "$DATAP" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENONI_GAME:-}" ] || ! have_gui; then
    [ -n "$DATAP" ] && printf '%s\n\n' "$MSG" >&2
    [ "$DRY" = 1 ] || [ -n "${OPENONI_GAME:-}" ] || usage >&2
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

if [ -d "$DATAP" ]; then set -- --asset-dir "$DATAP" "$@"; else set -- --asset "cd=$DATAP" "$@"; fi
set -- "$HERE/gasm-run" "$HERE/openoni.wasm" "$@"
if [ "$DRY" = 1 ]; then
  for a in "$@"; do printf "'%s' " "$(printf '%s' "$a" | sed "s/'/'\\\\''/g")"; done
  echo
  exit 0
fi

if [ -t 1 ] || [ -t 2 ]; then exec "$@"; fi
# Started from a menu or file manager: keep the output in a log and show errors in a dialog.
mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenOni @VERSION@, gasm-run @GASM_VERSION@"; echo "$*"; } >>"$LOG"
"$@" >>"$LOG" 2>&1
rc=$?
[ "$rc" = 0 ] || [ -n "${OPENONI_GAME:-}" ] || error_box "OpenOni stopped with an error:

$(tail -n 4 "$LOG")

$([ -f "$DATAP" ] && printf '\nA disc image must be the Oni CD.\n')
Full log: $LOG"
exit "$rc"
