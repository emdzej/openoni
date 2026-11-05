# Level objects (OBJC)

A level's objects are in its `.raw`, one `OBJC` chunk per object type. Each chunk is referenced by a
`BINA` instance ([instance files](./dat)) named `BINACJBO` and the type: `Character`, `Console`, `Door`,
`Flag`, `Furniture`, `Neutral`, `Particle`, `Patrol Path`, `PowerUp`, `Sound`, `Trigger Volume`,
`Trigger`, `Weapon`. They are what the level editor placed: the characters with their start positions,
doors, triggers, flags (the points scripts teleport to), and so on.

OpenOni reads them with `src/objc.c`. Worked out from the CD's data. The exe's readers are the `OBJ`
module's (step 7); this page grows with them.

## BINA

| Offset | What |
|---|---|
| 0x00 | Size of the data |
| 0x04 | Its offset in the `.raw` |

## The chunk

All values are little-endian.

| Offset | What |
|---|---|
| 0x00 | `CJBO` ("OBJC" reversed) |
| 0x04 | Size of what follows |
| 0x08 | Version: 39 |
| 0x0c | The objects |

Each object:

| Offset | What |
|---|---|
| 0x00 | Size of what follows |
| 0x04 | Type, four characters reversed (`RAHC` for `CHAR`) |
| 0x08 | Id |
| 0x0c | Flags |
| 0x10 | Position (3 floats) |
| 0x1c | Rotation (3 floats, degrees) |
| 0x28 | The type's own data |

## CHAR

