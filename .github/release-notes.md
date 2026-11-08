OpenOni is a function-by-function reimplementation of Oni (Bungie West, 2001), the Windows version 1.0,
running as a WebAssembly module on gasm.

OpenOni needs your own copy of Oni: the Windows CD, as an `.iso` image, a folder with its files
(`ONISETUP.EXE`, `ONISETUP.002`), or an installed game (`Oni.exe` 1.0 and `GameDataFolder`). No game data
is included. See the [status](https://openoni.emdzej.pl/guide/status) for what works.

- **macOS**: unzip and open `Oni (gasm).app` (ad hoc signed, not notarized: the first time, right-click, Open).
- **Linux** (x86_64, arm64): unpack and run `./openoni.sh` (`--install-desktop` adds a menu entry).
- **Windows** (x86_64): unzip and run `OpenOni.cmd`.
- **Any system**: `gasm-run openoni-<version>.wasm --asset cd=Oni.iso` with gasm 0.16.1 or newer.

The launchers ask for the game data once and remember it (`--change-data` picks another). Every file has a
`.sha256` next to it. Guide: https://openoni.emdzej.pl/guide/game-data

Or play in the browser: https://openoni.emdzej.pl/play/
