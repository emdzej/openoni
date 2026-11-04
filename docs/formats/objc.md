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
| 0x00 | Flags: 0x01 the player, 0x04 not there at the start (spawned by a script) |
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
| 0x1b4 | Six pairs of u16 (the ammunition and the like, not used by the port) |
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

Scripts pick a character by name or by script id (C+0x40). `AI2rInitializeCharacter` 0x48a6b0 gives
the player script id 0 and every other character from the level -1, so a number only ever finds the
player or a character made with `chr_create` (its id comes from the `.txt` file). The object id
(0x08) is not the script id.

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
| 0x0e | u16, u32 | Glance (inferred) |
| 0x0f | flag u16, distance float | Move near the flag |
| 0x10 | index u32 | Loop to the waypoint |
| 0x11 | frames u16, u32 | Scan (inferred) |
| 0x12 | | Stop scanning |
| 0x13 | frames u16, flag u16, float | Move to the flag, face its way, wait the frames (0: for good). On disk the frames come first; in memory the flag is at +4, the frames at +6 |
| 0x14, 0x15 | u16 | Run `patrolscript%04d` with the character's name |
| 0x16 | byte | Ignore the player |
| 0x17 | u16, u16, u32 | Unknown |

A CHAR's job (+0x1a8) 3 is a patrol: `AI2rInitializeCharacter` 0x48a6b0 starts the path whose id is
the low u16 of +0x1ac. Level 1 has 63 paths, 207 waypoints; most are 0x13s ending in a 0x10 or a 6.

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
