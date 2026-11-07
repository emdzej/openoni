# The visibility and the sky

What the game draws of the level each frame. Rays from the camera, cast through the environment's
octree, mark the leaves and quads they reach. The draw keeps to the marked ones, and the sky is drawn
only while a ray gets out of the level. Status: read from the exe (confirmed), except where marked.
Port: `src/akev_vis.c`, `src/render/env_draw.c` (`env_draw_visible`), `src/render/sky_draw.c`.

## The state

The AKEV's private data (`TMrTemplate_PrivateData_GetDataPtr`):

| Offset | What |
|---|---|
| +0x00 | A bit per quad: tested by this pass |
| +0x04 | A bit per quad: found facing away from the ray that tested it, this pass |
| +0x08 | 2 bits per quad: its mark (3 when a ray reaches it, then aged) |
| +0x0c | 2 bits per octree leaf: its mark |
| +0x14, +0x18 | The quads to draw this frame: the count and the list (0x444030) |
| +0x1c | The sky's frames left |

The globals:

| Address | What |
|---|---|
| 0x5cdbcc | The frames the camera has kept still |
| 0x5cdbd0 | The ageing's step (0 to 11) |
| 0x5cdb88 | The passes so far; picks the jitter |
| 0x5ccec8..0x5ccef0 | The camera's view, up and location at the last cast |
| 0x5ccef8, 0x5cdb80 | The rays' pattern (pairs of floats) and its count |
| 0x6a8fa0 | The 25 jitters (pairs), in cells |

## The rays' pattern

`AKrRayCastCoefficients_Initialize` 0x442f50 runs at the level's start (from `AKrEnvironment_LevelBegin`
0x43ffe0). It takes n rays a side: `ONrMotoko_GraphicsQuality_RayCount` 0x5084e0 gives 16, 18, 20, 20 and
20 for the qualities 0 to 4 (at most 20). The cell is 1/n of the screen. The ray of row i, column j is at

- u = o(j) × cell / 4 + i × cell,
- v = j × cell + o(i) × cell / 4,

with o = 0, 2, 1, 3 by the index's lowest two bits. The 25 jitters are the exe's table at 0x534cd0
times the cell (the port reads it at run time).

## A frame's rays

`AKrEnvironment_StartFrame` 0x4430f0, from `ONiGameState_Display_Reflectable` 0x4fb480 before the environment's draw:

