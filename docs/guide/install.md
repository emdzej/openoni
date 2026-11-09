# Installing

OpenOni is one `openoni.wasm`, run by [gasm](https://gasm.emdzej.pl). It comes in a bundle for each
system: the module, the matching released `gasm-run`, and a small launcher that finds your game data.
No bundle contains any of the original game: you need your own Oni CD (Windows, v1.0), an image of it,
or an installed game ([getting the game files](./game-data)).

| System | Download | Launcher |
|---|---|---|
| macOS (Apple Silicon, Intel) | `openoni-gasm-<version>-macos-universal.zip` | `Oni (gasm).app` |
| Linux x86_64 | `openoni-gasm-<version>-linux-x86_64.tar.gz` | `openoni.sh` |
| Linux arm64 | `openoni-gasm-<version>-linux-arm64.tar.gz` | `openoni.sh` |
| Windows 10/11 x86_64 | `openoni-gasm-<version>-windows-x86_64.zip` | `OpenOni.cmd` |
| Any browser | nothing to download | the [play page](/play/){target="_self"} |
| Your own gasm runner | `openoni-<version>.wasm` | `gasm-run openoni.wasm --asset cd=Oni.iso` (gasm 0.16.1 or newer) |

Released versions are on the [releases page](https://github.com/emdzej/openoni/releases), each file with a
`.sha256` next to it. Every push to `main` also builds all four bundles, each tested on its own system:
they are the artifacts of the [CI run](https://github.com/emdzej/openoni/actions/workflows/ci.yml) (kept
for 14 days; downloading them needs a GitHub login).

Every system and the browser run the same module, so the frames, sound and play are the same on all of them.

## The game data

Each launcher takes the game data once and remembers it:

- the CD's `.iso` image (or a raw `.bin`), which the game reads its files out of as it goes;
- a folder with the CD's files (`ONISETUP.EXE`, `ONISETUP.002`), for example the mounted CD;
- an installed game's folder (`Oni.exe` 1.0, 1,527,808 bytes, and `GameDataFolder/`), which loads levels
  a few seconds faster.

`--change-data` asks again, `--forget-data` deletes the saved location, `--help` lists the options.
Anything after the data goes to gasm-run, for example `--window 1280x960`, `--fullscreen` or `--mute`.

## macOS

1. Unzip `openoni-gasm-<version>-macos-universal.zip` and move `Oni (gasm).app` where you like.
2. It is signed ad hoc, not notarized: the first time, **right-click it → Open** (or
   `xattr -dr com.apple.quarantine "Oni (gasm).app"`).
3. It asks for your game data. Hold **Option** while opening it to pick other data.

From Terminal: `"Oni (gasm).app/Contents/MacOS/OpenOni" --help`.

## Linux

1. `tar xzf openoni-gasm-<version>-linux-x86_64.tar.gz` (or `-linux-arm64`).
2. gasm-run needs ALSA (`libasound2`, `libasound2t64` on newer Debian and Ubuntu) and a Vulkan or OpenGL
   graphics driver.
3. `./openoni.sh "Oni (USA).iso"` the first time (or a folder); without an argument it opens a chooser
   (zenity or kdialog) when one is installed. Later runs need no argument.
4. Optional: `./openoni.sh --install-desktop` adds a menu entry.

## Windows

1. Extract `openoni-gasm-<version>-windows-x86_64.zip` (right-click → **Extract All**).
2. Double-click `OpenOni.cmd`. If SmartScreen warns about an unrecognised app, click **More info → Run
   anyway** (gasm-run is not code-signed).
3. It asks for your game data: the CD drive, a mounted image, a folder, or the `.iso` itself.

## Where things are kept

| | Saved game data location | Log |
|---|---|---|
| macOS | `~/Library/Application Support/OpenOni/data-location` | `~/Library/Logs/OpenOni/gasm.log` |
| Linux | `~/.config/openoni/data-location` | `~/.local/state/openoni/gasm.log` |
| Windows | `%APPDATA%\OpenOni\data-location` | `%LOCALAPPDATA%\OpenOni\gasm.log` |

The saved games and options (`persist.dat`, `key_config.txt`) are in gasm's storage for `openoni`, the
same namespace the browser player uses.
