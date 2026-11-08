# Decals

Bullet holes, scorch marks and blood: a textured rectangle laid on the walls around a point, cut to the
quads it covers and folded over their edges. Ported in `src/decal.c`. The decal particles (the classes
with flags2 0x800000) own them through their decal block; the renderer is `src/render/decal_draw.c`.
Confirmed against the Windows exe unless marked. Names come from the Intel Mac build.

## P3rCreateDecal (0x44f070)

Fastcall: ECX the texture, EDX the quad (−1: find it), then on the stack the flags, the wrap angle, the
pressing direction (may be null), the position, the normal, the y axis b, the x axis a, the x and y
scales, the alpha (u16), the tint, the owner's list node and a buffer size. It returns 0 on success, 1
when nothing was hit and 2 when there was no memory.

Flags:

| Bit | Meaning |
|---|---|
| 1 | static: its own memory and the static list (0x6a8f70) |
| 2 | written into the caller's buffer (the port has no user) |
| 4 | only quads facing the decal (the class's flags2 0x400000) |
| 8 | planar: no walk over the edges (the characters' use) |
| 0x10 | unshaded: no colours from the quads' shading (flags2 0x200000) |

The steps:

1. The pressing direction (0x5e9530) is the reversed, normalized direction, or the normal when there
   is none or it has no length.
2. The wrap is clamped to [0.0087266, π]; its cosine goes to 0x5e957c.
3. With quad −1, a segment from the position along normal × −10 (`AKrCollision_Point` 0x441c80,
   skipping flags 0x846800, the nearest hit) gives the quad and the point. A given quad with flag
   0x800000 takes no decals.
4. A sphere at that point, of radius √(xs² + ys²), is moved along normal × −10 (`AKrCollision_Sphere`
   0x441cb0, the same skip, every hit). The hits are the quads the decal may cover (warning past
   0x200).
5. The decal's corners about a centre c are c − a·xs + b·ys, c + a·xs + b·ys, c + a·xs − b·ys and
   c − a·xs − b·ys, with texture coordinates (1,0), (0,0), (0,1), (1,1).

### Planar (flag 8)

For each hit quad:

- Its plane is turned toward the normal if the quad is two-sided (flags 0x280).
- With flag 4, quads at less than 0.001 facing are skipped.
- The corners are pressed along the normal onto the plane (when it faces it by at least 0.001), then cut
  to the quad (`P3iDecal_ClipToPolygon`).
- The new points move 0.05 along the normal. The unshaded ones (shade 0) take the quad's shading
  (`P3iDecal_BarycentricShadeInterpolate`).
- They become a triangle fan, at most 0x200 indices ("more than 170 triangles").

### The walk (P3iDecal_ProcessAdjacency 0x450f50)

The walk runs when flag 8 is clear (the particles' decals).

Items are 0x84 bytes in a pool of 64 at 0x5e7428 (0x450ed0). They are queued at 0x5e7424 by their depth
(the folds from the first quad), each one after every queued item no deeper.

| Offset | Field |
|---|---|
| +0 | in use |
| +1 | cut by the shared line |
| +2 | the next quad crosses its plane |
| +4 | depth |
| +8 | the parent's place in the hits (−1 first) |
| +0xc | parent quad |
| +0x10 | its place in the hits |
| +0x14 | quad |
| +0x18 | the decal's frame along the shared edge: (c − e0)·u, (c − e0)·v, b·u, b·v, a·u, a·v |
| +0x30, +0x3c | the shared edge e0, e1 |
| +0x48, +0x4c | the part of the edge inside the decal (t0, t1) |
| +0x50 | its middle |
| +0x5c | e1 − e0 |
| +0x68 | the parent's normal |
| +0x74 | the parent's edge bits |
| +0x78 | its own edge bits |
| +0x7c, +0x80 | prev, next |

The first item is the found quad, with the decal's own frame: the position, normal, b and a (globals
0x5e9540 to 0x5e957c). P3rCreateDecal takes items from the queue's head. A quad is walked once (bit
vector 0x5cdfe0, by its place in the hits).

#### One item's walk

1. **The item itself.** If it has a parent, it becomes its own first child: its +0x10 is set to −1 and
   its +1 is copied from its +2. Its own children then take −1 as their parent's place, so only the
   first quad's children skip their parent quad in the scan. This is the original's behaviour.
2. **The quad's plane.** Two-sided quads are turned toward the parent's normal (the decal's for the
   first quad) when they face it by less than −0.001.
3. **The decal's frame on the quad.** For the first quad it is the decal's own. Otherwise it is
   unfolded over the edge: u = normalize(e1 − e0), v = normalize(u × n), then the centre, b and a are
   rebuilt from the six numbers.
4. **The other hit quads.** Each one (but itself and the parent) is tried:
   - **On the same plane index** (bit 31 ignored), at the same depth:
     - Parallel edges (|eₙ × e_c|² < 1e-4) that are collinear (|eₙ × (c_j − n_k)|² ≤ 1e-4) and overlap
       (projected, max ≥ 0.01 and min ≤ |eₙ|² − 0.01) give the overlap's ends. The near end is the
       current corner when it lies inside, else the neighbour's. The far end is the other way round
       (the neighbour's end when the current corner lies inside). This is a quirk of the original, kept.
     - Non-parallel edges whose ends lie on both sides of the neighbour's edge line give the crossing
       point, if it is between 0.01 and 0.99 along that edge. The child is then marked as crossing (+2).
   - **On another plane**, at depth + 1. Each quad's corners are classed against the other's plane:
     behind below −0.01, on within 0.01, in front otherwise. Both quads need two corners on the plane, or
     corners on both sides of it.
     - A step (not both with corners in front, and not both behind) is taken only for a two-sided
       neighbour. Its front and behind counts are swapped. The original then negates the current quad's
       distances over the neighbour's corner count, and negates the neighbour's plane once per corner.
     - The neighbour's plane must face the pressing direction at least as much as the cosine of the
       wrap, and by −0.001 or more with flag 4.
     - Each quad's outline gives up to two points where the planes meet: its corners on the plane, and
       its edges that change side.
     - Projected on the planes' line (n_c × n_n), the two segments must overlap by 0.001 or more. Their
       overlap is the shared edge.
   - The child is queued. The shared edge's part inside the decal's rectangle (|u| < xs, |v| < ys,
     clipped by parameter) must not be empty, or the child is freed. At most 16 children; the scan
     stops when the pool runs out.
5. **The decal on the quad.** The corners are pressed along the frame's normal onto the plane, needing
   at least 0.001 of facing. They are cut to the quad.
   - Active children are those with +1 whose t0 is at least −0.1 and t1 at most 1.1. With any, the cut
     goes into a third working polygon, and each child's shared line then cuts it in turn. The side kept
     is the one with the item's own centre (the decal's position for the first quad), or with
     parent normal + middle for the item itself.
   - The piece counts only when it reaches the edge the item was reached over. That is, the cut
     points' edge bits meet +0x78, or there is no such edge.
   - Each point records the quad's plane index (bit 31 flipped when the plane was turned) for the
     welding.
6. **The children kept.** Those whose +0x74 edges the piece reaches stay queued; the rest are freed.

### The clipper

`P3iDecal_ClipToPlane` (0x450600) runs over the quad's edges a→b, with c the next corner:

- The plane is normalize(e × (e × f)) through b, where e = b − a and f = c − b.
- A point is kept when it lies inside by more than 1/1024 (0x432f10).
- Crossings take their coordinates between the two ends, and their shade along a→b. The shade mix is
  per channel, (x₁(256 − w) + x₂w) >> 8, with w the parameter in 1/256ths (0x447c10).
- A crossing's edge bits are the two ends' common bits plus the edge's bit.
- An edge of no length copies the polygon.

`P3iDecal_ClipToPolygon` (0x4503c0) cuts along every edge. Edge k (corner k to k + 1) has bit 1 << k.
The cuts go through two 16-point working polygons; the last one lands on the decal's buffer (512
points).

