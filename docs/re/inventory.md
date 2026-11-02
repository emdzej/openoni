# Inventory

What `Oni.exe` (Windows v1.0, 1,527,808 bytes, CRC-32 `a8223704`) is made of: 3,655 functions in Ghidra
(3,136 from the auto-analysis, the rest from `FillGaps.java`). The sizes are the better effort estimate
for the port, not the function counts.

## Where the names come from

Bungie's own names, from the Intel Mac build 1.2.1 (`oni-mac`, an i386 Mach-O with symbols). The Mac
build is GCC output of a later version, and the Windows one is MSVC with inlining, so functions are
matched rather than looked up (`tools/ghidra/match_mac.py`, results in `re/mac-matches.tsv`):

1. **Strings** that exactly one function references in each program (source paths compared by file name:
   `F:\BungieDevelopment\BungieSource\...` against `/tmp/OniSource...`): 397 pairs.
2. **Call graph**: the only unmatched callee or caller left on both sides of a matched pair.
3. **Layout**: equal gaps between consecutive matches that run in order in both programs (source
   files keep their function order).
4. **Neighbourhood**: an unmatched function whose matched callers and callees map onto those of exactly
   one Mac function.

`match_mac.py --validate=<share>` hides that share of the string matches and checks what steps 2-4 make
of them. Over four 50% holdouts: neighbourhood >= 0.75 is right 96-97% of the time, callee 89%,
neighbourhood < 0.75 86%, caller 79%, layout 72%.

`tools/ghidra/names.tsv` takes only the string and neighbourhood >= 0.75 matches: **1,055 names**,
confidence `med`, each noted with its method. The other ~600 candidates stay in `re/mac-matches.tsv`
until someone reads them. A spot check found a wrong low-score match (a list insert matched to
`ONrCharacter_SetWeaponVarient` at 0.67): that is why the threshold is there.

## Modules

Bungie's convention: `XXrName` public, `XXiName` internal, `XX` the module. Unnamed functions between two
named ones of the same module are counted with it. "unassigned" also holds the C runtime and the
OpenGL/Bink import glue (0x51e000 and up).

3655 functions, 1065 KB of code; named 1022 (45% of the bytes)

