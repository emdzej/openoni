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

## 1. Skeleton, file layer, exe loader (done)

CMake with two builds (native tests, `-DOPENONI_PLATFORM=gasm`), `platform.h`, `app.c`,
`platform_gasm.c` (title "Oni", 60 Hz, raw keyboard, locked pointer, pads, storage, audio push),
`tools/fetch-gasm-sdk.sh` / `fetch-gasm-runner.sh` (pin 0.16.0), `vfs.c` (host folder via
`--asset-dir`, case-insensitive, `/` or `\\`), `exe.c` (maps `Oni.exe`, checks size + CRC).

`app_main` keeps the original's loops on `gasm_main` + `gasm_wait_frame` (two modules: `openoni.wasm`
with Asyncify, `openoni-run.wasm` with stack switching). The test screen draws one bar per level file,
sized by the file. `ci.yml` builds both modules and checks that they refuse to start without the game.

**Check (passes):** the test screen runs headless from `--asset-dir game`. `openoni.wasm`,
`openoni-run.wasm` and `openoni.wasm --no-stack-switching` give the same hash line and the same
screenshot (md5 `a4c1e5b8`); the Node runner gives the same hash line. The video hash is the empty
`811c9dc5`, since `gasm:gl` hashes uploads and the test screen only clears. Esc at frame 30 quits with
exit code 0. `exe_test` finds the start-up strings at their addresses.

## 2. Instance files (done)

`src/tm.c` ports the instance manager ([format](../formats/dat.md)):

- The template table and the layouts, read from the exe at run time.
- The start-up check of the level files (header, template checksums).
- `TMrLevel_Load`: the whole `.dat` is read, and the links are resolved in place, by name across the
  loaded files, first loaded first. Link fields hold 32-bit handles, read with `tm_link()`, so the
  64-bit native build behaves the same.
- `TMrLevel_Unload`.
- `tm_raw_read` for `.raw`.

`tools/onidat.py` is the reference reader. The gasm build loads level 0 at start-up, as `ONiMain` does.

**Check (passes):** `dat_test` walks every instance of the 15 levels by its layout: 155,232 instances and
358,404 links, all to the right tag. Its summary lines equal `tools/onidat.py`'s (`all: hash=66db30fb`).
It loads every level after level 0, and the link counts (into level 0, unresolved) equal the tool's
`--load`. Every instance is found again by its handle and by name.

`openoni.wasm`, `openoni-run.wasm` and the Node runner all load level 0: 9,347 instances, 5,539 links.

## 3. Format decoders and viewers

TXMP (16/32-bit formats, mipmaps, animated textures), M3GM geometry, AKEV environment, TRAM
animation, ONCC character assembly, fonts, SUBT/text, SNDD sounds (ADPCM). Add exports to PNG/OBJ/WAV in
`out/`.

**Check:** every texture/model/sound decodes and the C hashes equal Python. A level and Konoko exported
to OBJ look right.

**Progress:**

