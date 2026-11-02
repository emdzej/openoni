# Port plan

OpenOni is a from-scratch reimplementation of **Oni** (Bungie West / Gathering of Developers, 2001), the
Windows release v1.0 (`Oni (USA).iso`, volume `001228_1801`, 437,792,768 bytes), in C11. It targets
[gasm](../../../gasm) **0.16.0** as `openoni.wasm`. The native runners and bundles cover macOS, Linux
and Windows, and the browser player covers the web. The native CMake build is for tools and headless
tests only. The conventions are those of `../openrf` (complete) and `../opencmr2` (in progress, the
closest analogue: a 3D Windows game of 2000 on `gasm:gl`). Copy their proven pieces rather than
reinventing them.

Each step below ends with a check: a native test against the user's data, or a headless `gasm-run`
hash or screenshot.

## Ground rules (unchanged from the siblings)

- Port function by function from `Oni.exe` and cite the address (`/* ONrGame_Update 0x4xxxxx */`).
  Keep the original's quirks and bugs. Only fix deviations *of the port*.
- Load tables, strings and geometry from the exe and the data at run time (`src/exe.c`, checked by size
  and CRC-32). Never copy them into the source tree.
- Never commit original data or anything derived from it (`cd/`, `game/`, `oni.exe`, `re/`, `out/` are
  git-ignored). Never paste decompiler output.
- Community knowledge (OniGalore wiki format notes, OniSplit behaviour, Daodan findings) may inform
  format understanding. The exe is the authority, and no code is copied.

## What the disc gives us

| File | What |
|---|---|
| `ONISETUP.EXE` (515,728 B) | MindVision **Installer VISE** stub (`ViseMain`, `vise32ex.dll`) |
| `ONISETUP.002` (348,137,970 B) | VISE archive (magic `ESIV`). Data is compressed and has no plain-text file table. Holds `Oni.exe`, `GameDataFolder/` (`level*_Final.dat/.raw`, `IGMD/` BSL scripts), movies, `binkw32.dll` |
| `GLSETUP_117.EXE`, `DIRECTX/` | driver installers (ignored) |

So, as with CMR2's InstallShield cabinet, **the game exists only inside the installer**. Decision: the
game reads an **installed-layout folder** (`Oni.exe` + `GameDataFolder/`). A small tool
creates that folder from the ISO (or from the extracted ISO contents), so no Windows installer has to
run: `openoni-extract` here, and visextract as a standalone tool. An existing Oni install folder works too. Reading the ISO directly from the game is
a later nice-to-have, not a requirement.

### Name source: the Intel Mac build

`~/Downloads/Oni 1.2.1.zip` holds Feral's Intel Mac build 1.2.1 (2015, from The Omni Group's port), an
i386 Mach-O (3,308,148 B) with **6,244 named functions in Bungie's own naming** (`AI2rAlarm_Update`,
`ONrCharacter_*`, `TRrAnimation_*`, `M3rDraw_*`, `WMrWindow_*`, `BFrFile_*`, `P3*` particles, `SS2*`
sound). It has no game data, only the app and two `.mov` movies. It is **a name map, not the authority**:
it is a later version with Feral's changes (new cheats, `-wsafety`, `-parkour`, an incompatible
`persist.dat`, Cocoa/OpenGL platform code). The Windows v1.0 `Oni.exe` stays the port's authority.
Names move across by matching the two programs in Ghidra (Version Tracking, string and call-graph
matches, the prefixes as module boundaries). Each name is checked before it lands in `names.tsv`.
Prefix convention: `XXr` public, `XXi` internal, `XX` = module (`ON` game, `AI2` AI, `OBJ` objects,
`M3` draw engine, `TR` animation, `P3` particles, `SS2` sound, `WM` UI, `BF` files, `UU` utilities, `MU`
maths, `PH` physics, `SL` script/BSL, `IM` images, `TM` instances).

## Original architecture

Confirmed in Ghidra and written up in [architecture.md](architecture.md):

- **Renderer:** OpenGL loaded at run time, under Bungie's Motoko draw engine (`M3` API, `MS` geometry
  engine, a `gl_*` back end), with dead 3dfx paths (FXT1, Glide gamma).
- **Data:** per-level instance files `level<N>_Final.dat` + `.raw` (no `.sep` on Windows), managed by
  `TM`; `level0_Final` holds the shared data and the menus. BSL scripts as text in `IGMD/`.