`P3iDecal_BarycentricShadeInterpolate` (0x450c30) gives a point the shade of the triangle it lies in:
a quad's half by the diagonal from corner 0 to 2, with the quad's normal from its first two edges. The
colour channels are weighted by the sub-areas, and the alpha is left at 0. The exact weight order is
inferred (standard barycentrics).

### Welding

1. Points within 0.1 on every axis are compared. Those with the same coordinates (within 0.01) and, when
   shaded, the same shade (every channel within 2, 0x447cc0) merge.
2. The others at the same place share the first's position.
3. Without flag 8, each point moves 0.05 off the walls along the sum of the planes it was found on (up
   to 16), normalized.

### The block

The block is 0x14 bytes of header followed by the data:

- Header: texture, quad, u16 points, u16 triangles, u16 alpha, tint.
- Data: positions (12 bytes a point), coordinates (8), u16 indices (rounded up to a multiple of 4), and
  shades (0xffffffff unshaded).

Static decals come from the heap. Dynamic ones come from a ring, "decal buffer" (0x426eb0: 0x18fff
bytes, 5000 slots, 4-byte alignment). A new block goes after the newest. The oldest blocks it would
overlap, and the slot it takes, are purged (0x4535d0): the owner's block pointer is cleared and its
particle is killed (0x456c70). The block is then added to the tail of its list.

## The particles' side

- **P3rCreateEffect** (0x456030) creates them: see [impact.md](impact.md). The decal block holds the
  decal and the particle's handle.
