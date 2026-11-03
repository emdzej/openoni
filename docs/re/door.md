# Doors (OBJ, "Door")

The door object type, `OBJrDoor_Initialize` 0x4c31a0 (the type's methods: the read 0x4c3890, the OSD
0x4c3b30, the update 0x4c44f0, the level's start 0x4c48a0). The port: `src/door.c`, test
`tests/door_test.c`. The data: [objc.md](../formats/objc.md), DOOR.

## The object

D is the door object, S = D+0x2c its OSD (the run-time copy of the data, `OBJiDoor_SetOSD` 0x4c3b30).

| Offset | What |
|---|---|
| D+0x04 | The object's id (OBJC) |
| D+0x0c | Position; D+0x18 the rotation (degrees) |
| S+0x00 | Door id (u16) |
| S+0x02 | Flags (u16, objc.md) |
| S+0x04 | Key (u16) |
| S+0x06 | Open time: 180 ticks, set by SetOSD |
| S+0x08 | Activation distance squared (float) |
| S+0x0c | State: 1 open, 0 closed (u16) |
| S+0x0e | Stuck counter (u16) |
| S+0x10 | Countdown while open (u16) |
| S+0x12 | Delay: the update does nothing while it counts down (u16) |
| S+0x60 | The class (`DOOR` instance); its +8 is the animation (`OBAN`) |
| S+0x64 | The door frame (`AKDA` entry) |
| S+0x68, +0x6c | The panels' physics contexts while the door is open or moving |
| S+0x70, +0x74 | The panels' `OBOA` elements: element +0x14 = the door id, and the id \| 0x1000 |
| S+0xc0 | Centre (3 floats) |
| S+0xe4 | The gunk: the `ONOA` entry (ONLV +0x6c) of (object id & 0xffffff) \| 0x3000000 |
| S+0xe8 | The gunk's shade |
| S+0x164 | Without panels: an animation of its own (oban, frame +0x16a, step +0x16c, stop +0x16e) |

## The level's start

`OBJrDoor_LevelBegin` 0x4c48a0:

1. With flag 0x04, the `AKDA` entry (0x34 bytes: +0 the quad, +0x1c the centre) whose centre equals the
   door's gets 0x2000 on its quad's flags: not drawn.
2. The panels: the first elements of the level's `OBOA` with +0x14 equal to the id, and to id | 0x1000.
3. The gunk (`ONrLevel_FindObjectGunk` 0x506fc0: `ONOA` is (id, `IDXA` of quads) pairs). Its shade is
   the mean (rounded) of its corners brighter than 0x3f in a channel (0x507080). Every corner gets it
   (0x5071e0, unless the byte 0x5ec608 is set).
4. Flag 0x40; 0x02 from 0x01; state 0, delay 0; the gunk shown; no panels.

## Open and close

`OBJrDoor_Open` 0x4c26c0 (ECX = the door, EDX = the character), only with a class with an animation and
the door not open:

- Not animating: each panel is made from its element (its flags set to 0x1200, the setup 0x439910, a
  slot of the level's array 0x439470). The physics flags get 0x82, the owner the door, frame 0. The
  first panel's animation context gets 0x20 with door flag 0x100. The second gets 0x40, its +0x144 is
  the first's context, and it gets 0x20 without door flag 0x100.
- Animating (closing): the frame becomes the animation's frame count (+0x72) minus the frame. If that is
  the open frame (+0x74), nothing more plays.
- State 1, countdown 0, then (unless done) flag 0x20. Each panel plays the class's animation from its
  frame to the open frame (`OBrAnim_SetRange` 0x438b90) and is shown (0x438d20). Without panels the stop
  (S+0x16e) is the open frame.
- The gunk is hidden. Then the door's events run (0x4f3cc0), the open sound plays (0x512480), the AI
  hears the class's sound (0x49a8a0), and the pathfinding is told (`PHrNotifyDoorStateChange` 0x50ed90).

`OBJrDoor_Close` 0x4c2590 does the same the other way: state 0. Animating, the frame becomes
2 × open frame − frame, and nothing plays if that is the frame count. Otherwise each panel plays to the
frame count. The close sound, the AI's sound and the pathfinding follow. It doesn't show the gunk: the
update does that when the panels are gone.

The animation: frames 0 to +0x74 open the door, the rest close it. The panels' matrix
(`PHrPhysics_MaintainMatrix` 0x43b410, type 3) is the frame's (with context flag 0x40, of the first
panel's animation), under the element's placement (`OBOA` +0x40) when the animation has flag 0x10. With
context flag 0x20 it is then multiplied by half turns about y (0x434a80) and z (0x434b00).

## The side

`OBJiDoor_PointOnSide` 0x4c2370: R = Rz·Ry·Rx of the door's degrees × π/180. The plane goes through the
position plus R·(0,0,0), R·(1,0,0) and R·(0,1,0) (`MUrVector_PlaneFromPoints` 0x433ca0: n =
(a − b) × (c − a), normalised). The result is 1 if n·p + d < −1/1024 (`MUrPoint_PointBehindPlane`
0x432f10), else 0. The update also tests for −1, which never comes.

## The update

`OBJrDoor_Update` 0x4c44f0, from the objects' update (0x507880) after the characters:

1. **Panels:** the frame is the first panel's. If its context's blocked counter (+0x140) is 0, the stuck
   counter is 0. Otherwise it counts up. At 0x1a4 with the door open: flag 0x2000. At 90: delay 180. Then
   an open door closes. A closed one opens again for the character the panel ran into (+0x30, a physics
   context with flag 2 whose callbacks' type is 1), if that character may pass (below).
   **No panels:** nothing without a class or before the start. Animating, the frame steps by the step,
   kept below the frame count and at the stop.
2. **Animating:**
   - Open: below the open frame, return. Clear 0x2000. A countdown of 0 becomes the open time; otherwise
     it counts down, and at 0 the animating flag goes.
   - Closed: below the frame count − 1, return. Clear 0x2000, delete the panels, show the gunk, clear the
     animating flag.
3. **Delay:** while the delay counts down, return.
4. **Jammed** (0x800): return.
5. **No look:** a manual door that's closed, and (without the cheat byte 0x5ec5f5) a locked door that
   isn't one way and has no key, skip to 7.
6. **The living characters** (ONrGameState +0x1672f8): the first with distance² to the door's position
   below S+8 that may pass opens a closed door (and the update ends). An open door stays open.
   May pass: not jammed; and not locked, or one way with the character on the right side (side 1, or
   0 with flag 0x400), or a key it holds (`WPrInventory_HasKey` 0x424370 through 0x51cd00).
7. Not animating and open: close.

## Commands

| Command | Handler | Work |
|---|---|---|
| `door_open` id | 0x4b6880 | `OBJrDoor_Open_ID` 0x4c1ee0: the first door of the id (0x4d01a0) opens |
| `door_close` id | 0x4b68a0 | `OBJrDoor_Close_ID` 0x4c1f20 |
| `door_lock` id | 0x4b6860 | `OBJrDoor_Lock_ID` 0x4c1d80: every door of the id (0xffff all) gets 0x02, and 0x4fdac0 is told |
| `door_unlock` id | 0x4b6870 | `OBJrDoor_Unlock_ID` 0x4c1e30 |
| `door_jam` id | 0x4b6820 | `OBJrDoor_Jam` 0x4c29d0 with 1: flag 0x800 |
| `door_unjam` id | 0x4b6840 | the same with 0 |

## The port

`src/door.c` has the level's start, open, close, the side, the update and the six commands. The panels
are drawn from their elements' geometry under the matrix above. They step with the physics update
(before the characters); the doors update after them.

Not yet:

- The panels' physics: characters walk through a moving panel, so the stuck logic never starts.
- Keys, the door's events, the AI's sound, the pathfinding notice.
- The sounds' place: they play as plain impulses.
- The action key for manual doors (`OBJrDoor_TryDoorAction` 0x4c2a50).
- `door_is_locked` and the lock lights.