- **Loops:** the out-of-game UI (`OWrOniWindow_Startup`) and the game (`ONiRunGame`) each own a
  blocking loop. Game time advances by the elapsed ticks (`iComputeDeltaTicks`).
- **Platform:** DirectSound + ACM codecs with a mixer thread, DirectInput with an event-queue thread,
  Bink, `QueryPerformanceCounter`.
- **Size:** 3,655 functions, about 1,065 KB of code ([inventory.md](inventory.md)). The biggest modules
  are the game (`ON`, 201 KB), AI (`AI2`, 169 KB), particles (`P3`, 75 KB), objects (`OBJ`, 69 KB),
  the geometry engine (`MS`, 48 KB) and the UI (`WM`, 47 KB).

## Platform mapping (gasm 0.16.0)

| Original | Port |
|---|---|
| `ONiMain` with blocking loops (front end, game, dialogs) | Recommended: keep them on gasm's own-main-loop support (`gasm_main` + `gasm_wait_frame`; stack switching, Asyncify fallback). The alternative is states as in OpenRF |
| OpenGL 1.1 | GL 1.x fixed-function emulation on GLES 3 (`gasm:gl`, WebGL 2 rules), the only GL user. Start from OpenCMR2's `gl_dev.c` (shader key, lighting, fog, two-stage combiner) |
| DirectInput / Win32 mouse | raw keyboard (`KEYS_RAW`) as the original's key codes, `pointer` with `GASM_INPUT_POINTER_LOCKED` for mouselook (`dx, dy`), pads mapped on top |
| DirectSound | one mixer, `audio_render` pushed each frame |
| `timeGetTime` | frame clock (deterministic headless runs) |
| files | `src/vfs.h` only: ISO 9660 / host dir + VISE layer, positional reads via `asset_read_at` |
| `persist.dat`, `key_config.txt` | `gasm:storage` (defaults written on first run, like the original) |
| command line | launch params (`--param level=, viewer=, demo=, ...`) |

Fidelity note: x87 extended precision can't be reproduced bit-exactly in wasm, so the goal is the
original's *behaviour*. Every gasm runner still has to produce identical hashes.

## 0. Research and repository (done)

