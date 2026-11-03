# Textures (TXMP)

A texture is a `TXMP` instance ([instance files](./dat)) whose pixels are in the level's `.raw`.
OpenOni decodes them with `src/txmp.c` and `src/im.c`; `tools/onitex.py` is the reference decoder, and
it exports PNGs. Worked out from `Oni.exe` v1.0 and checked on every texture of the CD: 8,918 textures in
15 levels, all their mip levels.

## The instance

Offsets are from the instance data, past the 8-byte header. `M3rTextureMap_New` 0x41eb00 builds
textures at run time with the same layout. Its size is 0xa4.

| Offset | Size | What |
|---|---|---|
| 0x00 | 128 | Debug name, usually empty |
| 0x80 | 4 | Flags (below) |
| 0x84 | 2 | Width |
| 0x86 | 2 | Height |
| 0x88 | 4 | Texel type (below) |
| 0x8c | 4 | Link: the `TXAN` animation it belongs to |
| 0x90 | 4 | Link: the environment map, a `TXMP` |
| 0x94 | 4 | Offset of the pixels in the `.raw`. At run time 0x41e9d0 adds the base of the loaded `.raw` (`TMrInstance_GetRawOffset` 0x423e10) |
| 0x98 | 4 | The Mac version's `.sep` offset, unused on Windows |
| 0x9c | 4 | Run time: the renderer's texture |
| 0xa0 | 1 | Run time: a dirty flag the GL back end sets |

**Flags:**

| Bit | Meaning |
|---|---|
| 0x0001 | Mipmaps follow the image |
| 0x1000 | The pixels are already in little-endian order. All of the CD's are; without it, 0x41e9d0 swaps them |
| 0x2000 | Run time: the raw offset has been turned into a pointer |

The other bits belong to the renderer (step 4).

On the CD:

| Flags | Textures |
|---|---|
| 0x1001 | 5,052 |
| 0x154d | 1,255 |
| 0x1211 | 353 |

## Texel types

The exe's pixel type table at 0x529e78 has 15 entries of 0x18 bytes each:

- the type number
- a flag
- bytes per pixel
- the A, R, G and B masks

OpenOni reads the bytes per pixel from it at run time.

| Type | Format | Bytes per pixel | On the CD |
|---|---|---|---|
| 0 | ARGB4444 | 2 | 1,197 |
| 1 | RGB555 | 2 | 2,576 |
| 2 | ARGB1555 | 2 | 7 |
| 3 | 8-bit intensity | 1 | |
| 4 | 1-bit | (w + 15) / 8 per row | |
| 5 | 8-bit alpha | 1 | |
| 6 | A4I4 | 1 | |
| 7 | ARGB8888 | 4 | |
| 8 | xRGB8888 | 4 | 65 |
| 9 | DXT1, 4×4 blocks of 8 bytes | | 5,073 |
| 10, 11 | RGB bytes, RGBA bytes (conversion targets) | 3, 4 | |
| 12 | RGBA5551 | 2 | |
| 13 | RGBA4444 | 2 | |
| 14 | RGB565 | 2 | |

## Size and mipmaps

`IMrImage_ComputeSize` 0x4478d0 gives the size of one level:

- 1 byte per pixel: rows are padded to even widths.
- DXT1: ((w + 3) & ~3) × ((h + 3) & ~3) / 2. A 2×2 or 1×1 level is still one 8-byte block.

With mipmaps, the levels follow the image, largest first. Each level halves both sides and clamps each
to 1, until both sides were 1. The levels are packed with no padding between them. Textures in the `.raw`
start on 32-byte boundaries.

## Conversion to RGBA

`IMrImage_ConvertPixelType` 0x44ac20 converts level by level, through a table of (from type, to type,
function) at 0x535a90. OpenOni ports the conversions to RGBA bytes, which is what the exporters and the
renderer use. They are not all the obvious ones:

- **ARGB4444** (0x44b020): every nibble repeated (×17).
- **RGB555** (0x44afa0): shifted up by 3, with the low bits left 0, so white is 248, not 255. Alpha is 255.
- **ARGB1555** (0x44b0b0): the top bits repeated below (`c << 3 | c >> 2`). Alpha is 255 if bit 15 is
  set, else 0.
- **xRGB8888** (0x44b250, to RGB bytes): the bytes as they are. Alpha is 255.
- **DXT1** (`IMiConvert_DXT1_to_NByte` 0x449650, `DecodeDXT1Block_To_RGBABytes` 0x44a8a0): S3TC
  blocks, decoded as follows.
  - Blocks run left to right, top to bottom. Each holds two RGB565 colours, then 2-bit indices, one byte
    per row, pixel x at bits 2x.
  - The end points are widened by a shift only (`r << 3`, `g << 2`).
  - If c0 > c1, the other two colours are (2·c0 + c1)/3 and (c0 + 2·c1)/3. Red and blue add 1 before
    dividing; green doesn't.
  - Otherwise the third colour is (c0 + c1)/2, with red and blue adding 1, and **the fourth is opaque
    black**. Standard DXT1 makes it transparent.
  - In a level smaller than 4×4, only the pixels inside it are written.

## Check

`tools/onitex.py <game>` decodes every level of every texture. It hashes one line per texture (index,
name, size, type, flags, CRC of the RGBA bytes) and one per level. `txmp_test` must print the same:
`all: hash=69e02682`.

`tools/onitex.py <game> level0 --png out/tex [names]` writes the top level of each texture as a PNG.

The HUD's `right` and `left` (level 0) are RGB555 pictures on black, flags 0x1400 (0x400 not known);
the port draws them added to the screen (inferred: the draw states the HUD sets aren't decoded yet).
`rightRing` and `leftRing` are ARGB4444 rings the health and weapon arcs sample.
