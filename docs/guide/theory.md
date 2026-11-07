# How Oni works

A theory of operation of Oni (Bungie West, 2001): how the game is built, from the frame loop to the
missions. It comes from porting the Windows executable function by function. It is meant for anyone
who wants to learn how a third-person action game of its time is put together. The details, with the
exe's addresses and layouts, are in the repository's reverse-engineering notes
([`docs/re`](https://github.com/emdzej/openoni/tree/main/docs/re)) and in the format pages of this site.

## The shape of the program

Oni is one executable of about 1.5 MB, written in C. Its code is split into modules, each with a
two- or three-letter prefix. `XXr` names a module's public functions and `XXi` its internal ones:

| Prefix | Module |
|---|---|
| `UU` | Utilities: memory, bit vectors, random numbers, errors |
| `MU` | Maths: vectors, matrices, quaternions |
| `BF` | Files |
| `TM` | Templates and instances: the data |
| `M3`, `MS`, `gl` | Motoko, the draw engine: cameras, geometry, the OpenGL back end |
| `AK` | Akira, the environment: the level's quads, collisions, visibility |
| `PH` | Physics of the non-character objects |
| `TR` | Bodies and animations |
| `ON` | The game itself: characters, levels, the game state, the HUD, the menus |
| `AI2` | The AI |
| `OBJ`, `OB` | The level's objects: doors, consoles, triggers, turrets, furniture |
| `WP` | Weapons and powerups |
| `P3` | Particles |
| `SL` | BSL, the scripting language |
| `SS2`, `OS` | Sound |
| `WM` | The window manager the menus and dialogs are built on |
| `LI` | Input |
| `CA` | The camera's behaviour |

The program starts, sets up every module, loads level 0 (the data all levels share and the menus) and
plays the intro movie. Then it runs two loops, one after the other, each blocking until it is done: the
out-of-game menus, then the game. A dialog runs a loop of its own on top of whatever is underneath.

## Time

The game runs at a fixed 60 ticks a second. Each pass of the game loop reads the input, works out how
many ticks have passed since the last pass, and advances the game state by that many. It plays the
sound and draws one frame. Everything that plays (characters, AI, particles, scripts) moves in ticks.
The draw shows the state as it is after the last tick. A slow machine draws fewer frames, but the
game plays the same.

Two random number generators feed the game, both the same linear congruential generator
(×0x19660d + 0x3c6ef35f) with separate seeds. `UUrRandom` serves the gameplay (the AI's choices,
animation variants), and `UUrLocalRandom` the effects (particles, the sound's permutations). Nothing
else is random, so a run with the same input plays the same. The port relies on this to test whole
levels unattended.

## The data

Every asset is an *instance* of a *template*. A template is a C structure identified by a four-letter
tag: `TXMP` a texture, `M3GM` a geometry, `TRAM` an animation, `ONCC` a character class, `AKEV` the
environment, `ONLV` a level, and so on. The layouts are compiled into the executable; the files hold
only the data. A level is two files: `levelN_Final.dat` holds the instances and `.raw` the bulk data
(pixels, sound samples). The instances point at each other by *links* that loading resolves into
pointers. Level 0 holds what every level shares; a level's links into it resolve by name.

What a level is:

- `ONLV`: the level's root. Its links lead to the environment, the character setups, the objects, the
  corpses, the sky, the particles and the rest.
- `AKEV`, the environment: the level's geometry as quads (the *gunk*), each with a texture, baked
  vertex shades and flags (glass, transparent, ghost, stairs, no-collision), plus an octree over them
  and a graph of rooms (BNVs) for pathfinding.
- `OBJC` and the like: the objects placed in the level (doors, consoles, turrets, furniture, trigger
  volumes, laser triggers, weapons, powerups, particle emitters, the flags that mark places for the
  scripts).
- `AISA`: the character setups the scripts spawn from: who, where, with what weapon, which team, and
  which scripts run when they die or are beaten.
- The BSL scripts, in text files outside the instance files (`IGMD/<level>/*.bsl`).

Templates are the reason the game could be made by designers: nearly all behaviour is data. A
character class says how fast it runs, which animations it has, how it fights and what the AI does
with it. A weapon class says what it fires, through which particle classes.

## The world

The environment is static geometry. Its lighting is baked into the quads' vertex shades, so drawing it
needs no lights. Everything that moves is drawn as geometry (`M3GM`) under a matrix and lit by an
ambient light and one or two directional lights.

Collisions go through the octree: a sphere swept along a move, or a point along a segment, is tested
against the quads in the leaves it passes through. Characters are a stack of spheres on the floor. The
character physics finds the floor under the feet, slides along walls and climbs stairs. Weapons and
powerups on the ground have simple physics of their own: gravity, bounces, friction.

Each frame, the draw casts a grid of rays from the camera through the octree. The rays mark the leaves
and quads they reach, and stop at the first opaque quad. Only the marked quads are drawn, and the
objects only when their leaves are marked. If a ray gets out of the level, the skybox is drawn behind
everything ([visibility](https://github.com/emdzej/openoni/blob/main/docs/re/visibility.md)).

## Characters

A character is three things:

- its **class** (`ONCC`): the body, the animations, the speeds, the hit points, the AI's settings;
- its **body**: a hierarchy of 19 parts, each a geometry;
- its **state** in the game: position, facing, the animation playing, health, the inventory.

Movement and fighting are driven by **animations**, not by code that moves the body. An animation
(`TRAM`) has:

- a type (run forward, punch, throw, fall, get up, ...);
- the states it may start from and the one it leaves the character in;
- the bones' rotations, frame by frame;
- the root's motion, which moves the character;
- the attacks: frame ranges where a part does damage, with the damage, the knockback and the hit's
  type;
- the frames that block, and the particles and sounds it starts.

The class's animation collection (`TRAC`) maps (state, type, varient) to the animations that fit, with
weights for random choice. Every tick, the input becomes an animation type. For the player, the keys and
the mouse are bound to named actions in a text file (`key_config.txt`: `bind w to forward`), and each
action is a bit in a 64-bit mask; the AI's executor sets the same bits. That mask becomes the
animation type; the collection picks the animation for the character's state; the animation moves
it. Combos are animations that start from the state another attack leaves. A throw is a pair of
animations, one for each side, played together.

A hit is a test of the attacking parts' spheres against the target's parts while an attack's frames
run. Its damage, its knockback and its type decide whether the target flinches, is stunned, knocked
down or thrown; a block turns it away
([character](https://github.com/emdzej/openoni/blob/main/docs/re/character.md)).

## Weapons, particles and impacts

A weapon class (`ONWC`) gives the geometry, the ammunition, the firing modes and the particle classes
it fires. The shots themselves are particles. The particle system (`P3`) is general: a class is a list
of variables, events (created, every tick, hit a wall, hit a character, died) and the actions run on
them (move, emit, damage, play a sound, leave a decal, explode). Bullets, rockets, sparks, steam and
glass shards are all particle classes in data.

When something hits something, the impact system picks the effect from a table by the impact's type
(a punch, a bullet, a fall) and the material struck (metal, glass, flesh). The effect is particles, a
sound and a decal. Decals (bullet holes, scorches, the characters' shadows) are cut to the quads they
land on and folded over their edges.

## The AI

The AI ([ai](https://github.com/emdzej/openoni/blob/main/docs/re/ai.md)) is layered.

- **Knowledge.** Each AI keeps a list of contacts: characters seen (central or peripheral vision,
  checked by rays), heard (gunfire, melee, footsteps, sounds the scripts play) or felt (being hurt).
  A contact has a strength that fades with time. The teams decide who is hostile: the Syndicate is
  hostile to everyone but its accessories, civilians included. A character of a neutral team turns on
  whoever hurts it, unless the blow came by the way, in a fight with someone else near.
- **Alert.** The contacts raise the character's alert level, from lull to combat.
- **States.** A state machine on top: idle, guard, patrol (a path of points with waits, looks and
  scans), pursuit (hunting a lost contact), alarm (running to an alarm console), combat, neutral
  behaviour (the civilians' conversations and their help) and panic.
- **Combat.** A behaviour for the range (far, medium, near): shoot from cover, rush, keep away. Each
  tick a maneuver is chosen by weights (advance, retreat, circle, dodge, melee). Aiming has the
  class's accuracy and delays. Melee is a profile of techniques (moves with their chances and
  conditions) that the AI picks among as the fight goes.
- **Movement.** The plan becomes motion through a movement layer: facing, walking or running,
  dodging, avoiding the walls. The way is found through the graph of rooms (the global path) and a
  grid inside each room (the local path). The grid is baked with the level (walls, crates, stairs);
  over it, every 20 ticks, the room marks what moves: the other characters, the loose objects, the
  doors' panels. A character may ignore some of these marks: a shut door it can open, the one it
  follows, the target it is fighting.
- **The executor.** At the bottom, the AI presses the same virtual keys a player does. The character
  code can't tell the two apart.

## Missions

A mission is the environment plus scripts plus the objects the scripts drive. The scripts are BSL, a
small C-like language: functions, variables, `if`, `fork` (start a function as its own thread),
`sleep` (wait ticks) and commands the game registers by name (several hundred). When the level loads,
its `main` function runs. It sets the stage and the objectives, and forks the threads that follow
the story.

The scripts mostly wait for the world to tell them something:

- **Trigger volumes** run a function when a character enters or leaves them (often only the player,
  or only once).
- **Laser triggers** run one when something breaks their beams.
- **Consoles** run an event list when used: unlock a door, open it, start a script. They can be
  active or not.
- **Characters' scripts** run when an AI dies, is beaten or is hurt. The setups (`AISA`) name them.
- **Doors** can be locked and unlocked by the scripts, and opened by consoles.
- Commands that wait (`chr_wait_animation`, waiting for a character to arrive or for a cutscene to
  end) let a thread follow the action.

What the scripts command:

- **Characters**: `ai2_spawn` creates an AI from a setup; `chr_*` and `ai2_*` commands set its job,
  path, target, team, alert, and make it say or play things.
- **The player's goals**: `objective_set` puts up the mission's text, `target_set` the HUD's arrow to
  a place. Save points (`save_game`) record where the player comes back after a death (`restore_game`).
- **Cutscenes**: `begin_cutscene` takes the control and puts the letterbox on; films (recorded input,
  played back on characters), camera animations and dialog play; `end_cutscene` gives it back.
- **The level's things**: `env_show` shows or hides parts of the environment, `obj_*` animates
  objects, `particle` starts and stops emitters, `sound_*` plays music and ambiences.
- **The end**: `win` ends the level and moves on, `lose` ends it as failed.

A typical level (the warehouse, level 1) runs like this:

1. Training, then a cutscene.
2. Konoko is let into the warehouse. Trigger volumes along the way spawn the guards of the bays
   ahead of her.
3. Consoles open the doors between the bays. Some are locked until a script has run.
4. A civilian must be talked to (the action key near a neutral starts its conversation) before the
   last console works.
5. The scripts save the game at points along the way, and the level ends with `win`.

The difficulty is set in data: the AI's skills by class, and how many enemies the scripts spawn. A
level is a hand-made sequence of fights and puzzles strung on triggers. Nothing is generated.

## Saving

A save point (`save_game n`) stores little: its number, the player's health, place and facing, her
inventory and her weapons by class and ammunition. The world isn't saved. Loading a save starts the
level afresh. The level's scripts read the save point reached (`save_point`) and jump ahead to it,
setting up what that part of the level needs. `restore_game` then puts the player back as she was.
`persist.dat` keeps the levels reached, the continues and the options.

## Presentation

- **The camera** follows the player at a distance, avoids the walls, and is taken over by scripts for
  cutscenes (orbits, recorded animations, interpolations).
- **The draw**: the sky (when seen), the environment's marked quads (solid, then transparent), the
  objects, the corpses, the characters (each with a level of detail by its size on screen), decals,
  particles, lens flares, then the HUD and the dialogs on top. Fog is on the depth on the screen, so it starts at a
  fraction of the far plane's depth (0.975 by default, about 80 units) that the level's scripts change.
- **The UI** is the window manager's dialogs: data-described windows (`WMDD`) with buttons, lists and
  sliders, used for the menus, the pause screen and the in-game text pages.
- **Sound**: sound groups (`OSBD`) pick a sample among variations and play it in 3D, muffled by the
  walls between.
  The level's ambient spheres and the music are started by the scripts.
- **Movies**: the intro and the outro are Bink videos.

## Lessons for building such a game

- **Make behaviour data.** Templates for characters, weapons, particles and impacts let designers
  tune the game without code. The executable holds the systems, the data the game.
- **Use a fixed tick.** A fixed rate and seeded random numbers make the game deterministic. The game
  stays testable, and a replay is only the input.
- **Let animations drive movement.** The animation states and transitions are the moves list. Combat
  is a property of the data (attack frames, block frames, throws) rather than special cases.
- **Give the AI the player's interface.** An AI that presses keys plays by the same rules as the
  player, and its layers (knowledge, states, combat, movement) can be built and tuned one at a time.
- **Write missions as event-driven scripts.** Triggers, consoles and characters' deaths wake script
  threads; a few hundred commands are enough for the whole game's story.
- **Build one spatial structure and use it everywhere.** The octree serves the collisions, the
  line-of-sight tests and the visibility alike.

## Further reading

- [Status](./status): what the port plays today.
- The formats on this site: [instances](../formats/dat), [environment](../formats/akev),
  [animations](../formats/tram), [characters](../formats/oncc), [objects](../formats/objc),
  [weapons](../formats/onwc).
- The reverse-engineering notes:
  [architecture](https://github.com/emdzej/openoni/blob/main/docs/re/architecture.md),
  [characters](https://github.com/emdzej/openoni/blob/main/docs/re/character.md),
  [AI](https://github.com/emdzej/openoni/blob/main/docs/re/ai.md),
  [physics](https://github.com/emdzej/openoni/blob/main/docs/re/physics.md),
  [pathfinding](https://github.com/emdzej/openoni/blob/main/docs/re/pathfinding.md),
  [consoles](https://github.com/emdzej/openoni/blob/main/docs/re/console.md),
  [doors](https://github.com/emdzej/openoni/blob/main/docs/re/door.md),
  [impacts](https://github.com/emdzej/openoni/blob/main/docs/re/impact.md),
  [camera](https://github.com/emdzej/openoni/blob/main/docs/re/camera.md),
  [lighting](https://github.com/emdzej/openoni/blob/main/docs/re/lighting.md),
  [visibility](https://github.com/emdzej/openoni/blob/main/docs/re/visibility.md),
  [window manager](https://github.com/emdzej/openoni/blob/main/docs/re/wm.md).
