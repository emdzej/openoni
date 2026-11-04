# Physics and collision

How objects and characters move and collide with the environment. Read in the Windows exe; the character's part
is ported in `src/physics.c` (tested by `physics_test`), the other objects' is not.
Confirmed means read in the exe; inferred means from the context.

## The update

`OBiPhysics_Update` 0x43af50 runs after the characters, over the physics contexts (0x150 bytes each, the
array at `*0x5cb520`, its count first):

1. Gravity: for each active one (flags +0x130 bit 2) without bit 0x40, the acceleration's y (+0x128) loses
   the gravity (+0x110).
2. The acceleration (+0x124..+0x12c) goes into the velocity (+0x70..+0x78), and is reset to 0
   (0x5298a8).
3. For each active one: bit 0x2000 cleared. Type 3 (animated, +0x134) counts down its pause (+0x148),
   shared with its parent. `PHrPhysics_Update_Animation` 0x43a620. The move is the velocity for types 2
   and 4, none for 0, 1 and 3.
4. Unless bit 4: bit 0x10000 set, the collisions found and slid along (0x43a710), the bit cleared; with
   bit 0x8000 a second pass; then, if anything was hit, the response (0x43aac0).
5. The position (+0x8c..+0x94) gains the move (zero for types 0, 1, 3); the callbacks (the table at
   +0x150): +0x10 after the move; +0x24 unless bit 0x1000 without 0x2000; +0x28. Then
   `PHrPhysics_MaintainMatrix` 0x43b410.

**The collisions** (0x43a710, confirmed in outline): the callback table's +4 finds the environment's
(`PHrPhysics_Callback_FindEnvCollisions` 0x43b870) for types 2 and 4, its +8 the other objects'; unless
bit 0x100, 0x43a9a0 adds more. Each is 0x28 bytes: the kind (0 environment, 1 an object), the quad or
object, the plane (a, b, c, d), the point. They are sorted by the squared distance from the object's
position to the point (qsort, 0x43aa90). Unless bit 0x800, for each in turn: the part of the move into the
plane (0x433da0: when move·n < 0, (move·n) n) is taken off the move; if that part reaches 1/1024
(0x529cc8) on an axis, it is a hit: callback +0x1c, for an object the other's +0x20 (unless bit 8), bit
0x2000, callback +0x14, and the pass stops. Then callback +0x18 with the count.

**The response** (0x43aac0): the same search; for each hit the move is zeroed (the object stops).

## The environment's collisions

