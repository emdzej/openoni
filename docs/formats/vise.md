# Installer VISE archive

The Oni CD (Windows, v1.0, volume `001228_1801`) has no game files in the open: everything is inside a
MindVision **Installer VISE** (Windows) archive, `ONISETUP.EXE` plus `ONISETUP.002`. OpenOni reads it with
`src/vise.c` (used by `openoni-extract`); `tools/vise.py` is the reference reader. The same reader
exists as a standalone MIT-licensed Go tool, [visextract](https://github.com/emdzej/visextract). Worked out from the CD
and the setup program; every file record carries a CRC-32, and all 95 files match theirs.

All numbers are little-endian.

## Segments

| File | Segment | What |
|---|---|---|
| `ONISETUP.EXE` (515,728 B) | 1 | A small MSVC setup program; its PE overlay (from 0x11000) is segment 1 |
| `ONISETUP.002` (348,137,970 B) | 2 | The compressed files, back to back |

Every segment starts with a 16-byte header: `"ESIV"` (0x56495345), an archive id (`0x3a07bc24`, the same in
both), the segment number, and a u32 (in segment 1: the segment's length; in segment 2 a value of unknown
meaning). The setup program ends with `"ESIV"` + u32 = the file offset of segment 1. That is how it finds its
own overlay (`0x401a80`: seek to end - 8, check the magic, read the offset).

File offsets in the item list are offsets in the segment file, header included: the first file in
`ONISETUP.002` starts at 0x10. The last file ends exactly at the end of `ONISETUP.002`.

## Segment 1

After the header:

1. Three strings, each with a length byte. The second one's length byte keeps only 6 bits (`& 0x3f`).
2. The setup program's own support files: u16 count, then for each: name (length byte + bytes), DOS date
   (u16), DOS time (u16), two u32 (0), the compressed size (u32), the compressed data. On the Oni CD there
   are four: `vise32ex.dll` (the installer engine; the setup program loads it and calls `ViseMain`),
   `English.vlg` (UI strings), `dsetup.dll` and `jpeg.dll`. Loop at `0x4016a0`.
3. The installer's data: dialogs, strings, registry keys, shortcuts, and the **item list**.

## Item list

Each item has the same frame:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | type: 1 folder, 2 file (others: shortcuts, registry entries, actions; ignored) |
| 0x04 | 4 | id |
| 0x08 | 4 | parent id (0 = the install folder itself) |
| 0x0c | 0x3a | (not needed) |
| 0x46 | 32 | 0xff bytes |
| 0x66 | 1 | 0 |
| 0x67 | 2 | name length |
| 0x69 | n | name |
| 0x69+n | ... | body, by type |

The bodies differ in length by type, so the reader finds items by their 32 × 0xff run followed by
`00`, a length and a printable name, then reads the frame back from 0x46 bytes before the run. Parents
come before their children. Paths are built from the parent ids; the item ids are not consecutive
(the installer numbers them when they're added).

File body (offsets after the name):

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | 0 |
| 0x04 | 4 | uncompressed size |
| 0x08 | 4 | compressed size |
| 0x0c | 4 | segment (2 = `ONISETUP.002`) |
| 0x10 | 4 | offset in the segment file |
| 0x14 | 4 | attributes (0x21 = archive + read-only) |
| 0x18 | 8 | FILETIME (creation, probably) |
| 0x20 | 8 | FILETIME |
| 0x28 | 8 | FILETIME, last write (UTC; equals the date on the CD for `readme.txt`) |
| 0x30 | 4 | DOS date, DOS time of the same moment (local time) |
| 0x49 | 4 | CRC-32 (zlib's) of the uncompressed file (unaligned) |

The folder body is 0x19 bytes and holds nothing the extractor needs.

## Compression

Each file (and each support file) is one stream: **plain deflate** (RFC 1951), stored in 16-bit
little-endian words whose two bytes are swapped. Swap every pair of bytes (pad an odd length with a 0)
and inflate as raw deflate; `src/inflate.c` (`inflate_vise`) does that, and `tools/vise.py` uses
Python's `zlib`.

The evidence, from the setup program's decoder (`0x401ee0`: modes 1 init, 2 compress, 3 decompress,
4 free; the compressor still has zlib's debug strings):

- the bit reader exchanges the bytes of each 16-bit word before taking bits LSB-first (`0x4037e0`);
- its distance table ends at 24577 and its length table at 258: a standard 32 KB window (the 64 KB
  buffers it allocates are I/O buffers);
- swapped and inflated by zlib, all 95 files and the four support files decode to their CRC-32s.

The block dispatcher (`0x404d70`) rejects type 1 (fixed codes) and has a stored-block path (`0x404b80`).
Only dynamic blocks occur on the Oni CD.

## What the archive installs

95 files, 641,025,194 bytes (`openoni-extract --summary`: `hash=9b4bdbfa`):

```
Oni.exe            1,527,808   CRC a8223704
binkw32.dll          286,208
readme.txt, ONI.url
GameDataFolder/
  level{0,1,2,3,4,6,8,9,10,11,12,13,14,18,19}_Final.{dat,raw}
  intro.bik, outro.bik
  IGMD/<15 level folders>/*.bsl    (global/ is empty)
```

The other items are the Start-menu shortcuts (`Oni`, `Read Me`, `Bungie's ONI Web Page`), the registry
key `Software\Bungie\Oni` (`Path` = `%TargetDir%`) and the optional GLSetup / DirectX runs. None of them is
needed to play.
