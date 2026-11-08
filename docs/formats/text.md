# Text (SUBT, IGSt, IGSA, IGPG, IGPA, IGHH, StNA, TStr)

Oni's text is in instances of several templates ([instance files](./dat)). The pages are drawn with the
[fonts](./font). These exist on the CD, counted over all levels:

| Tag | What | Where | On the CD |
|---|---|---|---|
| `SUBT` | Subtitle array: name and text pairs in the `.raw` | level 0 | 2 arrays, 756 entries |
| `IGSt` | In-game UI string, with its font | every level | 1,213 |
| `IGSA` | Array of `IGSt` | every level | 309 |
| `IGPG` | In-game UI page: font, image, two `IGSA` | every level | 212 |
| `IGPA` | Array of `IGPG` | levels 1 to 19 | 70 |
| `IGHH` | The HUD help overlay | level 0 | 1 |
| `StNA` | Array of `TStr` | level 0 | 3 arrays, 327 strings |

Links to these come from other templates:

- `TxtC` (text console): one `IGPA`
- `DPge` (diary page), `HPge` (help page), `IPge` (item page), `WPge` (weapon page): one `IGPG` each

OpenOni reads text with `src/text.c`; `tools/onifont.py` is the reference reader. Offsets are from the
instance data, past the 8-byte header. Texts are 0-terminated bytes. There is no encoding beyond ASCII,
except one 0x85 in two strings: see the fonts' [characters](./font#characters).

## Subtitles (SUBT)

`SS2rRegisterTemplates` 0x487080 registers the template.

| Offset | What |
|---|---|
| 0x00 | 16 bytes, unused |
| 0x10 | The block's offset in the `.raw` (u32). It is a plain field, not a raw field, so the loader leaves it alone |
| 0x14 | The count of entries (u32) |
| 0x18 | A u32 per entry: its offset in the block |

An entry is its name, 0-terminated, then its text, 0-terminated. The readers add three things: the
`.raw`'s base (`TMrInstance_GetRawOffset` 0x423e10), field 0x10, and the entry's offset.

**`SUBTsubtitles`** holds 609 entries: the dialogue, `Name:  text`. `SSrSubtitle_Find` 0x47f460 finds
a sound's line, and `SSrAmbient_GetSubtitle` 0x481c70 does the same for an ambient sound, keeping the
result at +0xf8. They walk the entries in order and take the first match:

- An entry matches if the name starts with it. The test compares the entry's length, ignoring case
  (`UUrString_CompareLen_NoCase` 0x426700).
