# The particle system at run time

The particle system (P3, `Particle3.c`; `src/particle.c`) runs the classes of
[docs/formats/par3.md](../formats/par3.md). Weapons' shots, sparks, smoke, glass, lights, glows and the
level's own effects are all particles.

## Particles

A particle is a 0x44-byte header and its data (256 bytes at most, in four buckets of 64..256). The
header: the class (+0x00), flags (+0x04), the action mask (+0x08), the position (+0x0c), the ambient
sound playing (+0x18), the tick of its last update (+0x1c), its time in seconds (+0x20), the life left
(+0x24, 0 forever), the fade's length (+0x28), its handle (+0x2c: the slot and a salt) and its link (+0x30,
the particle it follows). The flags: 0x01 hidden, 0x02 fading, 0x04 stuck, 0x08 chopped, 0x10 dead, 0x20
oriented along its velocity, 0x40 its matrix dirty, 0x80 explodes on a hit instead of its hit events,
0x100 plays a sound, 0x200 moved, 0x400 temporary, 0x800/0x1000 a dynamic/static decal, 0x2000 attached
(its matrix moves), 0x4000 attracted, 0x8000 the particles linked to it let go, 0x10000 expiring.

`P3rCreateParticle` 0x455e40 draws the lifetime, sets the emitters (on if their flag 1 says so) and the
variables' first values. `P3rSendEvent` 0x45b2c0 runs an event's list: each action not masked off, in
order, until one kills the particle or stops the step.

