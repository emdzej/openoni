# Bink movies (BIK, version 'i')

Oni plays two movies, `GameDataFolder/intro.bik` (1,204 frames) and `outro.bik`, through RAD's
`binkw32.dll` (286,208 bytes, CRC-32 6696be85, shipped in the game folder). OpenOni decodes them itself
(`src/bink.c`), ported from that DLL. The DLL's tables (the Huffman decode tables, the scan order, the
run patterns, the quantizers, the audio's bands and exponents) are read from the user's copy at run
time (`src/exe.h`, `dll_*`), as `Oni.exe`'s are. Addresses below are the DLL's (base 0x10000000).

All numbers are little-endian. Status: the layouts are read from the DLL (confirmed); both movies decode
whole (`tests/bink_test.c` hashes the first frames' planes and sound); no reference reader in `tools/`
yet.

## The file

| Offset | Size | What |
|---|---|---|
| 0x00 | 4 | `"BIK"` and the version, `'i'` |
| 0x04 | 4 | File size − 8 |
| 0x08 | 4 | Frames |
| 0x0c | 4 | The largest frame, bytes |
| 0x10 | 4 | Frames (again) |
| 0x14 | 4 | Width (512) |
| 0x18 | 4 | Height (384) |
| 0x1c | 4 | Frame rate numerator (15) |
| 0x20 | 4 | Frame rate denominator (1) |
| 0x24 | 4 | Video flags (0 in both movies: 0x100000 alpha, 0x20000 grayscale, 0x8000 newer colours) |
| 0x28 | 4 | Audio tracks (1) |
| 0x2c | 4 × tracks | Each track's largest packet |
| … | 4 × tracks | Each track: sample rate (u16, 44,100) and flags (u16, 0xe000: 0x2000 stereo, 0x4000 16-bit, 0x1000 DCT instead of RDFT) |
| … | 4 × tracks | Each track's id |
| … | 4 × (frames + 1) | Frame offsets; bit 0 marks a key frame |

A frame is, for each track, a u32 size and that many bytes (when non-zero: a u32 of decoded bytes, then
the packet), then the video data to the next frame's offset (`BinkDoFrame` 0x10009bc0).

## The video

`_ExpandBink@56` 0x100026e0: with flag 0x100000 an alpha plane first (a u32 size before it); without
flag 0x8000 a u32 before the luma plane, the offset (from that u32) of the chroma; the luma plane
(`0x10002860`), then, unless flag 0x20000, the two chroma planes at half size, the second starting where
the first's reader stopped (at the next 32-bit word). The planes decode to the frame buffer not shown
(two of each, swapped per frame); the other is the reference. A buffer is the luma (its stride and rows
twice a chroma plane's: `((w + 1) / 2 + 7) & ~7`), then the two chroma planes: Cr first, then Cb (the
blit's reading; checked on the pictures).

### Bits and bundles

The bits are read from 32-bit words, least significant bit first. A plane has nine *bundles*, streams of
values each with its own Huffman tree and buffer (`0x10003e30` sets them up, `0x10003f00` reads a tree):

| # | Values | Bits | Per block row | Signed |
|---|---|---|---|---|
| 0 | block types | 4 | width/8 | |
| 1 | the scaled blocks' types | 4 | width/16 | |
| 2 | colours | 8 | 64 × width/8 | |
| 3 | patterns | 8 | 8 × width/8 | |
| 4, 5 | motion x, y | 5 | width/8 | yes |
| 6 | intra DC | 11 | width/8 | |
| 7 | inter DC | 11 | width/8 | yes |
| 8 | runs | 4 | 48 × width/8 | |

A bundle's count has `bit_length(0x200 + (width/8) × per − 1)` bits. At each block row, a bundle whose
values are used up (its read pointer at its end) reads a new batch; a count of 0 leaves it empty for the
rest of the plane (the pointer past the end).

A tree (`0x10003f00`): 4 bits pick one of the DLL's 16 trees (the decode tables at 0x1003a030, a byte
per `1 << maxlen` code prefix: the code's length in the high nibble, its index in the low; the max
lengths at 0x100379f0). The symbols the 16 code indices stand for: tree 0, themselves; else a bit: 1, a
3-bit count − 1 and that many 4-bit symbols, the unlisted symbols after them in order; 0, a 2-bit depth
and the identity merged in pairs (a bit each: which of two goes first), then, by the depth, merged up
by 2s, 4s and 8s (`0x10004380`: a bit picks the next from either list until one runs out).

Reading a batch (block types `0x100044a0`, and the same pattern in the others):

- Block types and runs: a bit; 1: one 4-bit value for the whole batch; 0: symbols, where block types
  12–15 repeat the last value 4, 8, 12 or 32 times (0x10037a00).
- Colours (`0x10004a30`): a bit; 1: one colour repeated. Each colour is a high nibble from one of 16
  further trees (read with the bundle, `0x10004440`), picked by the previous colour's high nibble (kept
  across rows), and a low nibble from the bundle's own tree.
