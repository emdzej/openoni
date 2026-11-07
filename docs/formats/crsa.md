# CRSA: the corpses

Each level file holds one unnamed `CRSA` instance, 22048 bytes in every level (20 slots). The game keeps its
pointer at GS+0x2f8 and draws the corpses before the characters (0x4e1540). Status: read from the exe and
the level files (confirmed). Reader: `src/corpse.c`.

Offsets are from the instance's data, after its 8-byte header.

| Offset | Size | What |
|---|---|---|
| 0x00 | 0x0c | Padding (`0xdeadde`) |
| 0x0c | u32 | The level's own corpses (the first slots): 1 in level 1, 17 in level 3, 0 in level 12 |
| 0x10 | u32 | The next slot for a character's corpse (as saved: the count at 0x0c) |
| 0x14 | u32 | The slots (20) |
| 0x18 | 0x44c × slots | The slots |

A slot (0x44c bytes):

| Offset | Size | What |
|---|---|---|
| 0x000 | 0x20 | The name: the file it was made from (`_lvl_3_Intro_TCL_A_corpse.dat`), `dynamic_corpse` for a character's |
| 0x020 | 0x80 | The octree nodes its box lies in (0x440900: a count, then the node indices; −1 counts as always seen) |
| 0x0a0 | link | The character class (`ONCC`); 0 for an empty slot |
| 0x0a4 | 19 × 0x30 | The body parts' matrices (4×3, the translation last), as the character's A+0x2278 |
| 0x434 | 6 floats | The box: min x, y, z, max x, y, z (the character's C+0x1648) |

## In the game

- `ONrCharacter_LevelBegin` 0x4d87b0 → 0x4d9d50: the slots after the level's own are cleared and the next
  slot is set to the first of them.
- A dead AI character (C+4 flag 4, its dying over: on the ground for 120 ticks, 0x78) is deleted once its
  corpse time (C+0x1644, the class's ONCC+0xc8a) is out, unless its shield or invisibility still shows,
  C+4 has 0x400, or the character is seen at a detail above the lowest (A+0x221c not 6 nor 0). Its body
  goes into the next slot first (0x4ef340: the name, the class, the matrices, the box, its octree nodes),
  unless it fell for 330 ticks (0x14a) or took more than 900 (0x384) to die (C+4 flag 0x80000). The next
  slot wraps to the first after the level's own.
- `trigvolume_corpse` (0x4dcbd0) empties the characters' slots whose box's diagonal meets the volume
  (`OBJrTriggerVolume_IntersectsBBox_Approximate` 0x4cc630).
- 0x4e1540 draws each slot with a class: a name starting with `_` only from the graphics quality 1
  (0x508540); the class's first body (`TRBS` +0), from the quality 3 (0x508550) its second; the textures
  from the class (0x427da0); only if one of its octree nodes is visible (0x440b50). The port draws them
  all (it has no octree visibility yet).