`P3rUpdate` 0x45c830 runs twice a frame: in the tick, after the physics, the gameplay classes (from the
active list's head) at the game time + 1; in the frame part, the decorative ones (from its tail) at the
game time. Only the frame part's call checks and keeps the tick (0x6a8708): it returns if the tick is the
one it last ran, so a decorative particle ages once per game tick whatever the frame rate. The gameplay
call leaves 0x6a8708 alone: after a tick, the frame part is given the value the tick's call was, and
would be skipped had that call kept it. (The Fade Out action deletes a particle whose alpha is 0 at once, and the deletion's die
event runs the Fade Out again, without end: level 12's clouds, which fade out on their die event and fade
in through their update, are only safe because they have been updated.)

## Actions

The type table at 0x536060 (0xc0 bytes each: the type, its kind, its name, its function, the counts of
its variables and parameters and their names and types). The kind (+0x04) is a mask against the particle's state
(0x45b170: 2 when it is stuck, 4 when chopped, 1 when it collided this step): an action whose kind meets
it is skipped (so 4 not once chopped, 2 not once stuck, 3 not when stuck or after a collision). The mask
(+0x08) turns actions off by their index (the update event's only). An action's function is called with the class, the particle, the action, its variables, the
time, the step's length and the stop flag; it returns whether the particle died.

| Type | Name | Kind | Vars | Params | Function |
|---|---|---|---|---|---|
| 0 | Linear Change | 0 | 1 | 1 | 0x463f60 |
| 1 | Acceler. Change | 0 | 2 | 1 | 0x463fa0 |
| 2 | Random Change | 0 | 1 | 3 | 0x463ff0 |
| 3 | Pingpong Change | 0 | 2 | 3 | 0x464110 |
| 4 | Looping Change | 0 | 1 | 3 | 0x464330 |
| 5 | Change To Value | 0 | 1 | 2 | 0x464400 |
| 6 | Color Blend | 0 | 1 | 3 | 0x4644b0 |
| 8 | Fade Out | 0 | 0 | 1 | 0x464240 |
| 9 | Enable Action | 0 | 0 | 2 | 0x4665b0 |
| 10 | Disable Action | 0 | 0 | 2 | 0x466630 |
| 11 | Die | 0 | 0 | 0 | 0x4641d0 |
| 12 | Set Lifetime | 4 | 0 | 1 | 0x466510 |
| 13 | Enable Emitter | 0 | 0 | 1 | 0x464e30 |
| 14 | Disable Emitter | 0 | 0 | 1 | 0x464e70 |
| 15 | Emit Particles | 4 | 0 | 2 | 0x464ea0 |
| 16 | Change Class | 4 | 0 | 1 | 0x464fe0 |
| 17 | KillLastEmitted | 0 | 0 | 1 | 0x4653c0 |
| 18 | ExplodeLastEmit | 0 | 0 | 1 | 0x465490 |
| 20 | Start Amb Sound | 0 | 0 | 1 | 0x4675a0 |
| 21 | End Amb Sound | 0 | 0 | 0 | 0x467660 |
| 22 | XX Sound Volume | 0 | 0 | 0 | none |
| 23 | Impulse Sound | 0 | 0 | 1 | 0x467690 |
| 26 | Damage Char | 4 | 0 | 6 | 0x465d80 |
| 27 | Blast Damage | 4 | 0 | 8 | 0x465f30 |
| 28 | Explode | 4 | 0 | 0 | 0x4663a0 |
| 29 | Damage Environ. | 4 | 0 | 1 | 0x4663e0 |
| 30 | Glass Charge | 4 | 0 | 2 | 0x466240 |
| 31 | Stop | 4 | 0 | 0 | 0x4663c0 |
| 33..35 | Rotate X, Y, Z | 0 | 0 | 3 | 0x466870, 0x466b30, 0x466de0 |
| 37 | Find Attractor | 0 | 0 | 1 | 0x467720 |
| 38 | Attract Gravity | 0 | 0 | 3 | 0x4677a0 |
| 39 | Attract Homing | 0 | 0 | 3 | 0x467920 |
| 40 | Attract Spring | 0 | 0 | 3 | 0x467c50 |
| 47 | Apply Velocity | 2 | 0 | 0 | 0x464530 |
| 48 | Apply Gravity | 3 | 0 | 1 | 0x4645d0 |
| 49 | Spiral | 3 | 1 | 2 | 0x464b80 |
| 50 | Air Resistance | 3 | 0 | 2 | 0x464630 |
| 51 | Drift | 3 | 0 | 7 | 0x464720 |
| 52 | Set Speed | 3 | 0 | 3 | 0x4649f0 |
| 53 | Spiral Tangent | 3 | 0 | 3 | 0x467120 |
| 54 | KillBeyondPoint | 2 | 0 | 2 | 0x467360 |
| 55 | Create Effect | 4 | 0 | 4 | 0x465840 |
| 56 | Stick To Wall | 4 | 0 | 0 | 0x4657b0 |
| 57 | Bounce | 4 | 0 | 2 | 0x465520 |
| 58 | XXX Create Decal | 4 | 0 | 0 | none |
| 59 | Chop Particle | 4 | 0 | 0 | 0x467090 |
| 60 | Impact Effect | 4 | 0 | 2 | 0x465ac0 |
| 62 | Unhide Particle | 0 | 0 | 0 | 0x466430 |
| 63 | Hide Particle | 0 | 0 | 0 | 0x466440 |
| 64 | Set TextureTick | 0 | 0 | 1 | 0x466450 |
| 65 | Random TexFrame | 0 | 0 | 0 | 0x4664c0 |
| 70 | Set Variable | 0 | 1 | 1 | 0x466530 |
| 71 | Recalculate All | 0 | 0 | 0 | 0x466550 |
| 72 | Enable Above | 0 | 0 | 3 | 0x4666b0 |
| 73 | Enable Below | 0 | 0 | 3 | 0x466750 |
| 74 | Enable Now | 0 | 0 | 1 | 0x4667f0 |
| 75 | Disable Now | 0 | 0 | 1 | 0x466830 |
| 77 | SuperB Trigger | 0 | 0 | 2 | 0x467490 |
| 78 | BreakableStop | 0 | 0 | 0 | 0x467550 |
| 79 | Avoid Walls | 0 | 5 | 3 | 0x468030 |
| 80 | Random Swirl | 0 | 1 | 3 | 0x4683d0 |
| 81 | Follow Player | 0 | 0 | 1 | 0x468570 |
| 82 | Stop BelowSpeed | 0 | 0 | 1 | 0x4685d0 |
| 83 | Super Particle | 0 | 1 | 4 | 0x468650 |
| 84 | Stop Our Link | 0 | 0 | 0 | 0x467ee0 |
| 85 | Check Link | 0 | 0 | 0 | 0x467f60 |
| 86 | Break Links To | 0 | 0 | 0 | 0x468020 |

The other slots up to 89 are empty (type -1); the loader takes an action of an empty slot as type 0.
22 and 58 have no function. Ghidra's analysis left the functions of 64..86 undefined (no function at
the address); read them in the listing.

The later types, which the CD's classes use (the alarm and lock lights, the clouds, the Daodan's super
glow, the superball gun, the sniper rifle, the SCC, the mercury bow, the Van de Graaff and others):