- Patterns (`0x10004f10`): two symbols per byte, the low nibble first.
- Motion (`0x10005100`): a bit; 1: one 4-bit value (and a sign bit when non-zero) for all; 0: symbols, a
  sign bit after each non-zero one.
- DC (`0x10005360`): the first value in full (signed: `bits − 1` bits and a sign bit when non-zero), then
  groups of up to 8: a 4-bit length, 0 repeating the last value, else that many bits (and a sign) per
  delta.

### Blocks

Each 8×8 block of a row takes its type from bundle 0 (`0x10002860`):

| Type | What |
|---|---|
| 0 | Skip: the reference's block |
| 1 | Scaled: on even rows only, a 16×16 block from a sub type (bundle 1) drawn at 8×8 and doubled: 3 run, 5 intra, 6 fill, 8 pattern, 9 raw; the next block is covered. On odd rows nothing, two blocks passed. |
| 2 | Motion: the reference's block offset by bundles 4, 5 |
| 3 | Run: 4 bits pick one of 16 scan patterns (0x10037210, 64 bytes each); then until 63 pixels: a bit, 0: the next run + 1 pixels each a colour, 1: one colour for run + 1 pixels; the 64th pixel, if left, a colour |
| 4 | Residue: the motion block plus a residue (7 bits: the changes allowed) |
| 5 | Intra: a DC (bundle 6), the coefficients, 4 bits of quantizer, the IDCT |
| 6 | Fill: one colour |
| 7 | Inter: the motion block plus an IDCT (DC from bundle 7) |
| 8 | Pattern: two colours, 8 pattern bytes; a set bit takes the second |
| 9 | Raw: 64 colours |

The coefficients (`0x10029640`): a 4-bit count of bit planes, and a list of entries of (coefficient,
mode), at first (4,0) (24,0) (44,0) (1,3) (2,3) (3,3). For each bit plane from the top, each entry with
a set bit: mode 0 becomes (c+4, 1) and its four coefficients c..c+3 are each, on a set bit, new entries
(c, 3), else read; mode 1 becomes (c, 2) and adds (c+4, 2), (c+8, 2), (c+12, 2); mode 2 is cleared and
its four read as mode 0's; mode 3 reads that one. A read coefficient is the plane's bits with the top bit
set (just 1 on the last plane) and a sign bit. They come in list order and are placed by the scan order
at 0x1003e7dc. The residue (`0x1002a010`) is the same with (4,0) (24,0) (44,0) (0,2), a 3-bit start
bit, the existing non-zero values growing away from 0 by the bit, new ones set to ± the bit.