- **The level's** (an `ENVP` record or a `PART` object of a decal class, 0x453b60 from the start
  0x4538be and the `particle` command's 0x4551fb): a segment from the record's place 10 along its
  matrix's -y (its second column times -10, 0x52a030) against the quads, skipping the flags 0x44800, the
  first hit only (`AKrCollision_Point` 0x441c80). Without a hit there is no particle. Else the effect:
  the specification (the record's class at +0xb4, turned along the normal (3), the decal location (4),
  static, not turned at random) and the location (the time, the hit's point, its plane's
  normal, the matrix's third column as the decal's up, the record's +0xa0 and +0xa4 as its size, a wall
  hit on the quad). The particle and its handle go to the record's +0xb8 and +0xbc.
- **P3iUpdateDecalClass** (in P3rUpdate 0x45c830) runs a decal class's particles, oldest first:
  - one whose decal was purged dies;
  - the others age;
  - past the class's +0x200 most (halved and quartered at the lower particle details, 0x6a8704), the
    oldest start their fade;
  - while a particle fades, its decal's alpha is life × 255 / the fade time.
- **Death** (0x456ce0) deletes the decal (P3rDeleteDecal 0x453630, static for particle flag 0x1000).

## Display

`P3iDisplayDecals` (0x453760) runs for the static list, then the dynamic one, from 0x4fb480 after the
characters and objects and before the particles, with depth writes off:

- It skips a decal whose quad (+4, 0xffffffff none) the visibility hasn't marked (the environment's
  vis vector, 2 bits a quad: [visibility.md](visibility.md)).
- The quad's alpha is AGQR's second u16 for a see-through quad (flag 0x20), else 0xff.
- The draw (M3rDecal_Draw) is read from the Mac build's `MSrGeomContext_Method_Decal_Draw`:
  - the alpha is decal × quad × 2 >> 8, at most 0xff;
  - the tint and the per-point shades colour the triangles.

  The Windows method is not yet located; the shade × tint product is inferred.

## The characters' shadows

Each character's shadow is a decal, remade every frame (`src/render/char_draw.c`):

- **The characters' display** (0x4e1540) first deletes every active character's shadow (A+0x27fe set
  while there is one). Then it draws the characters with C+4 0x8000 and 0x4000.
- **The character's deletion** (0x4dc580) deletes its shadow as well.
- **The end of ONiCharacter_Display_Body** (0x4e1d70) makes the new one:
  - **Gates:** the graphics quality (0x5ed37c, ONrPersist_GetQuality 0x50f0d0) must be at least 1 (0x508540),
    `chr_shadows` (0x54e18d, "Enables character shadows", 1) on, C+4 without 4 or 0x1000000, and the
    body's fade (A+0x2818 while invisible or teleporting, else 1) above 0.
  - **The block:** a character's first shadow takes a block of 0x804 bytes (0x4fdc50, G+0x15706a +
    slot × 0x804). Its first two u16 are the character's index; A+0x27e8 points past them.
  - **The texture:** ONCC+0x20, else TXMP "notfoundtex" (0x53b8c0).
  - **Where:** under the pelvis (A+0x229c, A+0x22a4).
    - On the ground (A+0xf6 0) with a floor quad (A+0x128), it lies on that quad at the floor's height
      (A+0xe8), of size ONCC+0x2c and alpha u16 +0x38.
    - Otherwise a sphere of radius +0x2c is cast. Its centre starts at the body's bottom (C+0x190, or
      A+0xe8 on the ground) less the radius, and it moves (0, −(+0x2c + +0x24), 0) (AKrCollision_Sphere
      0x441cb0, skip 0x46800, the nearest). No hit, no shadow. Otherwise it lies on the hit's quad at
      the hit's point, and h is the height over the hit (at least 0):
      - h < +0x28: t = h / +0x28; the size runs from +0x2c to +0x30, and the alpha from +0x38 to +0x3a.
      - h < +0x24: t = (+0x24 − h) / (+0x24 − +0x28); the size runs from +0x34 to +0x30, and the alpha
        is +0x3a × t.
      - Higher: no shadow.
  - **The fade:** below 1, the alpha is multiplied by it (rounded).
  - **The call:** P3rCreateDecal with flags 0x1e (into the block, facing quads, planar, unshaded), wrap
    0x3fb2b8c2 (80°), no direction, normal (0, 1, 0), b (0, 0, 1), a (1, 0, 0), both scales the size,
    tint white, the block A+0x27e8, size 0x800. A block over the size fails ("decal size %d exceeds").
    On success, A+0x27fe is set and the block's +4 (the owner particle) is −1.

The port keeps the shadows' ids by character slot and gives them their own memory
(`decal_create_into` checks the size).

## Not yet

- The vis vector's culling.