- **Set TextureTick** (64): the texture's start (its time block) set to now's tick less the value.
- **Random TexFrame** (65): the texture's index a new random one (two draws, the high and low halves).
- **Set Variable** (70) and **Recalculate All** (71): one variable set to a value; every variable drawn
  again from its first value.
- **Enable Above / Below** (72, 73): the action given (an index into the particle's mask) on while the
  value is above (below) the threshold, off otherwise. **Enable Now / Disable Now** (74, 75): on or off.
- **SuperB Trigger** (77, the superball): the emitter's last particle explodes (its explode event) once
  the fuse, from the emitter's time, is spent; until then it is marked to explode when it hits (0x80).
- **BreakableStop** (78): after a hit on a breakable quad (flag 0x200000, or any with
  `p3_everything_breakable`), the step stops.
- **Avoid Walls** (79): every 0.2 s (its countdown variable) a look along the velocity out to the sense
  distance (AKrCollision_Point, quads not of 0x44800); a wall there makes the turn's axis the wall's
  normal crossed with the look (unnormalised; when they are parallel, the look turned flat) and the turn
  the turning speed, else the turn decays by its rate (stopping at 0.001). The velocity and orientation
  then turn about the axis by the turn times the step. Only on the frames it looks.
- **Random Swirl** (80): the swirl's angle moves by both rates a step (each in degrees a second); the
  velocity and orientation turn by the swirl speed about the axis at that angle between the
  orientation's x and z.
- **Follow Player** (81): the particle at the player's place, raised by the value.
- **Stop BelowSpeed** (82): slower than the speed (no velocity counts as 0), the step stops.
- **Super Particle** (83): the variable from the owner's shown super value (C+0x1668, 0 without a
  character owner): base + delta x it, held within the bounds.