1. **VISE format and the extractor (done).** The archive is plain deflate in byte-swapped 16-bit words,
   with an item list in the setup program's overlay ([format](../formats/vise.md)). `src/vise.c` reads it
   through the file layer: `openoni-extract` (native CLI) makes the game folder from the ISO or the CD's
   files in about 10 s, with no Wine, and every file is CRC-checked. `tools/vise.py` is the stdlib
   reference. The same reader is the standalone Go tool [visextract](https://github.com/emdzej/visextract)
   (MIT, release binaries for Linux, macOS and Windows), the extractor to point players to. `src/vise.c`
   stays for the browser player's import and, later, for reading the ISO directly.
2. **Ghidra project `oni`** (done): `oni.exe` (3,655 functions with `FillGaps.java`) and `oni-mac` (6,584
   functions, Bungie's names), both imported with `analyzeHeadless`. `tools/ghidra/match_mac.py` matches
   the two (strings, call graph, layout, neighbourhood; `--validate` measures precision). 1,055 names
   are in `tools/ghidra/names.tsv` (string and neighbourhood >= 0.75 matches, about 96% right), covering
   45% of the code's bytes. About 600 more candidates are in `re/mac-matches.tsv`, to be read.
3. **`docs/re/inventory.md` and `architecture.md`** (done): code size per module, the platform layer,
   the start-up and the loops.
4. **Repo** (done): `AGENTS.md`, `README.md`, `.gitignore`, `LICENSE`; the VitePress site (guide, status,
   the VISE format; `docs/re/` is not published) with `pages.yml`; `ci.yml` (native build and tests on
   macOS and Linux, the site).

**Check (passes):** `tools/vise.py --summary`, `openoni-extract --summary` and `visextract summary` print
`files=95 bytes=641025194 hash=9b4bdbfa`, from the ISO and from a folder of its files; `vise_test`
extracts all 95 files; the visextract and openoni-extract outputs are identical (`diff -r`).

## 1. Skeleton, file layer, exe loader

CMake with two builds (native tests, `-DOPENONI_PLATFORM=gasm`), `platform.h`, `app.c`,
`platform_gasm.c` (title "Oni", 60 Hz, raw keyboard, locked pointer, pads, storage, audio push),
`tools/fetch-gasm-sdk.sh` / `fetch-gasm-runner.sh` (pin 0.16.0), `vfs.c` (host folder via
`--asset-dir`, case-insensitive, `/` or `\\`), `exe.c` (maps `Oni.exe`, checks size + CRC).

**Check:** a test screen runs headless from `--asset-dir game` with a stable hash. The Node runner gives
the same hash.

## 2. Instance files

Port the original's loader for `.dat/.raw`: header, template table, instance descriptors, name
directory, cross-instance links, raw/sep offsets (resolved through accessors, since the native test
build is 64-bit). Write `tools/onidat.py` as the reference dumper.

**Check:** `dat_test` loads every instance of every level. The C and Python summary hashes match.

## 3. Format decoders and viewers

TXMP (16/32-bit formats, mipmaps, animated textures), M3GM geometry, AKEV environment, TRAM
animation, ONCC character assembly, fonts, SUBT/text, SNDD sounds (ADPCM). Add exports to PNG/OBJ/WAV in
`out/`.

**Check:** every texture/model/sound decodes and the C hashes equal Python. A level and Konoko exported
to OBJ look right.

## 4. Renderer (OpenGL 1.x on GLES 3)

Port Motoko/M3 draw contexts onto the GL emulation: environment (sorted opaque/alpha quads,
lightmaps, environment maps), sky, fog, characters (part hierarchy), 2D path for UI and HUD.

**Check:** `--param viewer=level&level=1` (fly-through) and `viewer=char&char=konoko` screenshots and
hashes. Reference captures come from the original under Wine, kept outside the repo.

## 5. Front end

WM UI: main menu, new/load game, options (keys from `key_config.txt` semantics), help, pause, data
console, credits. `persist.dat` goes to `gasm:storage`. Movies are skipped here (step 10).

**Check:** scripted `--input` from boot to the training level start, hashed.

## 6. Character, animation, movement

The character update loop, TRAM state machine and transitions, combos and throws, collision vs AKEV,
gravity/falls, camera (third person, mouselook), physics objects.

**Check:** `demo=moves` scripted input (run, jump, combos, throw) with stable hashes, matched by eye
against Wine captures.

## 7. BSL scripting and level logic

BSL lexer/interpreter with the original's built-in functions and variables. Triggers, trigger volumes,
doors, consoles, turrets, cutscene cameras (OBAN), objectives, and saves at checkpoints.

**Check:** the training level is playable start to finish (scripted demo, hashed).

## 8. Combat systems

Weapons (fire, reload, ammo, dropping), particle system (BINA particle classes, a large subsystem),
damage and hit reactions, power-ups, inventory, HUD.

## 9. AI

Combat and melee behaviour profiles, alarm network, pathfinding on the BNV graph, patrols, neutral
characters. **Check:** every level (1-14) is completable through scripted demos. Spot-check by hand.

## 10. Audio and movies

Oni Sound System (ambient, impulse, groups, music, dialogue, 3D positional mix). Bink: reuse
OpenCMR2's `bink.c` if Oni's `.bik` revision matches, otherwise extend it.

## 11. Release

Saves and options parity, the original's cheats, a browser player (`docs/public/play/` from OpenCMR2:
ISO pick, extract into OPFS with the same VISE code), gasm bundles for macOS, Linux and **Windows** (pointing to visextract for the CD), site at openoni.emdzej.pl, `release.yml`.
Optional widescreen and high-res stay non-default params.

## Risks

- **VISE extraction** (step 0) blocks the user-facing path. It doesn't block development: install once
  under Wine into `./game` and start steps 1-3 in parallel.
- **Mac-to-Windows name transfer** is GCC vs MSVC code. Expect good matches on functions with strings or
  unique calls and weaker ones on small leaf functions. Unmatched functions get names by hand.
- **Size:** Oni is a much bigger program than Return Fire, comparable to or bigger than CMR2.
  Particles, AI and BSL are each a sizeable port. Order the work so every step is visible and checkable.
- **Browser memory:** level files plus textures need watching against the 1 GiB guest limit. Load per
  level, as the original does.