1. The camera has moved if its location has moved by at least 0.1, or its view or its up by at least
   0.05 (the squares: 0x529e3c, 0x529e38). If it has kept still for more than 25 frames, nothing else
   happens this frame (the marks and the sky's frames stay).
2. The passes: `ONrMotoko_GraphicsQuality_RayCastCount` 0x508440 gives 1, 1, 1, 1 and 2.
3. For each pass the ageing step goes up by one (mod 12). At 0, every quad's and leaf's mark goes one
   down (0x424530 through the table at 0x532260: each 2-bit field less one, down to 0; the port
   computes it).
4. The sky's frames go one down, unless 0.
5. Each pass casts the rays (0x4433f0); the pass counter goes up.
6. A pass with a ray that got out sets the sky's frames to 25.
7. The sky is drawn while its frames aren't 0.

`AKiEnvironment_ComputeVis` 0x444030 (the environment's draw) also adds one to the pass counter.

## A pass: `AKiEnvironment_RayCastOctTree` 0x4433f0

The jitter is the pass counter mod 25. The tested and facing-away bits are cleared. The start leaf is
the one holding the camera (`AKiOctTree_FindLeaf` 0x4411a0).

The frustum's corners come from 0x41b500. 0x41b5c0 unprojects them from the camera's matrix: the near
plane's top left, top right, bottom left and bottom right, then the far plane's. A ray at (u, v) goes
from s = c0 + u (c1 − c0) + v (c2 − c0) to the same point on the far plane (c4, c5, c6). Its direction d
is the difference, so t runs from 0 to 1 along it.

Leaf by leaf from the start leaf:

1. The leaf is marked (3).
2. Its box: the packed word at +0x1c gives the far corner, 16 (k − 255) per axis (9 bits each from bit
   18, 9 and 0), and the size, 16 << the top 5 bits.
3. Each quad of the leaf's list:
   - Not yet tested this pass, and without the flags 0x2a0: the plane (from the quad's collision
     record, turned by bit 31 of its index) against the ray's start. With (s − corner 0) · n < 0 the
     quad faces away from the ray and is skipped for the rest of the pass. The first ray to test a
     quad decides.
   - Tested before and found facing away: skipped.
   - Otherwise the quad is marked. If it is opaque (no flags 0x520a0) and the ray hasn't hit yet, the
     ray meets its plane at t between 0 and 1 (n · d not 0), inside the leaf's box and inside the quad
     (0x43ee70, through 0x43e3b0 with the quad's axis, flags >> 25 & 3). Then the ray has hit.
4. A ray that has hit ends with this leaf: its quads are all marked first.
5. Otherwise it leaves by the face it reaches first. On each axis that is the side it heads for (the
   far one if d is positive); the face is the smallest t. Past t = 1 the ray has got out (the far
   plane).
6. The next leaf is the face's neighbour (+4 + 4 × face). A neighbour without bit 31 is a quadtree on
   the face, descended at the crossing point (half the size, then halved). 0xffffffff is out of the
   octree, and the ray has got out.

The pass also keeps the box of the leaves met at the environment's +0x5c. The port doesn't, since
nothing it has ported reads it.

## What the marks keep from the draw

- **The environment** (`AKiEnvironment_ComputeVis` 0x444030): the marked quads, scanning by index,
  without the flags 0xf or 0x42000. At most 0x2000 are kept ("Exceeded max visible GQs %d": the rest
  aren't drawn). They are sorted by texture (a counting sort, stable), and then `M3rEnv_DrawGQList`
  0x41a930 draws the solid ones before the transparent ones. A debug state (bit 1 of 0x41e740) also
  takes the quads with flags 0xf.
- **The leaves of a box**: 0x440900 collects the leaves a box overlaps, from the octree's root cube
  (−4096 to 4096), up to max − 1. With more than that the list reads 0xffffffff, which counts as
  visible. 0x440b50 is true if one of the listed leaves is marked.
  - Turrets (`OBJiTurret_Draw`, about 0x4ce3f0): a cube 8 each way of the head, listed once (while
    the list is empty, at +0x3a4, at most 0x20). The turret is drawn, and its flag 0x10 set, only
    while one of its leaves is marked. The flag clears otherwise. `OBJiTurretCombat_Update` 0x4cf140
    counts the frames without it and, after 120, stops the turret's shooters' background.
  - Corpses (the character display, 0x4e1540): the list made with the corpse (0x4ebb60, 0x4ef340, or
    for the level's own corpses `ONrCharacter_LevelBegin` 0x4d87b0, at +0x20, at most 0x20).
  - Weapons in the world (`WPrDisplay` 0x519710: at most 0x10) and powerups (0x51c2b0: at most 0xc).
    Their lists are rebuilt when they move.
  - Objects (`OBiIsVisible` 0x439160): always drawn with `obj_force_draw`'s flag (+0x12 0x8).
    Otherwise their frames unseen (+0x1c) are counted, reset when a leaf of the box is marked, and the
    object is drawn while that count is under 6. Before the leaf test come a far test (the camera's
    far distance) and a view-cone test past 90 units (8100, the square; the angle by 0x529d90,
    π/8). The port leaves those two to the GL's clipping.
- **A point** (0x440dc0): the leaf holding it is marked. The particles' lens flares are seen only so
  (and then by their line to the camera).
- **Decals** (0x453760): a decal's quad (+4; 0xffffffff none) must be marked
  (`AKrEnvironment_GetVisVector` 0x444010).

Live characters aren't culled this way, only by their own frustum test (0x4e27d0).

In the port, the turrets, corpses, objects, weapons and powerups make their lists from their world
boxes as they are drawn. The turrets' and corpses' lists are kept; the others are made each frame. The
weapons' and powerups' boxes are their geometry's bounds under their matrix (inferred: the original
keeps the box with the list).

## The sky

The level's ONSK is ONLV +0x54. `ONrSky_LevelBegin` 0x5105a0 runs from `ONrLevel_Load` 0x5059d0,
just after the camera's reset, into the game state's +0x167714.

| ONSK offset | What |
|---|---|
| +0x00..+0x10 | The skybox's textures: the top, −x, +x, −z, +z |
| +0x18.. | The planets' textures |
| +0x38 | The sun's lens flare texture (flag 0x400 set on it) |
| +0x3c | The stars' texture |
| +0x50 | The planets' count |
| +0x54 | Which planet is the sun (−1 none) |
| +0x58.., +0x78.. | The planets' width and height |
| +0x98.., +0xb8.. | The planets' elevation and azimuth (radians) |
| +0xe0 | The stars' count |
| +0xe4 | The stars' random seed |

The stars: with a count, `UUrRandom`'s seed (0x5326d4) is set from +0xe4 (0x426e00), and each star
takes a brightness (`UUrRandom`) and a direction (0x455990, from `UUrLocalRandom`). Stars that fall on
the skybox's opaque texels are dropped (`ONiSky_SkyboxIntercept`, `ONiSky_TexturePixel` in the Mac build).
Then each kept star takes a twinkle phase (`UUrRandom`). The sun's flare takes 35 pairs of `UUrRandom`
for its occlusion samples. Every sky in the game has no stars and no sun (+0xe0 0, +0x54 −1). So none
of this runs, and the random sequences aren't touched (checked over the 14 levels' ONSKs). The port
leaves it out.

The planets sit at their elevation and azimuth on a sphere of half the camera's far distance
(`M3rCamera_GetStaticData` 0x41b460): (sin az cos el, sin el, cos az cos el) × far / 2.

The skybox: the geometry engine's method +0x4c (0x412e60, `M3rDraw_CreateSkybox`) keeps the five
textures and a cube 5 each way:

- The corners 0 to 3 are the top (y 5: (−5, −5), (5, −5), (5, 5), (−5, 5) in x, z); 4 to 7 the
  bottom, in the same order.
- The faces are the top (3, 2, 1, 0), −x (3, 0, 4, 7), +x (1, 2, 6, 5), −z (0, 1, 5, 4) and +z
  (2, 3, 7, 6). There is no bottom.
- Each face's corners take the coordinates (0, 0), (1, 0), (1, 1), (0, 1) in turn.

`ONrSky_Draw` 0x511160, from the display when the visibility lets it (and not while a skipped
cutscene's screen is black: game state +0x3c set, +0x40 3):

1. It translates the camera's matrix to the camera's location (0x41baa0, 0x41bc40), so the box goes
   round the eye.
2. It draws the skybox (method +0x48, 0x412fc0): each face with a texture, clipped, white, alpha 255.
   Drawn before the environment, it doesn't keep it from showing (the port: no depth test or write).
3. It draws the stars (method +0x44), if any.
4. It draws each planet with a texture as a sprite facing the camera (method +0x34, 0x4118b0): white,
   alpha 255, the width and height as sizes. At half the far distance (5000) a planet of size 1 is
   under a pixel. The game's one planet (the sunset sky's) has 1 and 1, so it can't be seen in the
   original either.

The console variables `sky_show_sky`, `sky_show_planets`, `sky_show_stars` and the like (0x628de4..,
`ONrSky_Initialize` 0x510220) default on. The port has no console variables for them.