- **Stop Our Link** (84): the link broken (a class locked to its link keeps the link's place), then the
  brokenlink event. **Check Link** (85): the same unless the link lives and hasn't broken its links.
  **Break Links To** (86): the particle marked so the ones linked to it let go (0x8000).

## The bodies

The emitters' positions 6 and 7 put a particle on the owner's body (an owner of the kinds 0x1000000 or
0x2000000, a character). Both read `ONrCharacterClass_MakeBodySurfaceCache` 0x4f2550, made once a class
(ONCC+0xc30): for each of the 19 parts of the class's lowest body (ONCC+0xc34's first), the part's box
(its points' bounds), its volume, the areas of its x, y and z faces, its length along an axis, and the
area of the faces it shows; and the sums. The faces each part shows are a table of 19 at 0x52b248 (bits 0
to 5 the -x, +x, -y, +y, -z, +z faces, bits 6..7 the axis of its length: 0 x). On the CD every part shows
its y and z faces, the feet, head and hands their +x face too, and the neck and shoulders none.

- **The surface** (6, `ONrParticle3_FindBodySurfacePosition` 0x5097f0): a part by its shown area, a point
  on one of its shown faces (in the part's space). Along x the box is rounded at an end it shows: within
  the half of its thinner side of that end the normal is from the end's centre; elsewhere it is the
  point's from the box's axis. The point is pushed out along the normal by the parameter, and the
  particle is turned by an orientation built on the normal (its y). The particle hangs from the part's
  matrix (A+0x2278) if it has an attachment block, else it is put in the world through it. A part
  `p3_daodan_disable` took out (a debug command: the parts given, 0x5ed324, cleared at the level's start)
  gives no point.
- **The bones** (7, `ONrParticle3_FindBodyBonePosition` 0x509dc0): a part by its length, a point along
  its x between its box's ends; with a radius above 0 the ends pulled in by the half of the thinner side
  less the radius, and the point put at the radius around the axis at a random angle.

The Daodan glow uses both: the class's ONCP entries named `super*` (Konoko's `super_glow` on the pelvis,
`super_l_hand` and `super_r_hand`) are made on their bones the first time the character's shown super
value (C+0x1668) rises above 0, sent their start event while it is, and their stop event when it falls
back to 0 (DoCharacterFrame's step 14, 0x4df937). The particles stay made.

## Effects

`P3rCreateEffect` 0x456030 makes a particle for an impact or an action's effect at a location (the
specification's 5 places): 0 the location's point, 1 moved along the normal by the offset, 4 a decal on
the wall hit, 5 attached. For 5, when the step's collision (0x6a8540) was a context's that isn't a wall,
the particle hangs from that context's matrix (+0xa4), a character's the part hit (0x4f13e0: A+0x2278 of
the part, the hit's +0x30), and its place is put in the matrix's space (less the translation, by the
transposed rotation, 0x433a90). The port's other contexts keep the location's matrix.

## The level's particles

The level's ENVP list (ONLV+0x70) and its particle objects are made at the level's start
(`src/envparticle.c`). The level's objects have lists of their own: an OBOA element links an ENVP (+0x08)
which `OBrSetParticles` 0x438d90 loads with the object (EPrArray_New 0x454f90 with the owner kind 1,
the object, the object's context's matrix and its name). The records' particles hang from the matrix,
their places in its space, and each record's tag is rewritten in place to `<object name>_<tag>` (unless it
already starts with the name). Hiding or killing the object lets them go (0x4552d0). On the CD: the
truck cab of level 1's warehouse cutscene, the motorcycles of levels 4 and 8 and level 8's van, all
headlights.

## The display

`P3iDisplayParticleClass` 0x460520 sorts each class: decals and invisible classes (flags2 0x2800000)
are never drawn, the lens flares (0x40000000) are listed for after the rest (32 at most), geometry
(flags1 0x100), vectors (flags2 0x8000000), contrails (sprite modes 5 and 6; the contrail emitters,
flags2 0x20000000, listed for after the rest, 16 at most) and the other sprites. A class of flags2
0x100000 is the sky's: it is drawn only while the switch 0x6a8b40 is on, within a pushed draw state
whose state 0xf (the fog) is 0. `P3rInitialize` sets the switch, and each frame
`ONiGameState_Display_Reflectable` 0x4fb7e9 sets it (0x460510) to whether the visibility rays reached the
sky (`AKrEnvironment_StartFrame` 0x4430f0) and no cutscene's skip is in its state 3 (G+0x3c and
G+0x40; nothing in v1.0 starts a skip). The sky's own draw (0x511160, the box and the planets) has the
same state. On the CD the sky's classes are five clouds: `env_cloud01_p01`, `_p01a`, `_p01b`,
`env_cloud02_p01`, `_p02`. Confirmed by the code; that 0xf is the fog is inferred from the OpenGL
draw's use of it.

A contrail's alpha (its block's u16 at +0x1c) is the class's alpha value times 255, rounded, or for a
fading particle (flag 0x02) its life times 255 over its fade, rounded (0x4623e6). A sprite's is the
class's, or life / fade when fading; the alpha fog factor (`gl_calculate_fog_factor` 0x40dcf0) applies
to the sky's classes as well.