The IDCT (`0x10001930`; inter `0x100021c0`): each coefficient times the quantizer's (intra 0x10033208,
inter 0x10035208, 64 ints per quantizer) >> 11, columns then rows, the classic AAN butterflies with
0xb50, 0xec8, −0x14e8 and 0x8a9 (>> 11); a column with only its DC copies it. The output is
`(x + 0x7f) >> 8`, stored as a byte without clamping (it wraps: the original's), added (also wrapping)
to the motion block for inter blocks.

## The audio

A track's decoder (`0x10028890`): a frame of 2,048 samples at 44,100 Hz and over (1,024 from 22,050,
else 512), times the channels (the channels interleaved); an overlap of a 16th; `root = 2 / sqrt(frame)`;
the bands from the critical frequencies at 0x1003e574 (25), each `freq × (frame / 2) / half`, where half
is `(rate × channels + 1) / 2`, while below it.

A block (`0x10028ab0`): two 29-bit floats (5 bits of exponent from the doubles at 0x1003e718, 23 bits
of mantissa, a sign) for the first two coefficients; 8 bits per band, its quantizer
`pow(10, q × 0.0664f)`; then from coefficient 2: a bit, 0: the next 8, 1: 4 bits into the run lengths
at 0x1003e704 (× 8); a 4-bit width (0: all zero), each coefficient that many bits and a sign; the
coefficients times their band's quantizer; the inverse RDFT (`0x100241e0`); to 16 bits, rounded and
clamped (`0x10028eb0`). The packet (`0x100289b0`): the block's start crossfaded linearly over the overlap
with the previous block's end.

The port's inverse transform is its own radix-2 FFT of the same definition (Ooura's `rdft`, isgn −1,
the DLL's `0x100241e0` with its split-radix butterflies): the samples may differ from the DLL's in the
lowest bits.

## The picture

`BinkCopyToBuffer` 0x10009040 picks a blitter by the surface (`_YUV_blit_32bpp` and the others);
`_YUV_init` 0x1000b4e0 makes their tables. The port follows the tables (the C path; the MMX blitters the
DLL may pick round their own way): luma clamped to 16..235, × 0x950a >> 15; each chroma term
(c − 128) × its factor, divided by 32768 toward zero: R gets Cr × 0xcc4b, G gets −Cb × 0x3225 and
−Cr × 0x680f, B gets Cb × 0x10235; the sums clamped to 0..255; one chroma sample for 2×2 pixels.

`BinkWait` 0x1000a3c0: the clock starts at the first frame's end (0x10009b90); frame k is due
`k × 1000 × den / num` ms after it (with a sound track the DLL paces by its sound buffer instead).

The volume (`BinkSetVolume`, the DirectSound backend 0x1002ad40): capped at 0x7fff, then
`v × 127 / 0x7fff` into a table of hundredths of a dB (0x1003e820); the pan at its middle is 0.

## The players

`0x488080` (through `0x4d5580`, ONiMain's `intro.bik` at 0x4d33a4, before the Oni window): `BinkOpen`,
the volume (32768 × the overall volume clamped to 0..1), the window's size (`0x426510`), `BinkClose`;
open again, a `BinkBuffer` centred on the screen, and until the last frame: the events pumped and
dropped (no key ends the intro), `BinkWait`, `BinkDoFrame`, `BinkCopyToBuffer`, the blit (`0x4883e0`),
`BinkNextFrame`. A missing file is logged ("bink movie file (%s) does not exist") and skipped.

`0x488ff0` (through `0x4d56f0`): the player in the 3D display. Level 19 won (0x4d4942): under a 3Dfx
renderer the game only quits, `outro.bik` played by ONiMain's exit (0x4d3180, through `0x4d5580`);
otherwise it plays here first. With an S3 driver it uses a `BinkBuffer` (space ends it); otherwise GL
textures (`0x488410`), the frame at scale 1 and centred: a key down or a mouse button (events 2, 9)
ends it, and since the loop tests for the last frame before `BinkWait`, the last frame is not shown.

The port (`src/app.c`): `movie_play` and `movie_play_in_game`, the frame drawn centred as on a 640×480
screen, the sound into a stream beside the game's channels (`sound_stream_*`).