- An entry that starts with a digit (the C runtime's `isdigit`) is compared with the name past its
  first character. `01_01_01` is the line of the sound `c01_01_01`, and of any name `?01_01_01...`.

**`SUBTmessages`** holds 147 entries: training, door and console messages. `SSrMessage_Find` 0x47f550
finds the entry of exactly that name, ignoring case (`UUrString_Compare_NoCase` 0x4266d0). The texts
use the formatting codes of `TSrContext_FormatString` ([fonts](./font#measuring)): `[o.` ...`]`,
`[c.` ...`]`.

Both compares fold only `a` to `z`, to upper case, on signed chars.

## In-game strings (IGSt) and string arrays (IGSA)

0x503600 registers `IGSt`, `IGSA`, `IGPG`, `IGPA` and `IGHH`.

`IGSt`:

| Offset | What |
|---|---|
| 0x00 | Link to the `TSFF`, or none |
| 0x04 | Font style (u32) |
| 0x08 | Colour (u32, ARGB: `0xff74d0ff` ...) |
| 0x0c | Font size (u16) |
| 0x0e | Flags (u16): which of the four this string sets |
| 0x10 | The text, 384 bytes |

The fields 0x00 to 0x0f of an `IGSt` or an `IGPG` push a text state (0x5028d0) onto a stack of 5 at
0x5ed020, starting from the state under it. The flags at 0x0e say which fields replace the state's:

| Flag | Sets |
|---|---|
| 1 | the font family (0x00) |
| 2 | the style (0x04) |
| 4 | the colour (0x08) |
| 8 | the size (0x0c) |

On the CD, 1,100 strings set nothing and draw in their page's state.

`IGSA`: 20 unused bytes, then the count (u32) at 0x14, then the `IGSt` links from 0x18.

## Pages (IGPG) and page arrays (IGPA)

`IGPG`:

| Offset | What |
|---|---|
| 0x00 | Font header, as in `IGSt` |
| 0x10 | Link to the page's image: a `TXMP` (60 on the CD), a `PSpc` (44) or none |
| 0x14 | `IGSA`: the text of the page's first part |
| 0x18 | `IGSA`: the text of its second part (the objective's hint ...) |

`ONiIGUI_Page_Paint` 0x5029a0 draws a page with its header's state. Which part it draws depends on a
value at +8 of its second argument:

- 0x6e: the image (centred if it's a texture), 10 pixels of gap, then each string of the first `IGSA`
  under the last, each with its own state
- 0x6f: the strings of the second `IGSA`

Each string is drawn with `DCrDraw_String2` 0x469eb0 at the `IGSt` + 0x10.

`IGPA`: 20 unused bytes, then the count (u32) at 0x14, then the `IGPG` links from 0x18. A level's
objectives and the text consoles' pages are `IGPA`s.

## The HUD help (IGHH)

`IGHHhud_help_info`, drawn by `ONiInGameUI_DisplayHelp` 0x501440 while the help overlay is on:

| Offset | What |
|---|---|
| 0x00 | 28 bytes, unused |
| 0x1c | Link to the left texture (`TXMPleft_help`) |
| 0x20 | Link to the right texture (`TXMPright_help`) |
| 0x24 | Left texture's x, y (s16 each), added to a point of the HUD (a float the draw rounds with 0x4340a0) |
| 0x28 | Right texture's x, y (s16 each), likewise |
| 0x2c | The count of labels on the left (u32): the first this many |
| 0x30 | The count on the right (u32). Inferred: the draw doesn't read it; it equals 0x34 less 0x2c |
| 0x34 | The count of labels (u32) |
| 0x38 | The labels, 0x44 bytes each: the text (64 bytes), then x and y (s16) |

The overlay is on while the pause screen's help page is up (0x501990, button 0x69) or after
`ui_show_help 1` (`ONrInGameUI_EnableHelpDisplay` 0x504cf0); the flag is 0x5ed099. Each side's point is
its HUD element's centre (`ONrInGameUI_SetArcPoints` 0x504270: 10 in from the side plus half the
element's picture, half its height up from the bottom), rounded, plus the IGHH's offset, less half the
IGHH's texture (or, without a link, the HUD's own picture, at 0x5ed020 and 0x5ecf98; without that
either, less 64). Each texture is drawn there at its size when its element is shown (0x5ed0dc left,
0x5ed0a0 right). Confirmed by the draw.

The labels go in Tahoma 7, plain, with format 1 and the shade 0xff00ff00, the display's rectangle as the
layout's. The left side's (the first 0x2c of them, while the left element is shown) take their pen at
the label's offset plus the left point plus 3, and are moved so that their first run starts at the pen;
the others, at the right point less 3, so that their first run ends there. Vertically each is moved so
that the middle of its runs' box (the first's top, its baseline less the ascending height, to the last's
bottom, a line height lower; the sum halved in integers) is at the pen's height. Each is laid out with
`TSrContext_FormatString` 0x42e210 and drawn twice with `TSrContext_DrawFormattedText` 0x42d9e0: first
offset by (1, 1) in 0xff3f3f3f, then in its own colours. Port: `draw_help` in `src/render/ui_draw.c`.

## String arrays (StNA) and strings (TStr)

0x516c00 registers both.

- `StNA`: 22 unused bytes, then the count (u16) at 0x16, then the `TStr` links from 0x18.
- `TStr`: the text, 128 bytes.

The CD has three arrays: `StNAanim_types` (233), `StNAanim_states` (70) and `StNAanim_flags` (24). They
hold the names of the [animations](./tram)' types, states and flags. Every `TStr` is unnamed and in one
of them. Apart from the registration, the v1.0 exe has no reference to either tag (searched as
immediates), so they look unused at run time. Inferred: the Mac build has functions that sort animation
type and state string tables (`ONiQSort_AnimTypeToStringTableCompare`), which may build them from the
exe instead.

## Check

`tools/onifont.py <game>` hashes each kind with FNV-1a (32 bits), over every level, in the line formats
its docstring gives. `font_test` must print the same lines:

```
SUBT: arrays=2 entries=756 hash=3023d11c
IGSt: strings=1213 hash=b0768056
IGSA: arrays=309 strings=1213 hash=fd26ca2a
IGPG: pages=212 hash=37f644de
IGPA: arrays=70 pages=159 hash=0c9bee35
IGHH: overlays=1 labels=13 hash=3dadba0e
StNA: arrays=3 strings=327 hash=a993ace8
subtitles c01_01_01: Griffin:  Give me another reading.
messages XWELCOME: [o.Basic movement training initiated.]
all: hash=85c7502f
```

The last two lines are `SSrSubtitle_Find` and `SSrMessage_Find` ported. `onifont.py <game> --text
[levelN ...]` prints every subtitle, `IGSA` string, help label and `StNA` string.