| Module | Functions | Named | KB | Range |
|---|---|---|---|---|
| ON | 578 | 169 | 201.0 | 0x4332b0-0x5177e0 |
| unassigned | 625 | 0 | 175.7 | 0x401000-0x528e4c |
| AI2 | 553 | 262 | 168.7 | 0x489d20-0x4bb370 |
| P3 | 102 | 37 | 74.9 | 0x44eac0-0x468650 |
| OBJ | 365 | 66 | 69.1 | 0x4bee60-0x4d1f00 |
| MS | 58 | 13 | 47.5 | 0x40de90-0x419e30 |
| WM | 184 | 43 | 46.7 | 0x4687d0-0x476b90 |
| SL | 93 | 45 | 27.0 | 0x4777d0-0x47dac0 |
| PH | 71 | 29 | 24.7 | 0x43a240-0x50edb0 |
| OS | 69 | 21 | 22.2 | 0x4023d0-0x516670 |
| M3 | 138 | 49 | 21.9 | 0x41a360-0x41f9c0 |
| TR | 83 | 27 | 20.4 | 0x427290-0x42c330 |
| SS | 79 | 20 | 19.1 | 0x47f090-0x4f49b0 |
| WP | 91 | 20 | 18.9 | 0x4e1420-0x51d180 |
| TM | 52 | 23 | 16.5 | 0x41ff40-0x4242c0 |
| AK | 35 | 12 | 16.5 | 0x43ffe0-0x444030 |
| CO | 40 | 11 | 10.4 | 0x42f510-0x431ff0 |
| MU | 66 | 25 | 8.3 | 0x432f10-0x435590 |
| AI | 51 | 14 | 8.2 | 0x4bb790-0x5051e0 |
| UU | 77 | 27 | 7.9 | 0x401ef0-0x4c5c80 |
| TS | 27 | 11 | 6.3 | 0x42d9e0-0x42f250 |
| CA | 16 | 6 | 6.1 | 0x4d59a0-0x4d7110 |
| OB | 22 | 8 | 5.8 | 0x438a30-0x439f10 |
| EP | 18 | 2 | 5.3 | 0x4543b0-0x4558a0 |
| BF | 33 | 19 | 4.2 | 0x401300-0x402c10 |
| OC | 6 | 3 | 3.8 | 0x4f2910-0x4f3610 |
| MA | 13 | 7 | 3.5 | 0x4871d0-0x487f50 |
| SS2 | 13 | 4 | 3.5 | 0x4830c0-0x487080 |
| LI | 17 | 7 | 3.3 | 0x403c60-0x425e50 |
| AU | 25 | 5 | 3.0 | 0x437470-0x4380e0 |
| AS | 5 | 3 | 2.8 | 0x4bd710-0x4be240 |
| GR | 5 | 5 | 2.6 | 0x4320f0-0x432bb0 |
| DC | 15 | 9 | 1.6 | 0x469eb0-0x46a5b0 |
| IM | 7 | 3 | 1.5 | 0x4478d0-0x447f20 |
| AM | 5 | 3 | 1.3 | 0x4d4db0-0x4d52c0 |
| FF | 1 | 1 | 1.3 | 0x445660-0x445660 |
| PS | 4 | 3 | 1.1 | 0x46a610-0x46aa60 |
| FX | 2 | 2 | 0.6 | 0x4f46c0-0x4f4720 |
| CL | 2 | 2 | 0.5 | 0x43c680-0x43c7a0 |
| OW | 4 | 2 | 0.5 | 0x4be9d0-0x4bebd0 |
| OBD | 3 | 2 | 0.5 | 0x4d23a0-0x4d24e0 |
| BD | 2 | 2 | 0.5 | 0x47ebc0-0x47ec80 |

What the larger modules are, from their function names:

| Module | What |
|---|---|
| ON | The game: characters (`ONrCharacter_*`), the out-of-game UI and menus, films, sky, game settings, Motoko setup |
| AI2 | The AI: melee, combat, alarms, knowledge, movement, patrols |
| AI | Older AI and script glue (`AIrScript_*`, `AIiScript_*`) |
| P3 | Particles and decals |
| OBJ | Objects: doors, turrets, triggers and trigger volumes, furniture, consoles, sounds, flags, patrol paths |
| MS | Motoko's geometry engine: clipping, the environment and geometry draw methods |
| M3 | Motoko's draw API (`M3rDrawContext_*`, `M3rGeom_*`, cameras, matrix stack) |
| WM | The window manager: dialogs, menus, windows (the front end's UI toolkit) |
| SL | Scripting: BSL (`SLrScript_*`) |
| PH | Physics and collision (sphere trees, `PHrPhysics_*`) |
| OS | Oni's sound layer: ambient, impulse and group sounds, positions and volumes |
| SS / SS2 | The sound system underneath, subtitles and messages |
| TR | Animations (`TRrAnimation_*`) |
| WP | Weapons |
| TM | Templates and instance files (`TMrInstance_*`, level loading) |
| AK | The environment: level geometry, collision (`AKr*`) |
| CO | Console and configuration |
| MU / UU | Maths / utilities (memory, strings) |
| TS / DC | Text and fonts / 2D drawing of strings and textures |
| CA | Camera |
| BF | Files (`BFrFile_*`, file refs) |
| LI | Input bindings and modes |
| GR | Parser for the config groups (`GRiGroup_*`) |
| AS | Path generation (`ASrPath_*`) |
| OC | Cinematics |
| MA | Materials and impacts |
| IM | Images, pixels |
| AU | Containers (hash tables, shared arrays) |

The renderer's OpenGL back end has no Bungie prefix and falls under "unassigned": `gl_draw_engine_*` from
0x404e00, `initialize_opengl` at 0x40bcc0, and more `gl_*` functions elsewhere (`gl_texture_list_add` at
0x488410).
