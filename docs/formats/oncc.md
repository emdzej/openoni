# Characters (ONCC, TRBS, TRCM)

A character class is an `ONCC` instance ([instance files](./dat)), 3,232 bytes: Konoko, the Strikers,
the cops. For now OpenOni reads its body: the meshes, the bones and how they hang together. The rest
(physics, combat, AI, sounds) comes with the character engine (step 5).

OpenOni reads bodies with `src/body.c`. `tools/onibody.py` is the reference reader; it exports a body
posed by an animation frame as OBJ or a preview. Worked out from `Oni.exe` v1.0 and checked on all 29
classes of level 1, five bodies each.

## The class

Offsets are from the instance data. The links the body needs:

| Offset | Tag | What |
|---|---|---|
| 0xc34 | `TRBS` | The body set: five `TRCM` links, lowest detail first |
| 0xc38 | `TRMA` | A texture per body part |
| 0xc80 | `TRAC` | The animation collection |
| 0xc84 | `TRSC` | The aiming screens |

The class also links `TXMP` (0x20), `ONCV` (0x42c), `ONCP` (0x430), `ONIA` (0x434), `CBPM` (0xc3c) and
`CBPI` (0xc40).

The AI's senses, read by its sight check (0x49adf0) and its memory (0x49b3f0, 0x499e80):

| Offset | What | Level 1's thugs |
|---|---|---|
| 0x120 | Flags: bit 0, never startled | 0xa |
| 0x3f4 | Central sight distance | 475 |
| 0x3f8 | Peripheral sight distance | 150 |
| 0x3fc | Vertical sight, a cosine from straight up or down | 0.766 |
| 0x400, 0x404 | Central sight: cosines of full range and of none | 0.819, 0.574 |
| 0x408, 0x40c | Peripheral sight: the same | 0.174, −0.423 |
| 0x410, 0x414, 0x418 | How long a contact of strength 4, 3, 2 lasts, in ticks | 180, 900, 3600 |

## The body (TRCM)

| Offset | What |
|---|---|
| 0x04 | Bones (u16), 19 on the CD |
| 0x54 | `TRGA`: a geometry ([M3GM](./m3gm)) per bone |
| 0x58 | `TRTA`: each bone's offset from its parent (x, y, z floats) |
| 0x5c | `TRIA`: the hierarchy, 4 bytes per bone: parent, first child, next sibling, padding (`0xde`) |

When the body is loaded (0x4285e0), its run-time fields at 0x48, 0x4c and 0x50 are set to the elements of
those three arrays. `TRGA`, `TRTA`, `TRIA` and `TRMA` have their u16 count at 0x16 and their elements
from 0x18.

Each bone's geometry is in the bone's own space. In the rest pose (no rotations) the bones lie along a
line.

## Posing

`iBuildMatriciesRecursive` 0x427570 builds a matrix per bone, from bone 0 with the character's matrix as
its parent. It walks down the first children in a loop and recurses for the next siblings. Each bone's
matrix is:

1. its parent's matrix
2. translated by the bone's offset, in the parent's space (0x434c90)
3. times the bone's rotation from the animation (`MUrMatrixStack_Quaternion` 0x434c00)

Matrices are 3×4, stored by columns, with the translation last. `MUrQuatToMatrix` 0x4338c0 writes the
rotation's columns, scaling by 2/|q|². Its first column is (1 − 2(y² + z²), 2(xy − zw), 2(xz + yw)): the
transpose of the usual matrix for (x, y, z, w).

## Check

`body_test` builds every class's five bodies on level 1. It then poses Konoko's highest-detail body by
`KONOKOidle_spec3` at frame 37 and prints the matrices, as `tools/onibody.py game level1 --matrices
konoko_generic KONOKOidle_spec3 37` does (to 3 decimals).

`--obj out/body konoko_generic KONOKOidle_spec3 37` exports that pose, and `--png` draws it.
