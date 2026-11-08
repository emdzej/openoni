# AGENTS.md

Guidance for coding agents working in this repository. Humans: see [README.md](README.md) and
[openoni.emdzej.pl](https://openoni.emdzej.pl).

## What this is

OpenOni is a faithful, from-scratch reimplementation of **Oni** (Bungie West, 2001), the Windows release
v1.0 (`Oni.exe`, 1,527,808 bytes, CRC-32 `a8223704`), in C11. It targets [gasm](https://github.com/emdzej/gasm)
**0.16.1** (`openoni.wasm`: native runners on macOS, Linux and Windows, and the browser). A native build
exists for tools and headless tests only. Sibling projects with the same conventions: `../openrf` (Return
Fire, complete), `../opencmr2` (Colin McRae Rally 2.0, in progress, the closest: a 3D Windows game of 2000
on `gasm:gl`). Copy their proven pieces (file layer, gasm backend, bundles, play page) rather than
reinventing them.

It is not an emulator or a wrapper: every subsystem is ported function by function from the original
executable. **Fidelity is the product.** Reproduce the original's behaviour, including its quirks and bugs.
If something looks wrong, check the disassembly; if the original does it, keep it and add a comment. Only
fix deviations *of the port* from the original.

**State:** steps 0 to 2 of [docs/re/port-plan.md](docs/re/port-plan.md) (the order of work, each step
with its check) are done:

- Step 0, the installer archive: `src/vise.c`, `openoni-extract`, `tools/vise.py`,
  [docs/formats/vise.md](docs/formats/vise.md). The standalone Go version is `../visextract`.
- Step 1, the gasm skeleton: `platform.h`, `platform_gasm.c`, `app.c`, `exe.c`.
- Step 2, the instance files: `src/tm.c`, `tools/onidat.py`, [docs/formats/dat.md](docs/formats/dat.md).

Step 3, the decoders, is done. Each has a reference reader in `tools/` and a test with the same hash:

- textures: `txmp.c`, `im.c`
- geometry: `m3gm.c`
- the environment: `akev.c`
- animations: `tram.c`
- bodies: `body.c`
- level objects: `objc.c`; object animations: `oban.c`
- sounds: `sndd.c`; fonts and text: `font.c`, `text.c`

Steps 4 (renderer), 6 (character) and 7 (scripts) have begun:

- `src/render/` (gasm only): `gl_dev.c`, `env_draw.c`, `char_draw.c`, `geom_draw.c` (the geometry's lights
  and reflection maps, [docs/re/lighting.md](docs/re/lighting.md)), `sky_draw.c` (the skybox).
  `src/akev_vis.c`: the visibility rays through the octree, which the draw keeps to (the environment's
  quads, the objects, corpses, turrets, decals, the sky; [docs/re/visibility.md](docs/re/visibility.md)).
  `src/corpse.c`: the corpses (the level's `CRSA` and the dead AIs' bodies,
  [docs/formats/crsa.md](docs/formats/crsa.md)).
- `src/trac.c`: the animation collections.
- `src/chars.c`: the level's characters (from `CHAR` objects and `ai2_spawn`), their scripts (die,
  defeated, hurt) and the `chr_*`/`ai2_*` commands; `src/activity.c` their activity (an AI unseen for 90 ticks
  loses its active record, its movement stub moving it, until seen or needed again; `chr_all_active`). `src/ai.c` (the AI's executor), `src/movement.c` (the AI's path and movement layer: collisions, steering, turning and slowing), `src/patrol.c` (patrol paths and jobs), `src/alarm.c` (the alarm state and event), `src/pursuit.c` (the pursuit state and the guard's look around),
  `src/knowledge.c` (sight, hurts, the alert), `src/combat.c` (the combat settings, behaviours,
  maneuvers, the gun pickup, targeting and the executor's aim) `src/melee.c` (the melee profiles and techniques), `src/neutral.c` (the neutral characters'
  conversations and the characters' speech) and `src/panic.c` (the non-combatants' panic; all
  in [docs/re/ai.md](docs/re/ai.md)) move the others; `--param ailog=1` logs each fighter once a second, `--param physlog=1` the player's moves the walls stop, `--param hitlog=1` each hit the player takes (the attacker's animation, the angle, whether she could block), `--param soundlog=1` each animation sound played (`src/sound_anim.c`, the SABD records: [docs/formats/sabd.md](docs/formats/sabd.md)).
  `--param call=<function>[,...]` runs level functions at the start (e.g. `spawn_top,final_striker_attack` on level 1).
- `src/character.c`: a first playable. `--param play=1 --param level=1` runs Konoko through level 1
  (`--param at=x,y,z,deg` starts her elsewhere); `--param viewer=level` flies through a level. `--param
  route=<steps>` drives her headless (the steps are described in `app.c`, among them `M`, a wait for a
  script's message that shifts the later frames, `G`, the glass targets shot, `Wx,y,z`, a run to the point
  planned at run time through the rooms' graph (the locked doors kept out, the manual ones opened), `C`, a
  wait for a cutscene's end, `K<id>`, a console used, `X<name>,N`, a character hunted down, `L<type>`, the
  nearest powerup of a type picked up, and `S<N>`, a wait until she stands; `--param trace=N` logs the
  characters and the doors near Konoko every N frames; `--param sltrace=1` the scripts; `--param
  invincible=1` keeps her alive, `--param omnipotent=1` makes her blows fell anyone and `--param
  unstoppable=1` keeps her on her feet (the game's cheat switches), for runs of a level's scripts; without
  them `--param defend=1` has her block and strike back at the AIs fighting her while a route waits or an
  unarmed one is within 12, and use a hypo when low).
- `src/throw.c`: throws (the player's; the victim's part and its own damage), in
  [docs/re/character.md](docs/re/character.md) "Throws".
- `src/weapon.c`: weapons (spawn, pickup, the trigger and shots, drop and the throw, the slots and holstering),
  [docs/formats/onwc.md](docs/formats/onwc.md). Route keys `q` swap, `g` fire, `e` drop. `src/physobj.c`: the
  physics of the weapons and powerups in the world (gravity, the walls, friction; [docs/re/physics.md](docs/re/physics.md)
  "The other objects").
- `src/sl.c`: the BSL engine, running the level's scripts. `src/trigger.c` (trigger volumes),
  `src/object.c` (the level's objects, `obj_*` and `env_*anim`) and `src/env.c` (`env_show`) are its
  first commands; `src/laser.c` the laser triggers (`trig_*`), `src/envparticle.c` the level's
  particle emitters (`particle`), `src/save.c` the save points (`save_game`, `restore_game`).
  [docs/re/character.md](docs/re/character.md) maps the character records for the
  engine's port.

`src/turret.c`: the turrets, on the AI's targeting (`src/targeting.c`, shared with `src/combat.c`)
([docs/re/turret.md](docs/re/turret.md)).

`src/physics.c` gives the characters the original's floor and walls ([docs/re/physics.md](docs/re/physics.md)).
`src/graph.c` (the rooms' graph), `src/localpath.c` (paths through a room's grid) and `src/obstacle.c`
(the grids' run-time obstacles: the characters, objects and doors' panels) find the AI's way
([docs/re/pathfinding.md](docs/re/pathfinding.md)). `src/door.c`: the level's doors open for the characters
near them, and the `door_*` commands ([docs/re/door.md](docs/re/door.md)). `src/console.c`, `src/event.c` and
`src/action.c`: consoles, event lists and the action key (Ctrl) ([docs/re/console.md](docs/re/console.md)).

- `src/particle.c`: the particle system (P3, the BINA `3RAP` classes): events, actions, emitters, wall and
  character collisions, damage. The weapons' shooters and projectiles (`src/weapon.c`) and breaking glass
  (`src/env.c`, with the textures' materials: `src/material.c`) run on it. `src/render/particle_draw.c` draws the sprites,
  geometry, vectors, contrails and lens flares; `src/decal.c` the decals (bullet holes, cut to the walls'
  quads and folded over their edges, [docs/re/decal.md](docs/re/decal.md)), drawn by
  `src/render/decal_draw.c`; the attractors (homing) and the sounds in `src/particle.c`. `src/impact.c`: the impact effects (the Impt and Mtrl
  trees, the ONIE table, ONrImpactEffect) the particles' impact action plays ([docs/re/impact.md](docs/re/impact.md)).
- Aiming (the aiming screens in `src/chr_anim.c`, the recoil in `src/character.c`), reloading
  (`src/weapon.c`) and powerups (`src/powerup.c`): level 1's training plays through to its end.
- `src/wm.c`: the window manager (windows, messages, the WMDD dialogs, the DC, part specs and text; the
  level's load shows its progress in one, `load_begin` in `src/app.c`),
  `src/igui.c` the in-game UI's pages and the text consoles (`text_console`), `src/outgame.c` the main
  menu (the start without `--param play`, and Escape in a level), `src/li.c` the keys as the input
  layer's codes ([docs/re/wm.md](docs/re/wm.md)) and the bindings from `key_config.txt`, the actions
  the player's character takes ([docs/re/input.md](docs/re/input.md)). `--param keys=27,32,@546:250` taps keys into the
  dialogs and clicks (desktop pixels: it is as tall as 480).
- `src/persist.c`: persist.dat (levels reached, the continues, the pause screen's bits, the options);
  `src/outgame.c`'s Load Game lists them (the WM's list box, class 8) and its Options sets them (the
  check box, slider, popup and menu classes, the WM's timers).
- `src/cheat.c`: the cheat codes typed on the pause screen once the game is won ([docs/re/cheat.md](docs/re/cheat.md)).
- The splash screens (`splash_screen` in `src/app.c`, ONrGameState_SplashScreen): each level's intro as it
  loads, the scripts' chapter screens, the won and lost levels' with their music; the main menu's music
  and the level's sounds paused under it (`src/outgame.c`, the `sound_music_*` of `src/sound.c`).
- `src/film.c`: the films (FILM, the `playback` command; the developers' recording, saved_filmNNN.dat), with `begin_cutscene`/`end_cutscene`
  (`src/chars.c`): the cutscene after the training plays and the warehouse mission starts.
- `src/bink.c`: the Bink movies (BIK 'i': the container, the video, the audio, the YUV conversion), ported
  from `binkw32.dll`, whose tables `src/exe.c` loads at run time as `Oni.exe`'s (`dll_*`; 286,208 bytes,
  CRC-32 6696be85); [docs/formats/bink.md](docs/formats/bink.md). `src/app.c` plays the intro before the
  main menu and the outro when level 19 is won (`movie_play`, `movie_play_in_game`; `--param
  movie=outro.bik` plays one through the latter first).

The game time is the original's one clock (`src/gametime.c`, GS+0x144). What isn't ported yet is
marked in the code ("stand-in", "Not ported"); those go as their originals are ported.

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
CD through `OPENONI_CD=<dir|image>`, and skip without data. The game also plays straight from the CD:
`src/vfs_vise.c` layers the installer archive over a mounted CD (`OPENONI_DATA=<image>`, gasm-run
`--asset cd=<image>`, the play page's disc image), inflating each file as it is opened. **All data access goes through `src/vfs.h`**: no
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

`tools/onidat.py game --load` and `build/dat_test` must print the same summary and load lines (`all:
hash=66db30fb`; `OPENONI_DAT_SUMMARY=66db30fb` makes the test check it). Instance data is read through
`tm.h`: link fields hold handles (`tm_link()`), never pointers, so the 64-bit native build works.

The gasm module (`tools/fetch-gasm-sdk.sh` puts wasi-sdk, the gasm C SDK and Binaryen in `.deps/`):

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENONI_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk" \
  -DGASM_WASM_OPT="$PWD/.deps/binaryen/bin/wasm-opt"
cmake --build build-gasm
RUN="$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"
$RUN build-gasm/openoni.wasm --asset-dir game --headless 60 --screenshot out/s.png
```

It builds two modules: `openoni.wasm` (Asyncify, for runners without stack switching) and
`openoni-run.wasm` (stack switching). The game keeps the original's blocking loops: `gasm_main` runs
`app_main` once and every loop ends its frame in `plat_wait_frame()` (`gasm_wait_frame`). Don't unwind
loops into per-frame states.

Checking a change: both modules, and `openoni.wasm --no-stack-switching`, must give the same hash line
and the same screenshot. The `gasm:gl` video hash covers uploads only (buffers, textures, uniforms), not
clears or draws, so until the renderer uploads something it is the empty hash `811c9dc5`; compare
screenshots (`md5`) instead. `node ../gasm/runners/web/headless.mjs` must print the same hash line.

The browser player is `docs/public/play/` (the site's `/play/`): the player's own installed game folder,
imported into OPFS or read in place, run as `openoni.wasm` in gasm's Worker mode. Locally:
`docs/scripts/copy-wasm.sh && (cd docs && scripts/vendor-web.sh && pnpm build)`, then
`node tools/web-play-test.mjs` (headless Chrome: the page's hash lines must equal gasm-run's; with
`OPENONI_CD=<.iso>` it also plays from the CD image).

## Unattended runs

Always `--headless` for gasm-run; never open a window, never use macOS `screencapture`.
With `--screenshot`, gasm-run renders every frame on the GPU and its memory grows by gigabytes over a
long run (15 GB over level 1's route): pass `--param drawfrom=<N>` (nothing drawn before frame N) with
N a few frames before the end, and don't take screenshots of long runs without it. The frames before N
still run the display's logic (`gl_dev_set_dry`), so the game plays the same with or without it.

## Commits

Imperative subject, body explaining why when non-obvious. Never commit `game/`, `cd/`, `oni.exe`,
`oni-mac`, `re/`, `out/`, `build*/`, `.deps/`, `docs/node_modules/`.
