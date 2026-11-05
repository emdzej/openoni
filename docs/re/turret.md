# Turrets

The level's turrets (`OT_Turret.c` in the original's paths, ported in `src/turret.c`). They are a
"Turret" mechanics type, registered by *OBJrTurret_Initialize* 0x4cdf70 with 0x3f8 bytes per turret, and
come from the level's `Turret` objects. They are in seven levels: manplant, tctf, state, dream_lab,
neuro, tctf_ii and compound.

## The object (OBJC `Turret`, 0x6b bytes)

| Offset | What |
|---|---|
| 0x00 | The TURR class's name (63 bytes) |
| 0x3f | The id (u16): the event lists' and `turret_deactivate`'s |
| 0x41 | Flags (u16; only bit 2 is kept) |
| 0x43 | 36 bytes of zeros |
| 0x67 | The target teams (u32, a bit per team: 1 is Konoko's) |

The reader 0x4ce9e0 builds the in-memory data and calls *OBJiTurret_SetOSD* 0x4ceb80. That function
starts the turret active when the in-memory data's word at +4 is 1, but the reader never writes that
word. In practice every turret starts off, and an event list or a script turns it on (*confirmed*).
SetOSD finds the class by name, falling back to the first TURR ("failed to find turret class").

## The class (TURR, 1632 bytes; 24 in level0)

| Offset | What |
|---|---|
| 0x4e | The shooters' count (u16) |
| 0x58, 0x64, 0x6c | The base's, the head's and the barrel's geometry (M3GM); the draw shows the head and the barrel |
| 0x74 | The head's pivot on the base (3 floats) |
| 0x80 | The barrel's pivot on the head |
| 0x8c | The shooters, 0x4c bytes each |
| 0x54c | The AI firing mode, as a weapon's (ONWC +0xd0) |
| 0x5cc | The AI block, as a class's (ONCC +0x138); +0x5e8, its shooting skills |
| 0x600 | The ticks it stays active (u32, 0 forever; the sap wall turret: 900) |
| 0x604, 0x608 | The barrel's pitch limits (radians, signed; -0.785, 0.349) |
| 0x60c, 0x610 | The head's yaw limits (-1.222, 1.222) |
| 0x614, 0x618 | The pitch's and the yaw's turn per tick (0.015) |
| 0x61c | The active sound's name (an ambient: "turret_active"); 0x63c, its pointer (*OBJrTurret_UpdateSoundPointers* 0x4cf880) |

A shooter: +0x00 the particle class's name (16), +0x14 the ticks before the next shot (u16), +0x16 its
group (u16; 0xffff takes every event), +0x18 a 3x4 matrix on the barrel (the orientation, then the place
at +0x3c).

## The turret (0x3f8 bytes)

+0x0c the place, +0x18 the rotation (degrees), +0x2c the id, +0x2e the flags (1 particles made, 4 firing,
8 the active sound, 0x10 drawn this frame, 0x20 shown), +0x30 the state (1 active), +0x74 the class,
+0x78 the target teams, +0x80/+0x84 the barrel's pitch and the head's yaw, +0x88/+0x8c the yaw and pitch
wanted, +0x90 the targeting's trigger, +0x94 the base's matrix, +0xc4/+0xf4 the head's and barrel's
(local), +0x124 the barrel's in the world, +0x154 the line of sight, +0x156 the shot's cooldown, +0x158
the retarget timer, +0x15c the active ticks, +0x160 the ticks unseen, +0x168 the shooters' particles,
+0x268 the targeting (its +8, at +0x270, is the target), +0x3a0 the active sound.

## What it does

- **Matrices** (in the draw 0x4ce300). The head is turned about y by the yaw (0x434d70) at its pivot.
  The barrel is turned about x by 2π minus the pitch (0x434cf0) at its pivot. The barrel's world matrix
  is base × head × barrel (0x4341c0 twice). The original makes these as it draws, and its targeting
  reads the last drawn. The port makes them each tick, which is the same while the turret is drawn.
- **The particles** (*OBJiTurret_CreateParticles* 0x4cd5c0). Each shooter gets one at its place,
  oriented as the shooter, hanging from the barrel's world matrix. Its owner is the turret, 0x3000000 |
  the id (0x51ce10). Each gets the create event (11), then background-stop (5). The cooldown starts at 60.
  The owner widens the shots' cone (`WPrParticleInaccuracy` 0x51ced0): by the class's shooting skill's
  inaccuracy (+0x5f8, the skills' +0x10) times the difficulty's aim factor (ONGS +0xad4: 1.2, 1, 0.6),
  for every target team.
- **On** (0x4cd990 → 0x4cd9c0: the event list's "activate turret", `turret_activate`). The active time
  restarts and the active sound starts. Every shooter gets background-start (4). The targeting is set
  up (0x4ba6d0) with the barrel's axis z, the callbacks at 0x54c348 and the class's three blocks.
- **Off** (0x4cda80 → 0x4cdab0: "deactivate turret", `turret_deactivate`). The wanted angles go back to
  0, the target goes, and the targeting stops (0x4bab70). If it was firing, the shooters get stop (3).
  Every shooter gets background-stop (5), and the sound and the state go. The original "clears" the
  firing flag with `OR 0xfffb`, which sets every other flag bit instead (*confirmed*, kept).
- **The tick** (*OBJiTurretCombat_Update* 0x4cf140):
  1. While active and within its active time:
     - Every 15 ticks it takes the nearest character of a target team (0x4cf7e0, by the rounded squared
       distance, over the active list) and its line of sight (*OBJrTurret_PerformLOS* 0x4cf6b0).
     - With a target, it runs the targeting, as the AI's does: update (has a target, aim, fire on the
       line of sight), then the fire decision (aim, wanted, the line of sight).
  2. Past its time, it goes off once back at rest.
  3. The wanted pitch and yaw, signed, are held within the class's limits. The barrel and head turn the
     shorter way toward them, at most the speed per tick (a tie goes the long way).
  4. Drawn in a frame, the shooters get background-start; unseen for 120 ticks, background-stop.
  5. After the cooldown, a trigger from the targeting fires a shot of group 0 (0x4cd400): the first
     shot of a burst sends start (2), every shot pulse (1), and the shooter's cooldown follows.
     Otherwise a firing turret sends stop (3). With every AI passive (0x5ec0c4) it doesn't fire.
- **The callbacks** (0x54c348):
  - +0, the trigger, sets +0x90.
  - +0x10, the shooter's place, is the turret's place plus both pivots, unturned.
  - +0x14, +0x18 and +0x1c, the aim along a vector, at a point or at a character's pelvis, all go
    to 0x4cdcb0. It turns the point into the base's frame, takes away the pivots, and sets the wanted
    yaw (atan2 x, z) and pitch (atan2 y, the horizontal distance), both in [0, 2π).
- **The line of sight**:
  - Within 15 of the target's pelvis (squared 225, 0x52af74), it sees the target.
  - Otherwise it tests the line from 15 along the way to the target, which must be clear of the
    environment (flags 0x242000).

Not ported yet: the active sound (the port has no positional ambient sounds), the drawn test's box
visibility (0x440b50: the port counts every drawn turret as seen), `turret_reset`, and the debug lines.