| Offset (in the type's data) | What |
|---|---|
| 0x00 | Flags: 0x01 the player, 0x04 not there at the start (spawned by a script), 0x200 carries the LSI (C+0x1b4) |
| 0x04 | Character class (`ONCC` name, without the tag), 64 bytes |
| 0x44 | Name (what scripts call it), 32 bytes |
| 0x64 | Weapon, 64 bytes |
| 0xa4 | Eight scripts of 32 bytes: spawn, die, combat, alarm, hurt, defeated, out of ammo, no path (die and defeated confirmed) |
| 0x1a4 | Extra health, added to the class's |
| 0x1a8 | Job (3 patrol) |
| 0x1ac | The job's patrol path id (u16) |
| 0x1ae | Combat settings id (u16): a `Combat` object's |
| 0x1b0 | Melee profile id (u16) |
| 0x1b2 | Neutral settings id (u16) |
| 0x1b4 | Six pairs of u16, the inventory: carried, then dropped at death, of the ballistic clips, energy cells, hypos, shield, invisibility; the sixth pair unused (D+0x1d4..D+0x1eb). `ONrGameState_NewCharacter` 0x4dac50 copies them to C+0x1a2/0x1a6/0x1a4 (carried clips, cells, hypos), C+0x1a8/0x1ac/0x1aa (dropped), C+0x1b8 the shield (1 is 100), C+0x1b2 whether one is dropped, C+0x1ba the invisibility (C+0x1bc 600 with one), C+0x1b3 whether one is dropped. The death hook (0x4eb550) of an AI drops them (`WPrPowerup_Drop` 0x51bd80): the clips, cells, hypos one by one, a shield of 100, an invisibility of 1800, then its own shield, invisibility and the LSI |
| 0x1cc | Team: an index into the exe's eight names (0x54b38c: Konoko, TCTF, Syndicate, Neutral, SecurityGuard, RogueKonoko, Switzerland, SyndicateAccessory) |
| 0x1d0 | The weapon's ammo, a percentage of its shots (u32; `ONrGameState_NewCharacter` 0x4dac50 passes it to 0x519b30) |
| 0x1d4 | Alert levels, four u32: initial, minimal, job start, investigate (C+0xf94..C+0xfa0) |
| 0x1e4 | Alarm groups (C+0xfd8) |

The reader is 0x4bf290 (`OBJiCharacter_Read`, the Character type's read method). It keeps the file's
order up to the scripts; from +0x1a4 on, the object in memory (D, its data at D+0x2c) is laid out
otherwise (the extra health at D+0x1d0, the job at D+0x1f4, the ids at D+0x1f8..D+0x1fe, the team at
D+0x1ec, the alert levels at D+0x200..D+0x210). `AI2rInitializeCharacter` 0x48a6b0 copies the scripts to
the character in another order: spawn C+0x48, die C+0x88, combat C+0x68, alarm C+0xa8, hurt C+0xc8,
defeated C+0xe8, out of ammo C+0x108, no path C+0x128. The death hook (0x4eb550) runs C+0xe8 as
"defeated" (unless C+0x44 bit 0x20 says it ran) and C+0x88 as "death" (setting bit 0x04), deferred
with the character's name and run at once (0x4f2120). The team is confirmed by `chr_changeteam`
(0x4b8b10), which only sets C+0x12 to the name's index. The flags are confirmed in
`ONrGameState_NewCharacter` 0x4dac50 (docs/re/character.md): flag 0x01 gets the player's controller.

Every level has exactly one character with flag 0x01. On level 1 it is `char_0` (`konoko_generic`) at
(-1101.0, -340.0, -126.3), turned 172°, in the training room. The `konoko` at (-543.2, 159.0, -630.1)
has flag 0x04: the cutscene's.

In memory (the object's OSD, the type's data at +0x2c) the game keeps flag 0x20 on a character it has
spawned: `ai2_spawn` (the test in 0x48a480) spawns a character again only with `force` or with flag 0x10.
The reader clears 0x20 (confirmed, 0x4bf290), so a file that has it set (level 1's `A_t48`: 0x24) still
spawns once; the level's start (0x48a360) sets it on the characters it makes.

Scripts pick a character by name or by script id (C+0x40). `AI2rInitializeCharacter` 0x48a6b0 gives
the player script id 0 and every other character from the level -1, so a number only ever finds the
player or a character made with `chr_create` (its id comes from the `.txt` file). The object id
(0x08) is not the script id.

## PWRU, WEAP

A `PowerUp` object's data is its type's tag, four bytes read as a big-endian number: `AMMB` ammo, `AMME`
an energy cell, `HYPO`, `SHLD` a shield, `INVI` invisibility, `ALSI` the LSI (the exe's table at
0x54bed0: the type, the tag and the name, 12 bytes each, 6 of them; `OBJiPowerUp_Read` 0x4c9f40, a tag
not in it leaves the type 0). A `Weapon` object's is the weapon class's name (`ONWC`, 32 bytes).

At the level's start (0x51b0f0) the game makes them: each weapon (0x51ad90) 2 above the object's
place, facing its first rotation angle (+0x18 of the object, the x one), and each powerup (0x51b080)
placed with its geometry's bottom on the object's place (0x51c270), facing the y angle, with the type's
default amount (1, a shield 100, invisibility 1800). Level 1 has eight: the LSI the mission needs at
(424, 50, -639), ammo, cells and a hypo.

## NEUT

A `Neutral` object (version 39) is a neutral character's behaviour, read as it is stored
(`OBJiNeutral_Read`; `ONrLevel_Neutral_ID_To_Neutral` 0x506c00 copies its 0x26c bytes out). Confirmed
in the Windows exe's use of it (src/neutral.c) and against level 1's 39 of them:

| Offset | What |
|---|---|
| 0x00 | name (32 bytes) |
| 0x20 | id (u16): a CHAR's +0x1b2 picks it, or `ai2_neutralbehavior` by the name |
| 0x22 | count of lines (u16, 10 at most) |
| 0x24 | flags (u32): 1 one conversation only, 2 one give only, 4 it can't be broken off |
| 0x28 | trigger range: it calls the player within it (100 on level 1) |
| 0x2c | talk range: it talks within it (40) |
| 0x30 | follow range: how far from its place it follows the player (200) |
| 0x34 | enemy range: a fight this near puts it off or ends the talk (70) |
| 0x38 | the call's sound (an ambient, 32 bytes) |
| 0x58 | the abort's sound |
| 0x78 | the enemy's sound |
| 0x98 | the end script (a BSL function, run when every line was said) |
| 0xb8 | the weapon given (an ONWC name, `none`) |
| 0xd8 | the gifts: ammo clips, energy cells, hypos (a byte each), then flags: 1 a shield, 2 invisibility, 4 the LSI |
| 0xdc | the lines, 0x28 bytes each |

A line: flags (u16: 1 the player says it, 2 the give, 4 the speaker's animation is waited for, 8 the
listener's), a u16 not read, the speaker's animation type (u16, 0 none) and the listener's, the sound
(32 bytes). Level 1's "Talk_About_Truck" (18) is one line, flag 4 on the neutral, with the end script
`gotoconsole` (what lets console 9 open the warehouse); its "Give ..." ones are a line and the give.

## CONS

The console type's data, read by `OBJiConsole_Read` 0x4c1710. The port: `src/console.c`; how consoles
work: docs/re/console.md.

| Offset | Size | What |
|---|---|---|
| 0x00 | 63 | The class: a `CONS` instance's name |
| 0x3f | 2 | Id (`console_*` commands, events) |
| 0x41 | 2 | Flags |
| 0x43 | 63 | The screen's texture while inactive (`TXMP` name) |
| 0x82 | 63 | While active |
| 0xc1 | 63 | Once used |
| 0x100 | | Event list (below) |

Flags: 0x02 used and 0x04 active (run time), 0x08 active at the start, 0x20 used with a punch (animation
type 0xdd instead of 0xc4).

The `CONS` instance: +0x04 the action point and +0x10 its direction (3 floats each, in the console's
space), +0x1c the geometry (`OFGA`), +0x20 the screen (`M3GM`). The console's body is in the environment
as gunk (object id 0x0f000000 | id); the object itself draws only the screen.

### Event lists

Doors (at 0xd3) and consoles (at 0x100) end with an event list (`ONrEventList_Read` 0x4f39e0): a u16
count, then each event a u16 type and its argument.

| Type | Event | Argument |
|---|---|---|
| 1 | Execute script | 32-byte function name |
| 2, 3 | Activate, deactivate turret | u16 id |
| 4, 5 | Activate, deactivate console | u16 id |
| 6, 7 | Activate, deactivate alarm | u16 id |
| 8, 9 | Activate, deactivate trigger | u16 id |
| 10, 11 | Lock, unlock door | u16 id |

## DOOR

The door type's data, read by 0x4c3890 (OBJC version 39; `OBJrDoor_Initialize` 0x4c31a0 registers the
type). The port: `src/door.c`; how a door works: docs/re/door.md.

| Offset | Size | What |
|---|---|---|
| 0x00 | 63 | The class: a `DOOR` instance's name |
| 0x3f | 2 | Id (what `door_*` commands and the panels' elements give) |
| 0x41 | 2 | Key, 0 none |
| 0x43 | 2 | Flags, kept & 0x1795 |
| 0x45 | 12 | Centre (3 floats): the door frame's in the environment's `AKDA` |
| 0x51 | 4 | Activation distance, squared (float; 900 on level 1) |
| 0x55 | 63 | Texture 1 (the editor's) |
| 0x94 | 63 | Texture 2 |
| 0xd3 | | Event list: a u16 count, then the events |

Flags (from `OBJrDoor_Update` 0x4c44f0, `OBJrDoor_Open` 0x4c26c0 and `OBJrDoor_LevelBegin` 0x4c48a0):

| Bit | Meaning |
|---|---|
| 0x0001 | Locked at the start: the level's start sets 0x0002 from it |
| 0x0002 | Locked (run time; `door_lock`, `door_unlock`) |
| 0x0004 | The door frame's quad isn't drawn (0x2000 on its flags) |
| 0x0010 | Manual: a closed one doesn't open for a character near it |
| 0x0020 | Animating (run time; also while it waits open) |
| 0x0040 | Started (run time) |
| 0x0100 | The first panel turns half about y and z (else the second does) |
| 0x0200 | One way: locked, it still opens for the characters on one side |
| 0x0400 | The other side |
| 0x0800 | Jammed (run time; `door_jam`) |
| 0x2000 | Stuck (run time) |

The `DOOR` instance (128 bytes): +0x00 the geometry (M3GA), +0x08 the animation (`OBAN`), +0x14 the
AI's sound type (-1 none), +0x18 its radius, +0x1c and +0x3c the open and close sounds' names (32 each),
+0x5c and +0x60 the sounds, found at run time.

## FLAG

| Offset (in the type's data) | What |
|---|---|
| 0x00 | Colour, u32 |
| 0x04 | Prefix, u16 (shown as a letter before the id, `OBJiFlag_OSDGetName` 0x4c51c0) |
| 0x06 | Id, u16: what scripts give (`chr_teleport`, `ai2_movetoflag`) |
| 0x08 | Note, 128 bytes |

The reader is 0x4c55b0; the search by id is 0x4c56c0, through `ONrLevel_Flag_ID_To_Flag` 0x506c80,
which takes the flag's position from the object (0x10) and its facing from the rotation (0x1c).
`chr_teleport` moves the character to the position and leaves its facing.

## PART

The `Particle` type: an environment particle placed as an object, read by *OBJiParticle_Read*
0x4c87e0 (from OBJC version 0x20).

| Offset (in the type's data) | What |
|---|---|
| 0x00 | The particle class's name, 64 bytes |
| 0x40 | The tag scripts name it by (`particle <tag> ...`), 48 bytes |
| 0x70 | Flags, u16: only 2 (not created at the start) is kept |
| 0x72, 0x76 | The decal's width and height, floats |

*OBJiParticle_New* 0x4c8760 makes from it a record like the `ENVP`'s (0xd0 bytes: the class, the tag,
the matrix from the object's rotation through 0x437130, the position, the decal's size, the flags) and
lists it with `EPrNew` 0x454a60, so the objects are environment particles like the `ENVP`'s; the port
puts them after the `ENVP`'s (the order is inferred). Level 11's window glass (`WindowCharge`, the blast
that breaks it in the window cutscene) is one.

## PATR

The patrol path type (`CJBOPatrol Path`, the tag stored as `RTAP`), read by 0x4c9440 (registered by
`OBJrPatrolPath_Initialize` 0x4c8e60). The port: `src/patrol.c`.

| Offset | Size | What |
|---|---|---|
| 0x00 | 32 | Name (`ai2_dopath` finds the path by it, `AI2rPatrol_FindByName` 0x4b04c0) |
| 0x20 | 4 | Waypoint count |
| 0x24 | 2 | Id: what a character's job path (CHAR +0x1ac) gives |
| 0x26 | 2 | Unknown |
| 0x28 | | The waypoints: each a type (u32) and its fields |

The fields by type, and what `AI2iPatrol_DoWaypoint` 0x4b0650 does with them:

| Type | Fields | What |
|---|---|---|
| 0x00 | flag u16 | Move to the flag |
| 0x01 | | Stop: the patrol ends |
| 0x02 | frames u32 | Pause |
| 0x03 | flag u16 | Look at the flag |
| 0x04 | point 3 floats | Look at the point |
| 0x05 | flag u16 | Move to the flag and face its way |
| 0x06 | | Loop to the first waypoint |
| 0x07 | mode u32 | The movement mode (ai.h's modes) |
| 0x08 | point 3 floats | Move to the point |
| 0x09 | u32 | Lock the facing |
| 0x0a | flag u16, distance float | Move through the flag (on within the distance) |
| 0x0b | point 3 floats, distance float | Move through the point |
| 0x0c | | Stop looking |
| 0x0d | | Free the facing |
| 0x0e | flag u16, ticks u32 | Glance at the flag for the ticks |
| 0x0f | flag u16, distance float | Move near the flag |
| 0x10 | index u32 | Loop to the waypoint (at most the last); one that names itself loops to the next (the first after the last) |
| 0x11 | frames u16, u32 | Scan (inferred) |
| 0x12 | | Stop scanning |
| 0x13 | frames u16, flag u16, float | Move to the flag, face its way, wait the frames (0: for good). On disk the frames come first; in memory the flag is at +4, the frames at +6 |
| 0x14, 0x15 | u16 | Run `patrolscript%04d` with the character's name; 0x15 keeps the thread (+0x5ac) and the patrol waits until it ends |
| 0x16 | byte | Ignore the player |
| 0x17 | u16, u16, u32 | Unknown |

A CHAR's job (+0x1a8) 3 is a patrol: `AI2rInitializeCharacter` 0x48a6b0 starts the path whose id is
the low u16 of +0x1ac. Level 1 has 63 paths, 207 waypoints; most are 0x13s ending in a 0x10 or a 6.

`AI2rPatrol_Update` 0x4afc90 goes on through the waypoints done at once in the same tick, until one
takes time or the way comes back to the waypoint it started from ("patrol %s is looping endlessly"):
then on the next tick. Past the last waypoint, 0x4b0420 ends the patrol (the port: idle; going back to
the character's job is not ported). Level 3's Floor1_Stk_1 ends in a 0x10 naming itself. The port's
waypoints done at once still differ from the original's, so a 0x10 back to a later waypoint could go
round without reaching the start: the port stops after the path's count of waypoints in a tick (a
stand-in).

The glance (ported: patrol.c, the count in ai.c, the look in combat.c): `AI2rPatrol_Update` starts it when no glance or scan is under way
(C+0x1598 and the block's +0x558 clear) with 0x4a8150: C+0x15a0 1 (at a point), C+0x15a4 the flag's
place, C+0x1598 the ticks, C+0x1594 a delay (0 here). Unless the character has arrived (C+0x112c) with
its move's facing to keep (0x4aefb0), the patrol goes on at once and the glance plays on the way; else
the move stops (0x4ae660) and the patrol waits for the glance's end. `AI2rMovement_Update` 0x4a8550
counts the delay down first, then the ticks, a tick only once the aim is done (A+0x1ab8, 0x498e60);
while it lasts the movement's look (0x4aa250) takes the glance's kind and place instead of its own
(C+0x1584, C+0x1588).

## TRGV

The trigger volume type's data, read by 0x4cced0 (registered by `OBJrTriggerVolume_Initialize` 0x4cc960).
The port: `src/trigger.c`.

| Offset | Size | What |
|---|---|---|
| 0x00 | 63 | Name (`trigvolume_*` commands find volumes by it) |
| 0x3f | 32 | Entry script: a BSL function, called with the character's name |
| 0x5f | 32 | Inside script, every tick the character is in |
| 0x7f | 32 | Exit script |
| 0x9f | 4 | Team mask: bit *n* lets characters of team *n* in |
| 0xa3 | 12 | Size x, y, z (floats) |
| 0xaf | 4 | Id (`trigvolume_count` takes it; characters keep it while they're in) |
| 0xb3 | 4 | Parent id: its scripts run as well (OBJC version 0x16 and later; 0 before) |
| 0xb7 | 128 | Note, for the editor |
| 0x137 | 4 | Flags |

Flags (confirmed in `trigvolume_enable`'s callback 0x4b8000 and `ONrGameState_UpdateTrigger` 0x4e0cf0):

| Bit | Meaning |
|---|---|
| 0x01 | Entry script once: firing sets 0x08 |
| 0x02 | Inside script once: sets 0x10 |
| 0x04 | Exit script once: sets 0x20 |
| 0x08, 0x10, 0x20 | Entry, inside, exit script off |
| 0x40 | Volume off |
| 0x80 | Player only |

The stored flags and scripts are kept apart from the current ones; `trigvolume_reset` (0x4cc760) copies
them back.

The box (0x4cc7c0): the object's rotation, degrees × π/180 (0x529550) through the Euler conversion
0x437130 with order 0 (Shoemake's static XYZ, R = Rz·Ry·Rx, stored by columns), then the position. The
box goes from (0, 0, 0) to the size in that space. The builder keeps its eight corners, the six face
planes and a bounding sphere.

Each tick `ONrGameState_UpdateCharacters` 0x4dccf0 runs `ONrGameState_UpdateTrigger` on every volume,
before the characters move:

- Skipped if the volume is off, or player only and the character isn't the player.
- `OBJrTriggerVolume_IntersectsCharacter` 0x4cc680 decides. The character's team must be in the mask.
  Its bounding sphere must meet the volume's. Then a segment of the character (two points of the active
  character, +0x140 and +0x14c) must cross a face (0x43f750), or its first point must be inside all six
  planes (0x43f910).
- A character holds the ids of up to four volumes (+0x168 in the active character).
  - Entering takes the first free slot and fires the entry script. With no free slot, nothing is
    recorded.
  - While inside, the inside script fires every tick.
  - Leaving frees the slot (the later ones move down) and fires the exit script.
- The parent's script of the same kind fires with each.
- Scripts are queued (128 at most, 0x4f2070): the function, the character's name, the cause. At the end
  of the volume's update they run through `SLrScript_ExecuteOnce`, unless a script being run caused the
  update.
- Turning a whole volume off (`trigvolume_enable name 0`) clears its id from every character's slots
  (0x4f28a0), without moving the others down.

Inferred, not yet confirmed: the character's segment. The port uses a vertical segment from the feet
up 18 units until the active character's points are ported.

## TRIG

The laser trigger type's data ("Trigger", 0xdc bytes an object), read by `OBJiTrigger_Read` 0x4cbf30
and set up by `OBJiTrigger_SetOSD` 0x4cc1f0 (registered by `OBJrTrigger_Initialize` 0x4cb420). The
port: `src/laser.c`.

| Offset | Size | What |
|---|---|---|
| 0x00 | 63 | Class: a `TRIG` instance (the level's first when not found) |
| 0x3f | 2 | Id (`trig_*` commands and event types 8 and 9 take it) |
| 0x41 | 2 | Flags: only 0x38 is kept. 0x08 active at the start, 0x10 the emitters run backwards, 0x20 ping-pong |
| 0x43 | 4 | The beams' colour, ARGB |
| 0x47 | 4 | Start: where along the animation the emitters begin, clamped to 0..1 |
| 0x4b | 4 | Speed: frames a tick, clamped to 0..100 |
| 0x4f | 2 | Emitters, clamped to 1..32 |
| 0x51 | 2 | Ticks on (D+0x8c) |
| 0x53 | 2 | Ticks off (D+0x8a) |
| 0x55 | | Event list (see CONS) |

The class (`TRIG`, instance data): +0x10 the base geometry (`M3GM`, its bounding sphere the object's),
+0x1c the emitter (`TRGE`), +0x20 the emitters' animation (`OBAN`), +0x24 the active sound (32, an
ambient), +0x44 the hit sound (32, an impulse). `TRGE`: +0x00 the beam's start and +0x0c its direction
in the emitter's space, +0x18 the emitter's geometry.

The object at run time (D): +0x2c id, +0x2e flags (0x02 set off, 0x04 active, 0x40 hidden, 0x80 the
active sound playing), +0x74 the class, +0x78 colour, +0x7c start, +0x80 speed, +0x84 the speed now
(`trig_speed` sets it to its factor times +0x80), +0x88 emitters, +0x9c the emitters (0x4c bytes each:
the frame as a float, the animation's context, the matrix at +0x18), +0xa0 the countdown, +0xa2 on,
+0xc0 the rotation, +0x104 the beams' hit points by the animation's frame (12 bytes each).

- `OBJrTrigger_Reset` 0x4cae40: on, the countdown the on time (when either time is set); the emitters
  spread evenly: emitter *k* at fmod(*k* × frames / emitters + (frames − 1) × start, frames − 1), or
  all at frames − 1 when the start is exactly 1. Not set off; active as 0x08 says. Then the update and,
  while active, one more step. `SetOSD` steps once more after it.
- The step (0x4cafc0): each emitter's frame moves by its direction times the speed now. Past the end
  it goes back to 0 (ping-pong: to the last frame, turned round); below 0 to the last frame (ping-pong:
  0, turned round). Then the animation's matrix at the frame truncated.
- The update (0x4cb100, every tick): the step while active, then the countdown. At 0 the beams go on
  for the on time when off, or off for the off time when on.
- The draw (`OBJrTrigger_OnTrigger` 0x4cb6f0, every drawn frame, every trigger: the type has no
  visibility test) also tests the beams. Unless hidden, for each emitter it draws the emitter's
  geometry under T(position)·R·the emitter's matrix. Active and on, it casts the beam: the start and
  the start plus the unit direction through that matrix, the line between them set to 500 long. The
  level hit (`AKrCollision_Point` 0x441c80, skipping 0x44800) is found once for each animation frame
  and kept; with none the end is the origin. If the player (G+0xac, when G+0xb0) is hit nearer
  (`ONrCharacter_Collide_Ray` 0x4f1630), the trigger is set off: flag 0x02, the hit sound, the event
  list. That happens once, until an emitter misses, which clears 0x02. With more than one emitter one
  held crossing can set it off again on each frame. The beam drawn (`FXrDrawLaser` 0x4f4720, a
  contrail 0.2 wide) stops 2 short of its end.
