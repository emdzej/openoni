# Consoles and the action key

The console type (registered with `ONrMechanics_Register` in 0x4c0fe0's group, 0x19c bytes an object),
the action markers, the event lists, and the action key's handler. The port: `src/console.c`,
`src/event.c`, `src/action.c`; test `tests/console_test.c`. The data: [objc.md](../formats/objc.md),
CONS.

## The console

D is the console object.

| Offset | What |
|---|---|
| D+0x0c, +0x18 | Position, rotation (degrees) |
| D+0x2c | Id (u16) |
| D+0x2e | Flags (u16): 0x02 used, 0x04 active, 0x08 active at the start, 0x20 punch |
| D+0x74 | The class (`CONS`) |
| D+0x138 | The event list |
| D+0x148 | Its action marker |
| D+0x14c, 0x150, 0x154 | The screen's textures: active, inactive, used (0x41f950 by name; a default 0x41f920 if missing) |
| D+0x188 | The rotation matrix |

`OBJiConsole_LevelBegin` 0x4c1c20 makes the marker (`ONrLevel_ActionMarker_New` 0x506ee0: ONLV +0x74
the count, 0x14 bytes each from +0x78: the object, the place, the facing). The place is the class's action
point (+4) under the console's matrix. The facing is atan2(−x, −z) of its direction (+0x10) rotated, plus
2π when negative. Then flag 0x02 goes, and 0x04 follows 0x08.

`console_activate` (0x4b62b0 → 0x4c09a0), `console_deactivate` (0x4b62d0 → 0x4c09f0) and
`console_reset` (0x4b6300 → 0x4c0a40) take an id, 0xffff for all. Deactivate and reset without one do all.
Reset is the level's start again: not used, active as 0x08 says.

The draw (0x4c0c80): only with a class with geometry. The class's screen (+0x20, an `M3GM`) is drawn
under T(position)·R with its texture (+0x1c) swapped for the state's: inactive without 0x04, active
without 0x02, else used.

`OBJrConsole_OnActivate` 0x4c0880: inactive, the "locked" sound; used, the "already used" sound;
otherwise the "activate" sound, flag 0x02, and the event list with the character (0x4f3cc0). The sounds
play at the middle of the marker and the character's C+0x160 (`SSrImpulse_Play_Simple` 0x480a10).

## Event lists

`ONrEventList_Execute` 0x4f3cc0 runs each event through the handler of its type, from the table at
0x552098 (0x14c bytes an entry: the type, the name, the handler at +0x144, the argument's kind at +0x148:
1 u16, 2 u32, 3 a 32-byte name).

- Execute script, 0x4f3d40: `ONrGameState_DeferScript` 0x4f2070 with the character's name (or "none") and
  the cause "eventlist".
- Consoles 0x4c09a0/0x4c09f0, turrets 0x4cd990/0x4cda80, the laser triggers 0x4cb200/0x4cb280, doors
  0x4c1d80/0x4c1e30.
- Alarm on, 0x49ac30, with the character; off, 0x4cae10.

## The action key

`HandleStartTauntOrAction` 0x4f80d0 (ECX the input, EDX the character, the active record on the stack),
called by `HandleHeartbeatInput` after `HandleStartJumping`:

1. A console action under way (C+0x1680): `ONrCharacter_ConsoleAction_Update`, and the input is taken.
2. Without the action key gone down this tick (input +0x24 bit 1: the high word of "down"): nothing.
3. The weapon in hand, if its class has flag 2 (+0xcc), blocks the console.
4. Unless the film plays (0x4e4100) or C+4 has 0x400000: the pelvis (0x4e34f0), the nearest marker
   (0x506f20); if the character is an AI or no overlay is in its "a4..ab" range (0x4edcc0), and it may
   use it (0x4f5840): `ONrCharacter_ConsoleAction_Begin` 0x4ee1b0, taken.
5. The door (`OBJrDoor_TryDoorAction` 0x4c2a50, door.md): one it may pass opens (0x4c2520), with the
   sound at 0x631140, taken. One it may not: taken after step 6, with the sound at 0x631144.
6. 0x48bb40 (a neutral character's talk).
7. The taunt, type 0x5e from a table of one move (the action key), for the player only when the game time
   is past GS+0x110.

### May the character use the marker (0x4f5840)

From the pelvis p to the marker m, with the marker's facing f:

- |p.y − m.y| ≤ 8 ("pelvis height").
- Ahead, −(sin f·dx + cos f·dz), in [−2, 10] ("distance").
- Sideways, |cos f·dx − sin f·dz| ≤ 8, or 16 for the wide test ("sideways").
- The facing (the player's 0x4e33a0, an AI's C+0x16c) within 1.2217 of f, or 1.8326 wide ("facing").
- Within 3 (squared 9, 0x52a5b0): yes. Farther: the way from the pelvis to the point 3 before the marker
  (the marker's place less 3 along its facing, 0x529808, at its height) mustn't be blocked: by the
  environment, an object or the character's swept spheres (`AI2rManeuver_CheckBlockage` 0x49ea60, "can't
  use console, blocked by environment"), nor by another character (`AI2rManeuver_CheckInterveningCharacters`
  0x49eb80: each character in use (GS+0x1670f0) but itself and those with C+4 0x40004, a sphere of the
  character's radius, 0x4dc100: 3 for an AI, 4 for the player, plus 3, swept against its body and parts,
  `ONrCharacter_Collide_SphereRay` 0x4f1b60; "blocked by character(s)").

### The console action (`ONrCharacter_ConsoleAction_Update` 0x4ee220)

- Not yet given a deadline (C+0x1684 = 0): the animation must be a standing or moving one (types 2, 3,
  6–0xb, 0xd–0xf, 0x18, 0xce–0xd3, 0xd8–0xda) without the 0x100 variant, else it aborts ("aborting
  console wait for state"). Standing (state 7) in types 6, 7, 8, 0xce, 0xcf, 0xd8, the deadline is the
  time + 180.
- Playing the console animation (0xc4 or 0xdd): at the frame min(TRAM+0x174, frames − 1) the console is
  used (OnActivate), the marker forgotten, and done.
- Within 2 of the target (the marker's x and z, the console's own y, D+0x10): facing and desired facing
  the marker's, the location, the previous one, C+0x160 (y + 0.5) and the physics all set to it, the
  BNV and pathfinding nodes looked up again. Then DoAnimation stand (type 6), then the console's (0xdd
  with flag 0x20, else 0xc4); failing that 0xc4 ("failed to find proper console animation").
- Before the deadline: the console walk (type 0xc5) is set unless it plays (TRrCollection_Lookup of the
  state and variant, 12 ticks of interpolation). The desired facing is the marker's; the character moves
  toward the target by at most 0.25 (the double at 0x52b328).
- After the deadline: aborted.

Aborted or done: C+0x1680 = 0, `WPrInventory_Weapon_Purge_Magic`, and for the player GS+0x1679ae = 0.
A hit ends it the same way: `ONrCharacter_NewAnimationHook` 0x4e97a0 clears it when the new animation's
type is a hit's (0x17, 0x4c–0x5c, 0x60–0x65, 0x72–0x77, 0x7e–0x84, 0x8d, 0xc6, 0xd4–0xd7). Without it, a
character knocked down at the marker would stay in the action (its input taken) for good: lying down, it
can neither stand nor play the console animation, and the deadline is only checked on the walk.

## The sprint

Step 4 of `ONrGameState_DoCharacterFrame` for the player (C+0x1e8 = 0): A+0x2190 is −1 while
sprinting. Then the variant gets 0x100, and forward not held ends it (A+0x2190 = 0). Otherwise 0x100 is
cleared. Forward gone down within 15 ticks of A+0x2190 starts the sprint; otherwise A+0x2190 becomes the
time. Konoko's sprint runs (`KONSPRrun_lt`, `_rt`) are runs (type 3) of variant 0x100.

## The port

Ported: the consoles' start, the commands, OnActivate with its sounds and events, the screen's draw;
the event lists (scripts, consoles, door locks); the action key's console and door; the console action;
the sprint; the blocked-way test past 3 (its blockage from the active record's pelvis).

Not yet:

- The pelvis is the location raised by the pelvis height (the active record's pelvis matrix isn't kept).
- The overlay range test (no overlays yet), weapons, the film, neutral talk.
- Turrets, alarms and laser triggers in events.
- Sound positions.