| Part | State |
|---|---|
| Textures (TXMP), all five texel types of the CD, with mipmaps | done: `src/im.c`, `src/txmp.c`, `tools/onitex.py`. `txmp_test` equals the tool on all 8,918 textures (`69e02682`). [Format](../formats/txmp.md) |
| Geometry (M3GM) and its arrays | done: `src/m3gm.c`, `tools/onigeom.py`. `m3gm_test` equals the tool on all 37,862 geometries (`8d974ee1`). [Format](../formats/m3gm.md) |
| AKEV environment: the gunk quads, their textures and what is drawn | done: `src/akev.c`, `tools/onienv.py` (OBJ and preview exports). `akev_test` equals the tool on all 14 environments, 450,921 quads (`1f7ad149`). [Format](../formats/akev.md) |
| TRAM animation: the rotation streams, root motion and height | done: `src/tram.c`, `tools/onitram.py`. `tram_test` equals the tool on all 8,604 animations (`a992f7fd`) and decodes all 393,903 frames. [Format](../formats/tram.md) |
| ONCC character assembly: bodies, bones, posing | done: `src/body.c`, `tools/onibody.py` (posed OBJ and preview). `body_test` builds all 29 classes of level 1 and poses Konoko as the tool does. [Format](../formats/oncc.md) |
| Level objects (OBJC): characters, trigger volumes | done: `src/objc.c`, `src/trigger.c`. `trigger_test` loads the 787 volumes of the 14 levels. [Format](../formats/objc.md) |
| Object animations (OBAN) and the starting objects (OBOA) | done: `src/oban.c`, `src/object.c`, `tools/onioban.py`. `oban_test` equals the tool on all 1,067 animations (`1f2bffdc`, 334,386 frames). `object_test` creates the 912 objects scripts can create. [Format](../formats/oban.md) |
| Fonts (TSFF, TSFT, TSGA) | done: `src/font.c`, `tools/onifont.py` (glyph sheets). `font_test` equals the tool (`85c7502f`, with the text below). [Format](../formats/font.md) |
| SUBT and text (IGSt, IGSA, IGPG, IGPA, IGHH, StNA) | done: `src/text.c`, `tools/onifont.py --text`. [Format](../formats/text.md) |
| SNDD sounds (MS ADPCM) | done: `src/sndd.c`, `tools/onisnd.py` (WAV export). `sndd_test` equals the tool on all 7,386 sounds (`97eb43b7`). [Format](../formats/sndd.md) |

## 4. Renderer (OpenGL 1.x on GLES 3)

Port Motoko/M3 draw contexts onto the GL emulation: environment (sorted opaque/alpha quads,
lightmaps, environment maps), sky, fog, characters (part hierarchy), 2D path for UI and HUD.

**Progress:** `src/render/gl_dev.c` is the device: one GLES 3 shader for textured, vertex-coloured
triangles, with alpha test, blending, depth and fog. The renderer is built for gasm only. On top of it:

- `src/render/env_draw.c`: the drawn quads of the environment as one mesh, sorted by texture, with the
  solid quads first and then the transparent ones.
- `src/render/char_draw.c`: posed character bodies, through `src/render/geom_draw.c` (one geometry
  under a matrix), which also draws the level's objects.

`--param viewer=level --param level=N [--param cam=x,y,z,yaw,pitch]` flies through a level, with its
characters (from the level's `CHAR` objects) standing in their idle animations. The camera starts behind
the player.

Still to do:

- a capture of the original under Wine at a known place, to compare the environment's colours with (the
  draw path matches the exe step by step: [lighting.md](lighting.md) "The environment's colours"; the
  environment has no reflection maps in the original)

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

**Progress:** a first playable, `--param play=1 [--param level=N]`. Konoko starts where the level's
`CHAR` object puts her. W, A, S and D run (Shift walks), the mouse or Left/Right turns her, and the
camera follows behind.

What is ported:

