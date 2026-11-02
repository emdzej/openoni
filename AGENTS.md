# AGENTS.md

Guidance for coding agents working in this repository. Humans: see [README.md](README.md) and
[openoni.emdzej.pl](https://openoni.emdzej.pl).

## What this is

OpenOni is a faithful, from-scratch reimplementation of **Oni** (Bungie West, 2001), the Windows release
v1.0 (`Oni.exe`, 1,527,808 bytes, CRC-32 `a8223704`), in C11. It targets [gasm](https://github.com/emdzej/gasm)
**0.16.0** (`openoni.wasm`: native runners on macOS, Linux and Windows, and the browser). A native build
exists for tools and headless tests only. Sibling projects with the same conventions: `../openrf` (Return
Fire, complete), `../opencmr2` (Colin McRae Rally 2.0, in progress, the closest: a 3D Windows game of 2000
on `gasm:gl`). Copy their proven pieces (file layer, gasm backend, bundles, play page) rather than
reinventing them.

It is not an emulator or a wrapper: every subsystem is ported function by function from the original
executable. **Fidelity is the product.** Reproduce the original's behaviour, including its quirks and bugs.
If something looks wrong, check the disassembly; if the original does it, keep it and add a comment. Only
fix deviations *of the port* from the original.

**State:** step 0 of [docs/re/port-plan.md](docs/re/port-plan.md) (the order of work, each step with its
check). Done: the installer archive (`src/vise.c`, `openoni-extract`, `tools/vise.py`,
[docs/formats/vise.md](docs/formats/vise.md); the standalone Go version is `../visextract`).

## Hard rules

1. **Never commit original data or anything derived from it.** `game/` (the extracted game), `oni.exe`
   and `oni-mac` (the Ghidra copies), `re/` (decompiler dumps), `out/` (decoded assets) are git-ignored:
   keep it that way. No `.iso`, `.dat`, `.raw`, `.bik`, `.bsl`, extracted textures, models, sounds or movies.
2. **No data copied from the executable in the source tree.** Tables the game needs from `Oni.exe` are
   loaded at run time (`src/exe.c`, checks size + CRC). Small algorithmic constants and instruction operands
   are fine; tables, geometry and strings are not.
3. **Cite the original.** Every ported function names its source: `/* ONrGame_Update 0x4xxxxx */`. Keep
   names consistent with the Ghidra project; `tools/ghidra/names.tsv` is the record.
4. **Don't paste decompiler output** into source or docs. Write the port and the docs in your own words;
   describe layouts, formulas and addresses.
5. Community knowledge (OniGalore format notes, OniSplit behaviour) may inform format understanding; the
   exe is the authority and no code is copied.

## Game data

The user's CD image: `~/Downloads/Oni (USA).iso` (ISO 9660, 437,792,768 bytes). The game is only inside the
CD's Installer VISE archive (`ONISETUP.EXE` + `ONISETUP.002`). `openoni-extract` turns the CD (ISO or a
folder with its files) into the installed layout, with no Windows installer and no Wine:

```sh
cmake -S . -B build && cmake --build build
./build/openoni-extract ~/Downloads/"Oni (USA).iso" game     # ./game: Oni.exe, binkw32.dll, GameDataFolder/
```

Tests and tools read the game folder through the file layer (`OPENONI_DATA=<dir>`, default `./game`), the
CD through `OPENONI_CD=<dir|image>`, and skip without data. **All data access goes through `src/vfs.h`**: no
`fopen`/`opendir` in the core.

## Reverse engineering

- Import programs with Ghidra's own `analyzeHeadless` (`/opt/homebrew/Cellar/ghidra/<v>/libexec/support`,
  into `~/Library/Caches/ghidra-cli/projects`, after `ghidra stop --project <p>`), then query with
  ghidra-cli: its own import left `oni-mac` and `vise32ex.dll` with broken listings (`push` decoded as
  two-byte `MOV`s).
- Ghidra project `oni` via ghidra-cli: program `oni.exe` (a copy of `game/Oni.exe`, base 0x400000) and
  `oni-mac`, the Intel Mac build 1.2.1 (from `~/Downloads/Oni 1.2.1.zip`, an i386 Mach-O with about 6,200
  of Bungie's function names). **The Mac build is a name map, not the authority**: it is a later version
  with Feral's changes. Names move to the Windows exe after a check. Bungie's convention: `XXr` public,
  `XXi` internal, `XX` the module (`ON` game, `AI2` AI, `OBJ` objects, `M3` draw engine, `TR` animation,
  `P3` particles, `SS2` sound, `WM` UI, `BF` files, `UU` utilities, `MU` maths, `PH` physics, `SL` scripts,
  `TM` instances).
- `ghidra decompile 0xADDR --project oni --program oni.exe`, plus `x-ref to`, `disasm`, `memory`. In zsh,
  keep the options in an array (`P=(--project oni --program oni.exe)`, `$P`): zsh doesn't word-split.
- `tools/ghidra/apply-names.sh` applies `names.tsv` and regenerates `re/all.c`, `re/meta.tsv`,
  `re/funcs.txt`.
- Ghidra project `onisetup` holds the CD's setup program and `vise32ex.dll` (the installer engine).
- Record findings in the matching `docs/` page with exact layouts and addresses; mark inferred vs confirmed.

## Build and test

```sh
cmake -S . -B build && cmake --build build
OPENONI_CD=~/Downloads/"Oni (USA).iso" OPENONI_VISE_SUMMARY=9b4bdbfa ctest --test-dir build --output-on-failure
```

`tools/vise.py --summary <cd>` and `openoni-extract --summary <cd>` must print the same line
(`files=95 bytes=641025194 hash=9b4bdbfa`), from the ISO and from a folder with its files.

## Unattended runs

Always `--headless` for gasm-run; never open a window, never use macOS `screencapture`.

## Commits

Imperative subject, body explaining why when non-obvious. Never commit `game/`, `cd/`, `oni.exe`,
`oni-mac`, `re/`, `out/`, `build*/`, `.deps/`, `docs/node_modules/`.
