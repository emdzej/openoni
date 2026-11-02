# Geometry (M3GM)

A geometry is an `M3GM` instance ([instance files](./dat)): a mesh for characters' body parts, weapons,
power-ups, furniture and effects. The level itself is a separate format, `AKEV`. The mesh's parts are
separate instances, which the geometry links to. Everything is in the `.dat`; nothing comes from the
`.raw`.

OpenOni reads geometries with `src/m3gm.c`. `tools/onigeom.py` is the reference reader; it exports OBJ
files and draws flat-shaded previews. Worked out from `Oni.exe` v1.0 and checked on all 37,862
geometries of the CD.

## The instance

Offsets are from the instance data, past the 8-byte header.

| Offset | What |
|---|---|
| 0x00 | Flags, 0 in every geometry on the CD |
| 0x04 | Link: points, `PNTA` |
| 0x08 | Link: vertex normals, `VCRA`, one per point |
| 0x0c | Link: face normals, `VCRA` |
| 0x10 | Link: texture coordinates, `TXCA`, one per point |
| 0x14 | Link: triangle strips, `IDXA` |
| 0x18 | Link: the face normal of each triangle, `IDXA` |
| 0x1c | Link: the texture, `TXMP`. When it's 0, `M3rGeometry_Draw` 0x41a750 looks one up by name |
| 0x20 | A link of any type, 0 on the CD |

## The arrays

`VCRA` (3 floats per element), `TXCA` (2 floats) and `IDXA` (u32) share one layout:

- 20 bytes of padding
- the u32 count at 0x14
- the elements from 0x18

`PNTA` keeps its bounds first:

| Offset | What |
|---|---|
| 0x00 | 12 bytes of padding |
| 0x0c | Bounding box: min xyz, max xyz |
| 0x24 | Bounding sphere: centre xyz, radius |
| 0x34 | Count |
| 0x38 | The points, 3 floats each |

On the CD, the texture coordinates and vertex normals always number the same as the points.

## Triangle strips

`MSrGeomContext_Method_Geometry_Draw` (0x410930, the method at +0x3c of the software geometry engine's
table) walks the strip array:

- A value with **bit 31 set** starts a new strip. Its low 31 bits and the next two values are the
  first triangle.
- **Every other value** makes a triangle with the two values before it, as (a, b, c), with
  a and b then moving on to b and c.

Two things follow:

- **No winding flip.** The order is not swapped on every other triangle, as GL strips do. The draw
  culls with the face normals: each triangle has one, through the second index array, and that array
  holds exactly as many entries as there are triangles.
- **Points are shared**, between triangles and between strips.

On the CD every index is within the points, and every triangle count equals its face normal count.

## Check

`tools/onigeom.py <game>` decodes every geometry. It hashes one line per geometry (index, name, point
count, triangle count, CRC of the points, UVs, vertex normals, triangles and face normal indices) and one
per level. `m3gm_test` must print the same: `all: hash=8d974ee1`.

`--obj out/geom NAMES` and `--png out/geom NAMES` export single geometries.