- `src/trac.c`: the collection load (`iAnimationCollection_DiskToMemory` 0x428250: empty entries
  dropped, sorted, weights as running sums, the shortcuts' collection) and the lookup
  (`TRrCollection_Lookup` 0x427f00, with `UUrRandom` 0x426e10).
- `src/character.c`: the movement types tried for the input, with their start types (0x4f83f0); the
  stops; the next animation at an animation's end; the root's motion turned by the facing.

Jumping and falling are ported: `HandleStartJumping` 0x4fa2f0, the landing (normal and hard) and
the long fall of `HandleHeartbeatInput` 0x4f6ad0, holding jump for a higher rise, the in-air branch of
`HandleFallThrough` 0x4f9a70, and step 9 of `DoCharacterFrame` (takeoff, gravity ONCC+4, the fall's
limit ONCC+0xc). Space jumps, Shift crouches (the exe's default bindings). `character_test` jumps on
level 1's floor: about 10.6 up, 37 ticks in the air, then the landing.

The character's physics is ported (`src/physics.c`, [physics.md](physics.md)): the body box (0x4e5ff0)
and sphere tree (0x4dc160), the move slid along the walls (`OBiPhysics_Update` 0x43af50 with the
character's environment search 0x4ebd70), and the floor (the ground callback 0x4eca00,
`FindGroundCollision` 0x4ec040: the step up, the landing and its damage, the fall after 12 ticks without
ground). `physics_test` stands Konoko on the training room's floor, runs her into a wall and drops her
40 onto the floor. Since then the physics runs after all
the characters' frames (`OBiPhysics_Update`) and the characters push each other off (0x4e8c50). The ground probe of step 9 is the original's
`AKrCollision_Point` (`akev_collide_point`, the segment walked through the octree's leaves).

## 7. BSL scripting and level logic

BSL lexer/interpreter with the original's built-in functions and variables. Triggers, trigger volumes,
doors, consoles, turrets, cutscene cameras (OBAN), objectives, and saves at checkpoints.

**Check:** the training level is playable start to finish (scripted demo, hashed).

Progress:

- `src/sl.c`: the lexer of `SLiToken_Make` 0x47dac0, and a compiler to stack code whose threads can
  stop anywhere (sleep, blocking commands). `tests/sl_test.c` compiles all 14 folders and runs the
  warehouse's `main`.
- `src/trigger.c`: trigger volumes (`ONrGameState_UpdateTrigger` 0x4e0cf0) and the `trigvolume_*`
  commands. In play mode, entering a volume runs its script. `tests/trigger_test.c` loads the 787
  volumes of the 14 levels and walks a character through one.
- `src/chars.c`: the character list. The level's characters are created from its `CHAR` objects
  (those without flag 0x04) and by `ai2_spawn`. The trigger volumes see all of them. Commands:
  `chr_delete`, `chr_teleport` (to a `FLAG`), `chr_animate` and `chr_animate_block`
  (0x4b6e10, 0x4eb340), and `chr_wait_animtype` (the names come from the exe's table at 0x54d7c0).
  `tests/chars_test.c` covers them on level 1.
- `src/door.c`: doors ([door.md](door.md)). At the level's start each door finds its panels and its gunk
  (`OBJrDoor_LevelBegin` 0x4c48a0). `OBJrDoor_Update` 0x4c44f0 opens a door for a character near it
  that may pass, and closes it when nobody is. Open hides the gunk and plays the panels; once closed they
  go and the gunk is back. Commands: `door_open`, `door_close`, `door_lock`, `door_unlock`, `door_jam`,
  `door_unjam`. `tests/door_test.c`: Konoko walks through door 18, and the locked door 11 opens after
  `door_unlock`. The panels' physics and the rest since: [door.md](door.md) "The port".
- `src/console.c`, `src/event.c`, `src/action.c`: consoles, event lists and the action key
  ([console.md](console.md)). `tests/console_test.c`: Konoko uses the training's console 10. The script
  trace (`--param sltrace=1`) logs script calls, messages and dialogue with the frame number.
- The characters' scripts: the die and defeated scripts of the death hook (0x4eb550) and the hurt and
  defeated scripts of `ONrCharacter_TakeDamage` 0x4e65f0, run through the deferred queue (0x4f2070,
  0x4f2120). More commands: `chr_changeteam`, `chr_nocollision`, `ai2_setjobstate`,
  `chr_wait_animstate`, `chr_lock_active`, `chr_death_lock`, `chr_unkillable`, `chr_invincible`,
  `ai2_makeignoreplayer`. The combat state ends when its target dies (docs/re/ai.md, "Leaving combat").
  `tests/chars_test.c` stages the training's combat demo.
- A route autopilot (`--param route=`, in `app.c`) plays the training headless and the same on every
  runner: movement, the console, the dash, the track, doors, jumps, crouching, the AI's combat demo,
  the crouch moves, the slide, the jump-flip, the fight lessons (punch, kick, combos) and the hit
  flashes, the throw lesson (the manual door 100 with the action key, Throw_Forward_Punch on the dummy)
  and the block lesson, and the shooting lesson's start: the pistol picked up (Q, Pickup_Pistol_Mid),
  fired (Autopistol_Recoil) and dropped (E), to about frame 29,600. Next: the particle system (the
  shots: the glass targets and the droids need them), powerups (ammo) and reloading, the laser
  triggers (`trig_activate`).
- Weapons (`src/weapon.c`, docs/formats/onwc.md): weapons in the world (`weapon_spawn`), picking up
  (the swap key, the pickup animation's action frame), the hand's variant, the weapon on the fist's
  node, the trigger, the shooters and their pulses (ammo, delays, the recoil overlay), dropping. The
  overlays (`chr_anim.c`: four slots, blended into the drawn pose). The shooters' particles are
  on the particle system since (`src/particle.c`).
- Throws (`src/throw.c`, docs/re/character.md "Throws"): the player's throw moves become throws of the
  enemy in front (`RemapAnimationHook` 0x4ea550, `AttemptThrow` 0x4eaa10), the victim's part as the
  throw starts, the victim held and its own damage. The AI's throws followed (`src/melee.c`).

## 8. Combat systems

Weapons (fire, reload, ammo, dropping), particle system (BINA particle classes, a large subsystem),
damage and hit reactions, power-ups, inventory, HUD.

Progress: melee hits (`src/chr_hit.c`): the parts' contact, the attacks that land, reactions, damage,
knockback, death, blocking. `tests/hit_test.c`. The HUD's health ring (`ui_draw_hud`:
`ONrInGameUI_Display` 0x4fff50's "right" element, `ONiInGameUI_DisplayHealth` 0x500970, the arcs of
`ONrInGameUI_SetArcPoints` 0x504270, the colours of the `game_settings` ONGS), with its easing
(`ONiInGameUI_UpdateDamageMeter` 0x5048e0, from `ONrInGameUI_Update` 0x504780), the shield's arcs
(`ONiInGameUI_DisplayShield` 0x501080) and the hits' (`ONiInGameUI_DisplayHits` 0x500db0, set in
`ONrCharacter_TakeDamage` by the hit's side). Weapons and particles are next.

## 9. AI

Combat and melee behaviour profiles, alarm network, pathfinding on the BNV graph, patrols, neutral
characters. **Check:** every level (1-14) is completable through scripted demos. Spot-check by hand.

The module is large: 556 functions, about 175 KB of code between 0x487000 and 0x4b9000 (262 of them
named from the Mac build). `AI2rUpdateAI` 0x489d40 runs, for each living AI character (C+0x1e8 = 2):
the requested scripts (C+0x1f0 bits 8, 0x10, 0x40, 0x80, 0x20), a scripted turn (C+0xfa8), the
vocalization timer, the state's return to its job (C+0x1f8 = 0: back to the job state at C+0x1f4, or
idle), the neutral trigger (state 8), the alarm timer, then the state's update: 1 idle, 3 patrol, 5
combat, 6 (0x4b0fb0), 7 alarm, 8 neutral, 9 panic (0x4adc80). Then, unless the film plays, the
movement (`AI2rMovement_Update`), which drives the character's input through the executor. States
are entered through `AI2rEnterState` 0x48ad50 and left through `AI2rExitState`.

The order to port it, each step with a test on level 1:

1. **The executor and movement**: facing, moving to a point (`AI2rExecutor_Move`), the input it
   gives the character (the block at A+0x2158). Test: an AI walks to a flag. *Executor done* (`src/ai.c`,
   `tests/ai_test.c`); the movement layer since (`src/movement.c`).
2. **Idle and patrol**: `AI2rIdle_Update`, `AI2rPatrol_Update` with the level's patrol paths
   (`CJBOPatrol Path`). The scripts' `ai2_dopath`, `ai2_setjobstate`. Test: a warehouse thug walks
   its path. *Patrol done* (`src/patrol.c`, `tests/patrol_test.c`): the paths, the patrol jobs the
   characters start with, `ai2_dopath`, the waypoints' moves, waits, loops, modes and scripts, now
   on the movement layer, with the glances, the scans and the facing locks.
3. **Knowledge**: sight and hearing (`AI2rKnowledge_*`, 0x49a6e0), the alarm (`AI2rAlarm_*`), the
   `ai2_makeignoreplayer`, `ai2_passive` and alert commands. Test: a thug notices Konoko. *Sight,
   hurts and the alert done* (`src/knowledge.c`, `tests/knowledge_test.c`, notes in
   [ai.md](ai.md)): contacts, their order and forgetting, the sight cones and lines, hurts, the teams,
   the calm and pursuit handlers raising the alert, startles, the move to pursuit and combat (stand-in
   states until steps 5 and the pursuit's port), `ai2_passive`; the sounds and the alarm (`src/alarm.c`)
   since.
4. **Pathfinding** on the BNV graph (the level's `AKVA` volumes and their connections). *Global
   path done* (`src/graph.c`, `tests/graph_test.c`, notes in [pathfinding.md](pathfinding.md)): the
   graph (`PHrBuildGraph`), the blockages from the rooms' grids (`PHiCheckForBlockages`), the room under
   a point (`AKrNodeFromPoint`), the rooms' A* (0x50bdb0) and `AI2iPath_MakePath`'s reuse; the AI's
   mover walks it room by room, each room by its local path through the grid (`src/localpath.c`:
   `ASrPath_Generate`, `ASiList_CalcWaypoints`). The movement state's steering and the doors
   on the links followed (`src/movement.c`). The grids' run-time obstacle layer
   (`PHrPrepareRoomForPath` 0x50d660: characters, objects, the doors' panels; `src/obstacle.c`), the
   doors' danger factors in the A*, the searches as the character, and the clear point a new
   character steps to (`AI2iMovementState_FindClearPoint` 0x4a9170). Still
   stand-ins: those listed in [pathfinding.md](pathfinding.md).
5. **Combat and melee**: `AI2rCombat_Update`, the melee profiles (`CJBOMelee Profile`),
   `AI2rMelee_Update` 0x4a01f0 and its technique weighting 0x4a2580, blocking
   (`AI2rBlockFunction`). Test: a thug fights Konoko. *Begun* (`src/melee.c`, `tests/melee_test.c`,
   notes in [ai.md](ai.md), "Melee"): the profiles, the techniques picked by facing, range and
   delay, positions, maneuvers, attacks pressed through the executor's override, combos, the fight
   cookie, blocking by chance and difficulty; the attack prediction the weighting uses
   (`src/chr_intersect.c`, `tests/intersect_test.c`). The profile in memory as the original builds it
   (`PrepareForUse`, `MoveIsValid`, the transitions), the weighting, the update, the maneuvers and the
   positions run on it; the characters push each other off (0x4e8c50). The throws, the movement's
   collisions (`AI2iMovementState_MakeCollision`) and `AI2rCombat_Update`'s ranges followed (`src/melee.c`,
   `src/movement.c`, `src/combat.c`).
6. **Neutral characters and panic**.

## 10. Audio and movies

Oni Sound System (ambient, impulse, groups, music, dialogue, 3D positional mix). Bink: reuse
OpenCMR2's `bink.c` if Oni's `.bik` revision matches, otherwise extend it.

## 11. Release

Saves and options parity, the original's cheats, a browser player (`docs/public/play/` from OpenCMR2:
ISO pick, extract into OPFS with the same VISE code), gasm bundles for macOS, Linux and **Windows** (pointing to visextract for the CD), site at openoni.emdzej.pl, `release.yml`.
Optional widescreen and high-res stay non-default params.

## Risks

- **Mac-to-Windows name transfer** is GCC vs MSVC code. Expect good matches on functions with strings or
  unique calls and weaker ones on small leaf functions. Unmatched functions get names by hand.
- **Size:** Oni is a much bigger program than Return Fire, comparable to or bigger than CMR2.
  Particles, AI and BSL are each a sizeable port. Order the work so every step is visible and checkable.
- **Browser memory:** level files plus textures need watching against the 1 GiB guest limit. Load per
  level, as the original does.
