# Environment (AKEV)

A level's fixed geometry is one `AKEV` instance ([instance files](./dat)), the Akira engine's
environment. It is built from **gunk quads**: quads and triangles over one shared point array and one
texture coordinate array. Each quad has a shade per corner (the baked lighting), flags and a texture.
The environment also holds what's built on the quads: their collision planes and boxes, a BSP and an
octree, and the AI's pathfinding nodes. Everything is in the `.dat`, except the editor's debug names,
which are in the `.raw`.

OpenOni reads the environment with `src/akev.c`. `tools/onienv.py` is the reference reader; it exports
OBJ files and draws previews. Worked out from `Oni.exe` v1.0 and checked on all 14 environments of the
CD (level 0 has none): 450,921 quads.

## The instance

Offsets are from the instance data, past the 8-byte header. The layout is the template's, read from the
exe ([instance files](./dat#layouts)).

| Offset | Tag | What |
|---|---|---|
| 0x00 | `PNTA` | Points ([geometry](./m3gm#the-arrays) has the layout) |
| 0x04 | `PLEA` | Plane equations: a, b, c, d (4 floats) |
| 0x08 | `TXCA` | Texture coordinates (2 floats) |
| 0x0c | `AGQG` | The quads (below) |
| 0x10 | `AGQR` | Each quad's texture: a u16 index into `TXMA`, then 2 bytes of padding (`0xdead`) |
| 0x14 | `AGQC` | Each quad's collision data: its plane, a u32 index into `PLEA` (bit 31: the plane is negated), and its bounding box (6 floats) |
| 0x18 | `AGDB` | Per quad, two offsets of names in the `.raw`, the editor's object and file names. `AKrEnvironment_TemplateHandler` 0x440340 turns them into pointers |
| 0x1c | `TXMA` | The textures: an array of `TXMP` links |
| 0x20 | `AKVA` | BNV nodes, the AI's pathfinding volumes (0x74 bytes each; two `.raw` offsets at +0x24 and +0x58): each a BSP root, its sides, its box, and its grid (below) |
| 0x24 | `AKBA` | Sides of the BNV nodes (0x1c bytes: a plane, then the range of its adjacencies) |
| 0x28 | `IDXA` | Quads that scripts refer to... |
| 0x2c | `IDXA` | ...and their script ids, in the same order (`AIiScript_EnvTexSwap` 0x4bcdc0 looks one up and changes the quad's `AGQR` entry) |
| 0x30 | `AKBP` | BSP nodes |
| 0x34 | `ABNA` | BSP nodes of the BNV volumes |
| 0x38 | `AKOT` | The octree (below) |
| 0x3c | `AKAA` | Adjacency between BNV nodes (0xc bytes: the BNV beyond, the ghost or stair quad between) |
| 0x40 | `AKDA` | Door frames |
| 0x44 | | Bounding box of the level: min xyz, max xyz |
| 0x5c | | 24 bytes of `0xdeadde` padding |
| 0x74 | | A float, 12.0 in every level |

All the arrays share the layout of [geometry's](./m3gm#the-arrays): 20 bytes of padding, the u32 count at
0x14, the elements from 0x18. `PNTA` keeps its bounds first, with its count at 0x34.

The points no quad uses (6,770 on the CD) hold -2097152 in all three coordinates. The level's bounds
don't include them.

## Quads (AGQG)

Each element is 0x38 bytes.

| Offset | What |
|---|---|
| 0x00 | 4 point indices (u32) |
| 0x10 | 4 texture coordinate indices (u32) |
| 0x20 | 4 shades, ARGB (u32): the light baked into each corner |
| 0x30 | Flags (below) |
| 0x34 | The object id the editor gave the quad; `0xffffffff` for none |

`AGQR`, `AGQC` and `AGDB` have one entry per quad, in the same order.

**Corners.** A quad with the triangle flag (0x40) has three corners. Its fourth indices repeat the
third. The walks over a quad's corners take 4 less the flag (0x507080, 0x5071e0). The software draw
(`MSrGeomContext_Method_Env_DrawGQList` 0x40ff30) clips a quad as the triangles (0, 1, 2) and (0, 2, 3).

**Facing.** A quad faces along (b − a) × (c − a), from its first three corners. Its collision plane
agrees with this on every quad of the CD.

### Flags

What the exe tests:

| Bit | Quads | Meaning |
|---|---|---|
| 0x00000001 | 1,944 | Door frame |
| 0x00000002 | 2,587 | Ghost: no octree leaf refers to it on the CD (1,532 other quads aren't in the octree either) |
| 0x00000004, 0x00000008 | 356 each | Stairs (up, down) |
| 0x00000020 | run time | Jello: drawn see-through. `CAiTrackJello` 0x4d6b10 sets it on quads in front of the camera's target. `M3rEnv_DrawGQList` 0x41a930 draws these after the solid ones |
| 0x00000040 | 123,886 | Triangle: three corners |
| 0x00000080 | 7,072 | Transparent: `M3rEnv_DrawGQList` leaves these out of the solid and jello draws and marks them in a bit vector (inferred: for the alpha pass, `M3rGeom_Draw_Environment_Alpha` 0x41ab10) |
| 0x00000100 | run time | Set by `P3iCheckCollisions` 0x45eaa0 and 0x4e1900; the software draw (0x40ff30) clears it and flips 0x1000 |
| 0x00002000 | 19,882 | Not drawn (below) |
| 0x00040000 | 0 | Not drawn (below) |
| 0x00200000 | run time | Breakable glass. `AKrLevel_Begin` 0x43ffe0 sets it on every quad whose texture's material is breakable: the textures' materials come from the BINA `TMBD` list in level 0 (0x40-byte entries from +0x10, the count at +0xc: the material's name, then the texture's), bound at level load by `ONrTextureMaterials_LevelLoad` 0x517790. The materials (`Mtrl`, parent link at +8) form a tree under `Default`; `MAiMaterials_Traverse` 0x487f50 makes `Glass` and the materials under it breakable, except `Unbreak_Glass` and those under it. On level 1 this marks exactly the shooting range's 18 targets (confirmed: `particle_test`) |

`AKiEnvironment_RayCastOctTree` 0x4433f0 also tests 0x2a0 and 0x520a0. Bits 0x10, 0x200, 0x800, 0x4000,
0x8000, 0x10000, 0x20000, 0x80000, 0x100000, 0x400000, 0x800000, 0x1000000, 0x2000000, 0x4000000,
0x8000000 and 0x10000000 are set on the CD, but their meanings aren't worked out yet. They are for
collision, the AI and effects.

### What is drawn

`AKiEnvironment_ComputeVis` 0x444030 builds each frame's list of quads from the visibility vector. In
the normal draw state it takes a visible quad only if:

- none of its low four flags is set (door frame, ghost, stairs), and
- neither 0x2000 nor 0x40000 is set.

With bit 1 of the geometry state set (a debug view), it also takes the quads with low flags, visible or
not. The list is sorted by texture (a counting sort over the
`AGQR` index), at most 8,192 quads ("Exceeded max visible GQs"). `M3rEnv_DrawGQList` 0x41a930 then
splits it into three groups:

- solid quads
- jello quads (0x20)
- transparent quads (0x80)

The exports hold the quads this rule draws: 30,555 of level 1's 32,089.

## The octree (AKOT)

`AKOT` links five arrays:

- `OTIT`: interior nodes, 8 children each
- `OTLF`: leaves
- `QTNA`: quadtree nodes
- two `IDXA`: quad lists

A leaf's first u32 packs its quads, as a start in the first `IDXA` (bits 12 to 31) and a count (bits 0
to 11). The rest is 6 neighbour links, then 2 u32s. A child of an interior node has bit 31 set when it
is a leaf (`AKiEnvironment_RayCastOctTree` 0x4433f0). The octree is the renderer's and collision's job (steps 4 and 5).

## The BNV volumes and their grids

| Offset | What |
|---|---|
| 0x00 | The BSP root, into `AKBP` (0xc bytes a node: the plane, the child behind, the child in front) |
| 0x04 | The id |
| 0x08, 0x0c | The sides, first and end, in `AKBA` |
| 0x1c, 0x20 | The grid's width and height |
| 0x24, 0x28 | The grid's run-length data in the `.raw`: offset, size |
| 0x2c | The cell's size (4.0) |
| 0x30 | The box: min xyz, max xyz (the grid's origin is the min) |
| 0x48, 0x4a | The grid's offsets in cells (i16) |
| 0x5c | Flags: 4 a room, 1 stairs |
| 0x60 | Stairs: a plane, then at 0x70 the slab's height |

The grid's data is runs of a byte each: the value in the low 4 bits, the length in the high 4, or in
the next byte when they are 0, row by row (`PHrGrid_Decompress` 0x43c600). An octree leaf's ninth u32
lists its BNVs: the first (bits 8 to 23) and the count (bits 0 to 7) in the octree's second `IDXA`.

## Check

`tools/onienv.py <game>` reads each level over level 0, as the game loads it, so the shared textures
resolve to level 0's. It hashes every quad and then one line per level:

- each quad: corner count, flags, object id and texture index, then per corner the point, the texture
  coordinates and the shade as stored, then its texture's name
- each level: the level and its hash

`akev_test` must print the same: `all: hash=1f7ad149`.

The exports:

- `--obj out/env level1` writes `level1.obj` and `level1.mtl`, with one material per texture, and the
  textures as PNGs in `out/env/level1/`.
- `--png out/env level1` draws three flat views: from above, at an angle, and the plan cut at a fifth
  of the height. Each quad is filled with its texture's mean colour times its shade.