`PHrPhysics_Callback_FindEnvCollisions` 0x43b870: the sphere (the context's) moving by the move against
the environment, skipping quads with the flags 0x44800 (and 0x4000 with the context's bit 0x4000):
`AKrCollision_Sphere` 0x441cb0, or the point's 0x441c80 with bit 0x20. Each hit gives a record with the
quad's plane (negated when the plane index has bit 31); two-sided quads (flags 0x2a0) give a second with
the plane times −1 (0x529804).

**`AKrCollision_Sphere`** 0x441cb0: the hits go to a list of up to 255 (0x48 bytes each: the distance as
float and as an integer, the kind, the plane, the point, the quad), sorted by 0x441b40 (by distance; with a
limit of 1 just the nearest).

- When the move is at least the radius times 0x5294bc: samples along the move one radius apart (the
  last at the end); otherwise one sphere at the middle of the move, its radius grown by half the move.
- For each sample: down the octree (AKOT, the environment's +0x38: interior nodes of 8 children, 0x20
  bytes; the root's half size 0x529e24, halving) to the leaf holding the centre; then a flood from it: a
  leaf (bit 31) not yet seen (0x424370) whose box (the packed size and position at leaf +0x1c, through the
  run-time tables 0x534d98 and 0x534dbc) is within the radius pushes its six faces' neighbours; a
  face's quadtree node (0x10 bytes) pushes its four children; −1 is nothing. At most 0x800 on the stack.
- The quads of the leaves seen (each leaf +0: the first index << 12 | the count, into the octree's index
  list): skipped by the flags, each once (0x437d60, a bit vector), tested by `AKrGQ_SphereVector`.

**`AKrGQ_SphereVector`** 0x4425e0, a sphere (centre, radius) moving by a vector against a quad (its
collision record AGQC, 0x1c bytes: the plane index, the box; its corners from AGQG):

1. None if both ends are beyond the radius on the same side of the plane, or outside the box grown by
   the radius and 0x529dfc.
2. An end within the radius of the plane whose projection is inside the quad (0x43ee70 → 0x43e3b0): hit
   there (the start's first).
3. A corner within the radius of either end, or of the move's nearest point to it: hit at the corner.
4. Each edge: its nearest point to the start, to the end, and to the move: hit there.
5. Else the move crossing the plane inside the quad (0x43c8c0).

**`AKrCollision_Point`** 0x4412d0 (through 0x441c80): a segment against the environment. From the leaf holding the
start to the one holding the end, a leaf at a time: its quads (once each, by the skip flags) through
0x43c8c0; then out by the face the segment reaches first, the distances to the three faces it heads for
measured from the start (not from where it entered), and down the face's quadtree at the segment's
crossing. A limit stops the walk after the leaf that reaches it. A hit's distance is the L1 distance from
the start. Over 3,000 random segments on level 1 it finds the same quads as testing them all, but for
the degenerate quads (no octree leaf holds them) and two segments.

## The character's physics

`FUN_004dc2b0` (from `ONrGameState_NewActiveCharacter`) gives an active character a physics context
(type 2, flags 0, the character at +0x14c, the position from C+0x148) and the character's callback table
(0x5ec918, 0x30 bytes): +0x00 1, +0x04 its environment search 0x4ebd70, +0x08 the objects' 0x4ecb40,
+0x10 after the move 0x4eca00, +0x14 on a hit 0x4ecb20, +0x20 hit by another 0x4ecab0, +0x2c 0x4eb540.

**The sphere tree** (A+0x08, `FUN_004dc160`): the base is the feet (C+0x14c), or the body's bottom in the
air (C+0x190); the height h is C+0x1658 less the base, clamped to 0..0x52b2cc. The root sphere (+0x08) sits
at the base plus h/2, radius h/2. Four leaves (+0x20, 0x18 bytes each) of radius r (4 for the player, 3 for
an AI) stand at the base plus r plus i × (h − 2r)/3. Two middle spheres (+0x80, +0x98) sit halfway between
leaves 0 and 1, and 2 and 3.

**The environment search** (0x4ebd70): the root sphere moving by the move (`AKrCollision_Sphere`, skipping
the flags 0x48800); each hit the sphere tree confirms (0x43bfa0) is a collision with the quad's plane. A
plane whose normal's y is at most 0.5 is a wall: the wall count A+0xb0 goes up and the AI hears of it
(`AI2rMovementState_NotifyWallCollision`, for characters with C+0x06 bit 0x40). Two-sided quads add the
opposite plane.

**After the move** (0x4eca00): with a vertical move (A+0xd0) and not flag 0x40, `FindGroundCollision`
0x4ec040; on the ground with no floor found, A+0xe0 counts up (the first time noting the height, A+0xf0);
otherwise it is 0. Past 11 the character starts falling (A+0x21dc).

**On a hit** (0x4ecb20): a collision's second number (the plane's y) below 0 becomes 0. **Hit by another**
(0x4ecab0): unless that object's kind is 5, its push goes into the knockback (A+0xb8).

**The body box** (0x4e5ff0, step 19 of `DoCharacterFrame` after the pose): the bounding spheres of parts 0
to 10, their centres through the parts' matrices, make a box; the body's bottom C+0x190 is the larger of
the feet's height and the box's bottom, its top C+0x1658 the larger of the box's top and its bottom
plus 16. The sphere tree is built from them right after.

**The move** (step 12 of `DoCharacterFrame`, `UpdateCharacters` step 10): the tick's motion in the world
splits into the move (A+0xc4..0xcc: x, the rise when going up, z) and the drop (A+0xd0, when going
down); the move goes into the context's acceleration, so the physics slides it along the walls, while
the drop goes only through `FindGroundCollision`. The character's position is the context's, taken at
the next step 12.

## The floor

`FindGroundCollision` 0x4ec040, from the ground callback whenever there is a drop:

1. Jumping (the animation types 0x18, 0x28..0x2b) or in the air: the body's bottom above the feet is
   the offset fh, the feet's height the larger of the bottom and the feet, the floor quad (A+0x128)
   forgotten. Else fh is 0.
2. The step's limit: half of the pelvis height (C+0x18c) less fh, above the feet plus fh; at least the
   feet plus 6; at least the last ground height (A+0x12c) plus 6.
3. A sphere of radius 7.1 from 10 above the feet plus fh, moving down by the drop and that height
   (`AKrCollision_Sphere`, skipping the flags 0x148800, all the hits). The physics' height takes the
   drop, and so does the feet's; that height is the ground height for now.
4. Each quad hit: its normal's y (turned for a two-sided quad, not "jello" 0x20, seen from behind by the
   sphere's start, 10 above the feet: *confirmed*, the dot product at 0x4ec39d takes ESP+0x7c) must
   be above 0.5. Its corners are clipped to the square 1.5 around the feet (x ≤ x+1.5, z ≥ z−1.5,
   x ≥ x−1.5, z ≤ z+1.5: 0x4ef020, 0x4ef270, 0x4ef0e0, 0x4ef1a0, one Sutherland–Hodgman step each).
   With 3 points or more, each edge whose lower end is under the limit offers its higher end; the best
   is capped at the limit. The highest over the quads is the floor.
5. A floor at or above the feet: the ground height and the quad noted, the physics raised to it, the
   contact (A+0xe4) set. From the air: while jumping, the air time ends; else the landing (A+0x21dd),
   the air velocity and time cleared, and the damage of the fall from A+0xf0: none below ONCC+0x18,
   all the hit points beyond ONCC+0x1c, between them the share of the most hit points (C+0x3c),
   rounded. With damage the landing is the hard one (A+0x21de).

## Environment animations (chr_envanim)

`AI2iScript_EnvAnimate_Internal` 0x4b7010 gives the active character's physics context (A+4) an object
animation: P+0x130 the `OBAN`, +0x134 its state (2 with `norotation`), +0x136 the frame, +0x138 the
step, +0x13c the time. It is started (`OBrAnim_Start` 0x439580, its random start if the animation has
one) and moved a frame on. 0x4ed080 sets the context's flags 0x44 (no gravity, no walls) while it plays.
The block form only waits (the command retried) while an earlier one plays: frame + 1 below the frames.

- `PHrPhysics_Update_Animation` 0x43a620, in the physics update, plays it.
- `ONrGameState_DoCharacterFrame` 0x4ddba0 ends it at frames − 1 (the flags back). While it plays, the
  context's position is the animation matrix's translation (`OBrAnim_Matrix` 0x4395f0), down by
  C+0x18c, just before step 12 takes the position. The quarter turn it multiplies in there
  (0x434c30, 0x434cf0 π/2 about x, 0x434d70, 0x434df0, 0x434b80) is computed and left unused.
- The body's root (0x4e5a30) is the animation matrix times that quarter turn instead of
  T(position)·R(facing). With `norotation` it is the matrix alone, which is a translation only, so the
  character's facing is dropped.

The port: `chr_envanim_start`, `chr_root_matrix` (character.c), `physics_take` and `physics_update`.
