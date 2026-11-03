# Fonts (TSFF, TSFT, TSGA, TSFL)

Oni draws its text with bitmap fonts of its text system, `TS` (Bungie's `BFW_TextSystem`). The
templates are registered by `TSrRegisterTemplates` 0x42f250. There are four kinds of instance
([instance files](./dat)):

- `TSFF`, a font family: its fonts and its language
- `TSFT`, a font: one size and style, its metrics, its glyph arrays and its glyphs' bitmaps
- `TSGA`, a glyph array: 256 glyphs
- `TSFL`, a font language: the line breaking characters

The CD has one family, `TSFFTahoma`, in `level0_Final.dat`: 15 fonts, sizes 7, 9, 10, 12 and 14 in
three styles, 182 glyphs each, 2,730 in all. The other levels link it by name. OpenOni reads fonts with
`src/font.c`; `tools/onifont.py` is the reference reader. Worked out from `Oni.exe` v1.0 and checked on
every font of the CD.

## The family (TSFF)

Offsets are from the instance data, past the 8-byte header.

| Offset | What |
|---|---|
| 0x00 | 16 bytes, unused (Bungie's `0xdead` fill) |
| 0x10 | Link to the `TSFL` |
| 0x14 | The count of fonts (u32) |
| 0x18 | A link to a `TSFT` per font |

`TSrFontFamily_Get` 0x42ee60 finds a family by name. `TSrFontFamily_GetFont` 0x42ee80 walks the list
for the first font of a size and style, passing over empty links. It finds nothing if there is no exact
match.

A text context picks its font with `TSiContext_GetBestFont` 0x42ebd0, whenever its family, size or
style changes. For each size it tries, it takes the asked style, else the plain style (0). If it falls
back to plain, the context keeps style 0. The sizes go:

1. the asked size, then each size below it, down to 1
2. the asked size again, then each size above it, up to 18
3. none: an error

A size above 17 that isn't found going down is not tried going up. `TSrContext_GetFont` 0x42e8c0 has no
fallback: it takes the context's size and the given style, and style 3 means the context's own font.

## The font (TSFT)

| Offset | What |
|---|---|
| 0x00 | 6 bytes, unused |
| 0x06 | Size in points (u16). 7, 9, 10, 12, 14 on the CD |
| 0x08 | Style (u32). 0, 1 or 2 on the CD |
| 0x0c | Ascending height (u16): `TSrFont_GetAscendingHeight` 0x42eec0 |
| 0x0e | Descending height (u16): `TSrFont_GetDescendingHeight` 0x42ef60 |
| 0x10 | Leading (u16): `TSrFont_GetLeadingHeight` 0x42ef30 |
| 0x12 | u16, equal to the descending height on the CD. No reader found |
| 0x14 | 256 links to `TSGA`, by the character's high byte. The CD's fonts have only the first |
| 0x414 | The size of the bitmaps, in 4-byte words (u32) |
| 0x418 | The bitmaps |

`TSrFont_GetLineHeight` 0x42ef40 is the ascending height, the descending height and the leading added
up. On the CD, size 14 has 19 + 4 + 3 = 26.

**The styles.** Inferred from the glyphs: style 1 is bold, since its glyphs are wider. Style 2 has style
0's metrics but other bitmaps, which suggests italic. Bungie's names for them aren't in either exe.

## The glyph array (TSGA)

There are 256 glyphs of 0x14 bytes from the start of the data. Glyph `lo` of the array linked at `hi`
is character `hi << 8 | lo`. An unused slot is all 0.

| Offset | What |
|---|---|
| 0x00 | The character (u16). On the CD it always equals the slot |
| 0x02 | Advance (s16): how far the pen moves |
| 0x04 | Bitmap width (u16) |
| 0x06 | Bitmap height (u16). `TSrFont_GetCharacterSize` 0x42eed0 returns these two |
| 0x08 | x (s16): the bitmap's left edge is at the pen's x less this. 0 on the CD |
| 0x0a | y (s16): the bitmap's top is at the pen's y (the baseline) less this. The ascending height on the CD |
| 0x0c | The bitmap's offset from `TSFT` 0x418, in 4-byte words (u32) |
| 0x10 | Run time: the glyph's cell in the texture cache. 0 in the files |

**Bitmaps.** A glyph's bitmap is width × height bytes, row by row, with the width as the stride. It
starts at the font + 0x418 + 4 × the word offset. Each byte is how much of the background to keep: 0
where the glyph is inked and 255 where it is clear, with antialiased edges in between. The coverage is
therefore 255 minus the byte. On the CD the bitmaps are as tall as the ascending and descending heights
together, so every glyph of a font has the same height.

`TSiGlyph_Draw` 0x42cc40 blends a glyph into the texture of the glyph cache
(`TSiTextureCache_RenderGlyph` 0x42cb80). It clips the glyph to the texture first. Then, per channel,
the result is (under × byte + colour × (255 − byte)) >> 8. Which channels it blends depends on the
texture's pixel type:

- `ARGB4444`: all four channels
- `RGB555`: the three colour channels
- `ARGB1555`: the three colour channels, keeping the alpha bit
- `I8` and `A8`: the one byte
- `A4I4`: the two nibbles
- `ARGB8888`: all four bytes

## The language (TSFL)

There are five 0-terminated strings of 64 bytes. For `TSFLRoman` on the CD:

| Offset | Value | Use |
|---|---|---|
| 0x00 | 0xc9 | Probably the ellipsis (… in Mac Roman). Inferred |
| 0x40 | a space | The breaking characters: `TSrContext_FormatString` 0x42e210 wraps a line after the last one that fits |
| 0x80 | `;,:./-` | Inferred: characters a line may break after |
| 0xc0 | `($[´{` | Inferred: characters a line may not end with |
| 0x100 | `!%),.?]}` | Inferred: characters a line may not start with |

Only the string at 0x40 has a reader found so far.

## Characters

Every TS reader steps through a string the same way (`TSiString_GetNextCharacter`, inlined, with
`TSiCharacter_IsDoubleByte` 0x42f230):

- A byte with bit 7 set, other than 0xff, starts a two-byte character. That byte is the high byte.
- Every other byte is a character by itself. 0 ends the string.

The exe has no switch for this. On the US CD, whose fonts have only the glyph array of high byte 0, a
byte from 0x80 to 0xfe therefore takes the next byte with it and draws nothing. The glyphs 0x80 to 0xfe
of that array are never reached. Their shapes follow the **Mac Roman** layout: 0x80 is Ä, 0x8e é, 0xc9
…, 0xe9 È. This is inferred from the glyph sheet (`onifont.py --png`). The CD's texts hardly use such
bytes: there is one, a 0x85, in two strings.

The original steps over both bytes even when the second is the terminator, and then reads on past the
string. OpenOni's walk stops at the terminator instead.

`TSrString_GetLength` 0x42f090 counts the characters. `TSrString_GetCharacterAtIndex` 0x42f030,
`TSrString_DeleteChar` 0x42ef70 and `TSrString_InsertChar` 0x42f0d0 edit strings character by
character, for the menus' text fields.

## Measuring

`TSrContext_GetStringRect` 0x42e920 gives a string's rectangle with its context's font:

- left and top 0
- the width: the glyphs' advances added up in 16 bits. A character with no glyph array adds nothing,
  and there is no kerning
- the height: the line height

`TSrContext_FormatString` 0x42e210 lays out text for `TSrContext_DrawFormattedText` 0x42d9e0. It
adds up the same advances, and it:

- breaks lines at the family's breaking characters
- takes at most 255 characters a line and 24 lines (as read from the loop, not checked yet)
- aligns the lines by the context's format flags
- reads `[` + a letter as a colour code and `]` as the end of one, from a table in the exe at 0x533da0

The training messages use these codes (`[c.` ...`]`). Porting it is left to the menus (WM).

**A quirk of the draw**, for the renderer. `TSiContext_DrawLine` 0x42d860 moves the pen for a glyph
that ends left of the clip rectangle, but not the string pointer (0x42d95c jumps past the store at
0x42d9ba). The first character is then measured again until the pen reaches the rectangle. The loop
still counts each pass as a character, so as many characters go missing from the line's end. It stops
at the first glyph that starts right of the texture.

## Check

`tools/onifont.py <game>` hashes the fonts with FNV-1a (32 bits), along with the text instances
([text](./text)). The lines it hashes are in its docstring: every font's metrics, then each glyph's
fields and bitmap bytes. `font_test` must print the same lines:

```
TSFF: families=1 fonts=15 glyphs=2730 hash=716a7765
widths: 7/1=147 7/0=123 7/2=123 14/1=289 14/0=256 ...
best: 7/0 7/0 ...
all: hash=85c7502f
```

`widths` is each font's width of "The quick brown fox, Konoko!". `best` is the best-font lookup for
sizes 0 to 20 in each style. `onifont.py <game> --png Tahoma 14 out/tahoma14.png [style]` writes a font's
glyph sheet: high byte 0, 16 × 16 cells, ink black.
