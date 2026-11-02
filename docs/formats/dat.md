# Instance files (.dat, .raw)

Everything the game loads, from textures and models to characters, animations, dialogs and the level
geometry, is an **instance** of a **template**: a struct type named by a four-character tag (`TXMP` a
texture, `M3GM` geometry, `ONCC` a character class). Each level's instances are in
`GameDataFolder/levelN_Final.dat`, and the bulk data they point to (pixels, sound samples) is in the
`.raw` file of the same name. Level 0 holds the shared data (menus, fonts, Konoko), and stays loaded under
the others.

OpenOni reads these files with `src/tm.c`, a port of Bungie's instance manager `TM` (`TMrInitialize`
0x41ff40, `TMrLevel_Load` 0x4217d0). `tools/onidat.py` is the reference reader. Worked out from `Oni.exe`
v1.0 and checked on all 15 levels of the CD. All numbers are little-endian; the original also reads
big-endian files (version swapped), which the CD doesn't have.

## Header (0x40 bytes)

Checked by 0x420ff0.

| Offset | Size | What |
|---|---|---|
| 0x00 | 8 | Checksum of the template set the file was built with |
| 0x08 | 4 | Version, `0x56523331` (`"13RV"` in the file) |
| 0x0c | 2 × 4 | Record sizes: header 0x40, instance descriptor 0x14, template entry 0x10, name entry 0x08. Anything else is refused |
| 0x14 | 4 | Instance count |
| 0x18 | 4 | Name directory count (the named instances) |
| 0x1c | 4 | Template entry count |
| 0x20 | 4 | Data block: offset |
| 0x24 | 4 | Data block: size |
| 0x28 | 4 | Name block: offset |
| 0x2c | 4 | Name block: size |
| 0x30 | 16 | Not read by the loader |

The tables follow the header in this order:

1. The instance descriptors.
2. The name directory.
3. The template entries.

The data block and the name block come after them.

## Instance descriptor (0x14 bytes)

| Offset | What |
|---|---|
| 0x00 | Template tag (`'TXMP'` as a u32) |
| 0x04 | Data offset, from the data block. It points past the instance's 8-byte header |
| 0x08 | Name offset, from the name block |
| 0x0c | Size, header included. It is normally the layout's end padded to 32 bytes. Some `TXCA` and `PNTA` instances are bigger, and the spare space is filled with `0xdeadde` |
| 0x10 | Flags. The loader keeps the low 16 bits: 1 = no name, 2 = placeholder (no data), 4 = not walked at load (never set on the CD). Bit 8 is set on many unnamed instances, purpose unknown. The high bits (0x10, 0x30) are dropped |

Names include the tag in front: `TXMPNONE`. A **placeholder** is a named instance with no data. It
stands for an instance of the same name in another file, usually level 0.

The 8-byte instance header holds two values:

- The instance id, `index << 8 | 1`.
- The file id. The loader replaces it with the file's pointer; OpenOni uses its slot number instead.

## Name directory (8 bytes each)

Each entry holds an instance index and a name offset. The entries are sorted by name in byte order, so
lookups are binary searches (0x423160, `TMrInstance_GetDataPtr` 0x4232e0).

## Template entry (0x10 bytes)

Each entry holds the template's checksum (8 bytes), its tag and its instance count. At start-up,
`TMrGame_Initialize` 0x4209e0 checks every level file this way: each template it uses must exist in the
exe with the same checksum, or the level is invalid ("level %d: template %c%c%c%c checksum ...").

## File names and ids

`TMrUtility_LevelInfo_Get` 0x420590 reads `level<N>_<suffix>.dat`. N is at most 127.

- For the suffix `Final`, the file is "final" and the suffix hash is 0.
- For other suffixes, the hash is the sum of (uppercase character − 0x40) × position, with positions
  starting at 1.

The file id is `((hash & 0xffffff) | N << 24) << 1 | 1`, so level 0's file id is 1. Files ending in
`tools.dat` are skipped.

## Templates and layouts (in the exe)

The file doesn't describe its structs: their layouts are compiled into `Oni.exe`.

**The template table.** It holds 114 templates, 0x30 bytes each; 0x558cf0 holds the count.

| Offset | What |
|---|---|
| 0x00 | Checksum (8 bytes) |
| 0x08 | Tag |
| 0x0c | Name ("Texture Map") |
| 0x10 | The layout |
| 0x14 | Flags |
| 0x18, 0x1c | Sizes checked by `TMrTemplate_Register` |

The layout pointers are not in the table. The start-up code at 0x51d1c0 stores them, one `mov dword [mem],
imm32` per template, and then stores the table's base at 0x620138. OpenOni reads those instructions'
operands at run time to find them.

**Template flags.** Bit 2 means no links anywhere: the loader doesn't walk the template's instances. Bit
4 means no links in the variable array: the walk stops at it. The layouts agree with both flags.

**The layout code.** A layout is a byte code walked by 0x421c50:

| Opcode | Operands | What |
|---|---|---|
| 1, 2, 3, 4 | | A field of 8, 4, 2 or 1 bytes |
| 5 | count (u8) | A fixed array: the element's layout follows, up to opcode 6 |
| 6 | | End of an array element, or of the layout |
| 7 | kind (u8) | A variable array, after a count field: kind 1 = u32 padded to 8 bytes, 2 = u32, 3 = u16. The element's layout follows, up to opcode 8. An empty array's element layout is skipped (0x420390) |
| 8 | | End of a variable array element |
| 9 | tag (u32) | A link to an instance of that tag (0 = any) |

Every layout starts with `2 2`, the 8-byte instance header. Opcode 10, which the byte-swapper also
handles as a 4-byte field, doesn't occur in any layout.

## Links and the load

A link field in the file holds the target's instance id (`index << 8 | 1`), or 0 for no link.

`TMrLevel_Load` loads a level's files, then walks each file once. The original stores the target's data
pointer in the field; OpenOni stores a 32-bit handle (slot << 24 | index), so the 64-bit test build works
the same way.

- **Unnamed target:** the link becomes the target's data.
- **Named target:** the target is looked up by name in every loaded file, from the first loaded on. The
  original's slots 0 and 1 are the dynamic instance files, so the search starts at slot 2. A file with
  only a placeholder is passed over.
  - So a level's own copy of a level 0 instance gives way to level 0's.
  - A name no loaded file has data for leaves the field 0.
- **Load order matters.** Level 0 is walked when it is loaded on its own. Its links to placeholders stay
  0, even when a later level has those names.

| Level | Instances | Placeholders | Links (with level 0) | Into level 0 | Unresolved |
|---|---|---|---|---|---|
| 0 | 9,347 | 43 | 5,539 | – | 69 |
| 1 | 9,356 | 39 | 26,917 | 2,335 | 70 |
| 8 | 13,501 | 73 | 40,303 | 4,177 | 75 |
| 19 | 12,633 | 79 | 35,008 | 3,619 | 81 |

**.raw offsets** are plain 4-byte fields in the layouts. Each template's own code reads them, from the
`.raw` next to the `.dat` (0x420720 opens it). They come with the templates that use them, in step 3.

## Check

`tools/onidat.py <game>` walks every instance of every level by its layout. The walk stops on any field
that runs past the instance and on any link to the wrong tag. It then hashes the result, and
`dat_test` must print the same lines. On the CD:

- 15 levels.
- 155,232 instances.
- 358,404 links, all of the right tag.
- `all: hash=66db30fb`.

`--load` adds the counts of the load as described above. These match `dat_test` too.
