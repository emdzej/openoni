# The window manager (WM)

Bungie's own GUI toolkit, under the menus (main menu, options, load and new game, quit), the pause
screen and the in-game text consoles. Windows have a class, a procedure, a place in a tree, and get
messages; dialogs are built from `WMDD` instances. Port: `src/wm.c` (in progress).

## Window classes

`WMrWindowClass_Register` 0x475a70 adds a class to the table at 0x5ea1b0 (0x5e99a8 the count, 12-byte
entries: the class number u16, the procedure, the size of its data); a number registered twice is
refused. The desktop (class 1) is registered by the WM's start (0x475ad0), which also makes the desktop
window, the size of the display.

| Class | What | Registered by | Procedure |
|---|---|---|---|
| 1 | desktop | 0x475ad0 | 0x476d70 |
| 3 | box | 0x46ae90 | 0x46af20 |
| 4 | button | 0x46b0c0 | 0x46b150 |
| 5 | check box | 0x46b550 | 0x46b5e0 |
| 6 | dialog | 0x468c40 | 0x468cd0 (`WMiDialog_Create`) |
| 7 | edit field | 0x46b930 | 0x46b9c0 |
| 8 | list box | 0x46c690 | 0x46c720 |
| 9 | menu bar | 0x46fa40 | 0x46fad0 |
| 10 | menu | 0x46ec40 | 0x46ecd0 (`WMiMenu_Callback`) |
| 11 | picture | 0x4701d0 | 0x470260 |
| 12 | popup menu | 0x470530 | 0x4705c0 |
| 13 | progress bar | 0x470d80 | 0x470e10 |
| 14 | radio button (inferred) | 0x471040 | 0x4710d0 |
| 16 | scroll bar | 0x471a80 | 0x471b10 |
| 17 | slider | 0x472760 | 0x4727f0 |
| 20 | text | 0x473160 | 0x4731f0 |

The procedures are fastcall: the window in ecx, the message in edx, two parameters on the stack.

## Windows

`WMrWindow_New` 0x475160 (class, style, flags, id, x, y, w, h, parent, the create parameter): a slot of
the 255 in the handle table (0x5e99b0, 8 bytes: bit 0 used, the window), searched from the one after the
last given out. A second desktop is refused. The window record:

| Offset | What |
|---|---|
| 0x00, 0x04 | previous and next sibling |
| 0x08 | parent (style 0x2000: a child) |
| 0x0c | owner (not a child: the nearest window up the parents that is not a child) |
| 0x10 | first child |
| 0x14 | the class's table entry |
| 0x18 | style |
| 0x1c | flags |
| 0x20 | id (u16) |
| 0x24 | handle (the slot) |
| 0x28, 0x2a, 0x2c, 0x2e | x, y, width, height (i16) |
| 0x38 | title (255 characters) |
| 0x140, 0x142 | the title's width and height in the window's font |
| 0x148.. | the font: family (`TSFF`), size 7, style 0, colour 0xff000000 |

A child is added at the end of its parent's children.

## Dialogs (WMDD)

`WMrDialog_ModalBegin` 0x4689d0 (dialog id, procedure, parameter): 0x468830 finds the `WMDD` with the id
(at +0x100) among all of them, makes its window (class 6), then loops, the original's own blocking loop:
each frame the screen is drawn (the game's draw hook 0x5e96cc, then the WM's windows), and the input's
events are dispatched, until the dialog's "done" byte (its data +4) is set; its result (+8) is
returned. Within the loop, a window other than the dialog getting a key or mouse event (the event types
6 to 0xe, 0x1e to 0x27) is ignored.

`WMDD`, offsets from the instance data:

| Offset | Size | What |
|---|---|---|
| 0x000 | 256 | Title |
| 0x100 | 2 | Id (`dialog_text_console`: 71) |
| 0x104 | 4 | Style |
| 0x108 | 4 | Flags (0x10000: centred on the display, `WMiDialog_Center` 0x469180) |
| 0x10c | 2 each | x, y, width, height |
| 0x114 | 4 | Item count |
| 0x118 | 0x124 each | Items |

An item:

| Offset | Size | What |
|---|---|---|
| 0x000 | 256 | Title |
| 0x100 | 2 | Class |
| 0x102 | 2 | Id |
| 0x104 | 4 | Style (made a child: \| 0x2000) |
| 0x108 | 4 | Flags |
| 0x10c | 2 each | x, y, width, height, in the dialog |
| 0x114 | 4 | Font family (`TSFF`) |
| 0x118 | 4 | (1 or 0) |
| 0x11c | 4 | Colour (ARGB) |
| 0x120 | 2, 2 | Font style, size |

The dialog's procedure on its create (0x12) makes every item's window with the dialog as parent; its
command (0x17) with the id 1 (OK) or 2 (cancel) in the low word and 1 in the high ends the dialog with
that id as the result, unless the dialog's own procedure handled it. Keys (0x1c): Return or the pad's
Enter press the focused button; Escape or 0x2a cancel.

`dialog_text_console` (460 by 410, centred): the text (class 20, id 110, 20, 30, 420 by 315), `Next`
(button, id 102) and `Close` (button, id 2), both at 310, 355, 130 by 40, and the outline (box).

## Messages

A window's procedure gets (window, message, p1, p2). `WMrMessage_Send` 0x4760d0 calls it at once (a
window no longer valid, 0x474c30, gets nothing); 0x476080 posts to the queue of 32 (0x5e9770, 16 bytes:
window, message, p1, p2; read by 0x476010, dispatched by 0x475ca0). The numbers, from the callers:

| Message | What |
|---|---|
| 1 | before the create (the class's answer 1 refuses the window) |
| 3 | hit test (p1 the point; 0x8000: the client area; else the part of the frame) |
| 4 | paint the frame (the default: the active `PSUI`'s window parts by the flags) |
| 5 to 0xe | the mouse on the frame: move, left down, up, double, middle down, up, double, right down, up, double (p2 the part) |
| 0xf | activate, p1 1 or 0 (the frame) |
| 0x11 | the client rectangle |
| 0x12 | create (p1 the create parameter) |
| 0x13 | destroy |
| 0x14 | close |
| 0x17 | command from a child: p1 the child's id, its notice in the high word; p2 the child |
| 0x1c | key down (p1 the LI key code) |
| 0x1e to 0x27 | the mouse in the client area, as 5 to 0xe |
| 0x28 | activate, p1 1 or 0 |
| 0x2b | paint |
| 0x30 | the mouse capture lost |
| 0x31 | a child made (p1 0x12) |
| 0x36 | paint a dialog's item (p1 the item's id) |
| 0x3d | the dialog's start (its procedure's parameter) |

The active window (0x5ea4c8) is a top-level window (not style 0x2000); activating (0x4746d0) sends the old
one 0x28 and 0xf with 0, takes the mouse capture (0x5e9978) away with 0x30, brings the new one to the
front (0x474830) and sends it 0xf and 0x28 with 1. A child given activation activates its top-level
parent. In front (`WMiWindow_BringToFront` 0x474830, the parents first): a window of flags 0x8000 (the
menus) goes first of its siblings, one of 0x4000 after the leading ones of 0x8000, any other after the
leading ones of either (0xc000). `WMrWindow_SetCapture` 0x474900 sets the mouse capture (the old one gets 0x30; the Mac build's neighbours had
named it `WMrMessage_Send`).

## Drawing

Each frame (0x476140) the desktop gets 4 and 0x2b, then its children are drawn (`WMiWindow_Draw` 0x4762d0;
the Mac build's neighbours had named it `WMiCaret_Draw`) from the last to the first, each one
visible (style bit 1) getting 4, 0x2b, its caret (blinking 50 sixtieths on, 25 off, the `PSUI` part
0x10) and then its children the same way: the first child is the front. The cursor last (0x468770).

## Input

The Windows build's window procedure (0x50f7a0) feeds the input layer's queue (0x403e10): the mouse
(WM_MOUSEMOVE 1, left down 2, up 3, middle 4, 5, right 6, 7; a double click as a down) at its place, and
the keys as type 9: Tab, the page keys, the arrows, Insert, Delete and F1 to F12 from WM_KEYDOWN through
the virtual-key table at 0x5293b8 (256 bytes, in the exe), the others from WM_CHAR as their characters
(with Ctrl, the WM_KEYDOWN's translation). The WM (0x477070, while it takes input: 0x5e99a0) first runs
its timers, then takes up to 10 events (unless the input mode is the game's, `LIrMode_Get` 1): the mouse
clamped to the desktop, the window under it (0x475f50: the deepest visible one, style bit 1, not
disabled 6) or the capture, its hit test, and the message from the client table (0x53cac0: 1 0x1e,
2 0x1f, 3 0x20, 4 0x22, 5 0x23, 6 0x25, 7 0x26) or the frame's (0x53cb00: 5, 6, 7, 9, 10, 12, 13), a
move with the capture being 0x1e; a second down soon after is followed by its double (0x477380). The
message's p1 is the point (x in the high word); p2 the frame's part, or in the client the event's
buttons (bit 0 the left one held: the drags test it). The keys go to 0x477510: the focused window
(0x5e999c) gets 0x1c.

The timers (`WMrTimer_Start` 0x475b70: the window, an id, a period in sixtieths; refused if the window
has that id already; `WMrTimer_Stop` 0x475c40) are 16-byte entries of an array: the window, the id, the
period, the next tick. Each one past due is set a period on from now and its window's procedure gets
0x1b with the id; if the array changed meanwhile the walk starts again.

## The drawing context (DC)

`DCrGetDC` 0x4694b0 takes one of 3 DCs (0x5e96d0, 0x2c bytes each) for a window's client area: its
origin on the desktop (the window's screen rectangle, 0x474e60, plus its client origin) and its clip
(the client area cut by each parent's, up to the top-level window); `DCrGetWindowDC` 0x469720 the same for
the whole window, `DCrReleaseDC` 0x469650 frees it. The DCs share one text context (0x5e9754): a window's
font sets its family, size, style and colour (`DCrText_SetFontInfo` 0x46a570; the font is the family's of
that size and style, else its nearest), `DCrText_SetShade` 0x46a5c0 the colour, `DCrText_SetFormat`
0x46a5b0 the format.

### Part specs (PSpc, PSUI)

A `PSpc` is 9 parts of a texture, by columns (part = column * 3 + row): each a top left corner (i16 x, y,
from +0x00) and a bottom right (from +0x24), the `TXMP` at +0x48. At load (`PSiPartSpec_PrivateData`
0x46ab70) each part's texture coordinates are its corners over the texture's size (0x46ac80), and the
edges are kept: the left column's width (part 0), the right's (part 6), the top row's height (part 0),
the bottom's (part 2).

`DCrDraw_PartSpec` 0x4698b0 (DC, part spec, a mask of the 9 parts, place, width, height, alpha) draws the
parts over the rectangle: the edges at their sizes when both fit, else each edge e becomes e * e / size
(or the size, if smaller than the edge); the middle column and row take what is left (none if less than
nothing). The mask 0x10 alone (the centre) fills the rectangle. The columns from the left (the second
after the first's width if any of the first is drawn, the third after both), the rows down each column,
a row's height added only for the parts drawn. Each part is a sprite clipped to the DC's clip, its
texture coordinates cut alike (`WMrClipRect` 0x469340).

A `PSUI` lists the part specs the classes draw with (45 links): +0x00 background, +0x04 border, +0x08
title, +0x0c grow, +0x10/+0x14 close idle/pressed, +0x18/+0x1c zoom, +0x20/+0x24 flatten, +0x28 text
caret, +0x2c outline, +0x30 button, +0x34 default button, +0x38 hilite (pressed), +0x3c/+0x40 check box
on/off, +0x44/+0x48 edit field and with focus, +0x4c list hilite, +0x50 divider, +0x54 check, +0x58 popup
menu, +0x5c/+0x60 progress track/fill, +0x64/+0x68 radio on/off, +0x6c..+0x78 the arrows up and down
idle/pressed, +0x7c vertical track, +0x80..+0x8c left and right, +0x90 horizontal track, +0x94 hilite,
+0x98/+0x9c slider thumb/track, +0xa0, +0xa4 background, +0xa8 file, +0xac folder. The active one
(`PSrPartSpecUI_GetActive` 0x46aa60) is "psui_g206" (0x53c45c), the developers' look, unless set
(`PSrPartSpecUI_SetActive` 0x46aa90): the game's dialogs set "psui_oniUI" (0x54c898) while they run.

### Text

`DCrDraw_String2` 0x469eb0 (a rectangle or the client area, the string, the pen) runs
`TSrContext_FormatString` 0x42e210 and draws its lines (`TSrContext_DrawText_DC` 0x42e050). The
formatting, with the context's font:

- The string becomes runs: "[" opens a code, in which a letter of the colour table (0x533da0: 8 bytes an
  entry, the letter and the ARGB; 0 ends it) sets the colour of what follows and "." closes it (any
  other character closes it and is drawn); "]" ends the run and puts the context's colour back. At most
  24 runs, 255 characters each.
- A run that would pass the rectangle's width is cut after the last of the family's breaking characters
  (the `TSFL`'s second string) in it; with none, it moves to the next line, unless it is the line's first,
  which stays whole. Format 0x30: one line, unbroken.
- Each line's runs from its start: centred in the rectangle (format 2), against its right (4), else at
  the pen's x. The lines go down from the pen's baseline, a line the font's ascending, descending and
  leading heights; format 8 centres them in the rectangle's height.

## The classes the game's dialogs use

- The dialog (6, `WMiDialog_Create` 0x468cd0): its create makes the items, its own procedure (the
  parameter of `WMrDialog_ModalBegin`) gets the messages first.
- The box (3, 0x46af20): the `PSUI` outline (+0x2c) over it, alpha 0x7f when disabled (flags bit 2), else
  0xff; style 0x40000 adds its title over a part of the centre (not ported).
- The button (4, 0x46b150): the button part (+0x30; +0x34 for style 0x100000, the default button; +0x38
  while pressed or lit, the long's bits 0x30000), alpha as the box's; style 0x20000 its title, centred
  (format 0xa) in its font's colour (0xff808080 when disabled). A left press takes the focus and the mouse,
  marks it pressed (the long's 0x10001); the release lets go, and over the button sends the parent 0x17
  (the id, 1 in the high word) after the click's impulse (0x5e975c, not ported). The default button
  takes the Return key (0x38 answers 0x40).
- The text (20, 0x4731f0): style 0x800000 is drawn by its dialog (0x36 to the parent, with the DC and the
  item); else its title in its font (grey when disabled), style 0x20000 centred, 0x40000 right, 0x100000
  centred in the height.

- The check box (5, 0x46b5e0): the `PSUI` check part (+0x3c checked, +0x40 not), its top left part's size,
  at the left, centred in the height, faded (0x7f) when disabled; style 0x10000 its title 2 after it
  (format 9; grey when disabled). A press takes the focus and the mouse (the long's bit 0); the release
  over it turns the long's 0x10000 over and sends the parent 0x17 (the id, 1 high). 0x3b sets it
  (`WMrCheckBox_SetCheck` 0x46b900), 0x2d tells it (0x46b920).
- The slider (17, 0x4727f0; 0x18 bytes: min, max, value, +0xc the button held, +0xd dragging, +0xe the
  thumb's x less the mouse's, +0x10 the thumb's x and its centre's y, +0x14 its size). Its parts
  (0x4724c0): the track (1, the `PSUI`'s +0x9c, its top row's height, centred, the window's width), the
  thumb (4, +0x98, its top left part's size), the sides of the thumb (2: the track's left to the thumb's
  left + 1; 3: the thumb's right - 1 to the track's right; the window's height). The thumb's x
  (0x472cc0) is the track's inside (less its left and right parts, 0x472c40) plus the round of the
  inside less the thumb's width, over the range, times the value less the min; its y the track's
  middle. `WMrSlider_SetRange` 0x472650 sets the min and max and the value to the min; 0x4723b0 the
  value, clamped. Painted: the track's top row (mask 0x49) and the thumb (mask 1, faded when disabled).
  A press (0x472db0) takes the focus and the mouse: on the thumb it starts the drag, on a side it steps
  a tenth of the range down or up (at least 1); a move with the left button while dragging sets the
  value to the min plus the round of the range over the inside's span times the mouse's x plus the
  offset less the inside's left. Its timer (id 1, every 10 ticks, from its create) presses again where
  the mouse is while the button is held. Keys (it takes 0x30, the arrows and the page keys): Home the
  min, End the max, Page Up/Down a tenth, the left and right arrows 1. Each change sends the parent
  0x17 (the id, 1 high). (Its create places the thumb over an empty range: the x87's 0 / 0 is its
  integer indefinite, 0x80000000, of which the low half, 0, is kept.)
- The menu (10, 0x46ecd0; 0x14 bytes: its size, +4 the layout to redo, +6 the widest of the columns ended
  by the height, +8 the item lit, +0xc the items, +0x10 the columns). An item is 0x48 bytes: +0 flags (1 a
  separator, 2 enabled, 4 checked), +2 the id, +4 the title (0x40), +0x44 the height, +0x46 the width (the
  title's rectangle in the menu's font; a separator twice the top row of the `PSUI`'s +0x50). Made by
  `WMrMenu_Create` 0x46ea40 (flags 0x8000, its owner the given window) from a `WMM_` (+0x12 its id, +0x14
  its title, +0x54 the count, +0x58 the items, 0x44 bytes each: flags, id, title). The layout (0x46f1d0)
  stacks the items in columns (8 bytes: the first, the count, the width, the height) from twice the
  background's top row while they fit the menu's height; a column is as wide as its widest item plus
  0x16 and twice the left column. `WMrMenu_Locate` 0x46e830 puts it at a point, as tall as all its
  items and as wide as their columns at that height, moved up or left to fit the desktop with 3 to spare.
  Painted (0x46f330): each column's background (`PSUI` +0x00), and in it from the top row down each item:
  a separator's part (+0x50) at its middle, else the lit one's hilite (+0x4c, when it and the menu are
  enabled), its check (+0x54, the centre part), and its title 0xc past the left column, grey when
  disabled. A move lights the item under the mouse (0x46f060), a press or a release over an enabled one
  sends the owner 0x19 (the id, the flags high, the menu). 0x2f (shown or hidden) unlights it.
- The popup menu (12, 0x4705c0; 0x10 bytes: +0 a tick, +4 open and waiting, +8 its menu, +0xc the selected
  id): its menu the `WMM_` its title names (none with style 0x20000; a missing one refuses the window),
  in its font, the first item selected. Painted: the `PSUI` popup (+0x58) over it, the selected item's
  title 2 past its left column (format 9). A press opens the menu at the popup's place (0x470a90; 0x1a to
  the menu's parent first) and takes the mouse; a release within 15 ticks leaves it open for the next
  press (which goes to the menu, or closes it when it is no longer shown), a later one is passed to the
  menu; moves are passed while it is open or the button is held. The menu's choice (0x19, 0x470bd0) closes
  it, selects the item, lets the mouse go and is passed to the parent (0x19, p2 the popup). The title
  (0x470ae0) is the selected item's; without style 0x10000 the popup takes the menu's width (+6) and the
  line height plus its top and bottom rows. `WMrPopupMenu_Reset` 0x470c70 removes the items,
  `WMrPopupMenu_AppendItem` 0x4704d0 adds an enabled one of an id, `WMrPopupMenu_SetSelection` 0x470d00
  selects one and tells the parent (0x19, the item's flags high).

## The text console

`text_console` name (`ONrInGameUI_TextConsole_Display` 0x504c30): the `TxtC` of the name (its +0 the
`IGPA` of pages) in the dialog 71, the game's `PSUI` (psui_oniUI) and the input mode 0 meanwhile, the WM
taking the input (`WMrActivate` 0x476110; `WMrDeactivate` 0x476410 after). Its procedure
(`ONiInGameUI_TextConsole_Callback` 0x5049a0, the TxtC and the page in its data):

- 0x3d: page 0; `Next` (102) shown while pages follow, `Close` (2) on the last.
- 0x17: 2 ends it; 102 turns the page, past the last ends it; the buttons again.
- 0x1c: Escape sends Close's command, space Next's (so space on the last page ends it too), any other key
  a command of no item.
- 0x36 for the text (110): the text state's stack starts from the item's font, format 1, and the page
  (`ONiIGUI_Page_Paint`, [text.md](../formats/text.md)) is drawn.

## The desktop and the picture

The desktop (`WMiDesktop_Callback` 0x476d70) paints its background, set by 0x3c (`WMrSetDesktopBackground`
0x476b00): a texture at its size or a part spec over the desktop. `PSrPartSpec_LoadByType` 0x46aa10
finds a part spec by its type in "partspec_list" (`PSpL`, the string at 0x53c44c: +0x14 the count, then
8 bytes an entry, the type and the `PSpc`); the menus set type 1 and put type 0 back.

The picture (11, `WMiPicture_Paint` 0x470260) names its image in its title: a `TXMP`, else a `TXMB`
(style 0x20000: none; 0x55 sets a part spec, 0x56 a texture). A part spec is drawn over the window, a
texture at its size cut to the window, or stretched over it (style 0x10000). `DCrDraw_TextureRef`
0x469fc0 draws a `TXMP` over a width and height at its texels' scale (the texture coordinates the size
over the texture's) or stretched (0 to 0.999), and a `TXMB` as its tiles, 256 apart by rows, each cut to
what is left.

## The main menu

`ONiMain` 0x4d3280, after its start-up and the Oni window (the WM and its desktop), runs
`OWrOniWindow_Startup` 0x4bebd0: the WM's input on, the input mode 0, `ONrOutGameUI_MainMenu_Display`
0x4d2c70; then, unless the player quit (0x630ffd), `ONiRunGame` 0x4d4640. In a level, the player's escape
(`ONiGameState_PlayerEscape` 0x4faec0: the input's bit 1, at most once in 15 ticks, not in a cutscene)
runs the same menu over the level (`OWrOniWindow_MainMenu` 0x4bec00).

The menu: "psui_oniUI", the desktop's background of type 1, dialog 150 (640 by 480, centred: five
buttons over the picture `pict_mainmenu`). Its procedure (`ONiOutGameUI_MainMenu_Callback` 0x4d2cd0):

- 0x3d: Options (102) shown out of a level, Resume (104), in its place, in one; the menu's music.
- 0x17: 2 (Escape) resumes in a level; 100 New Game runs dialog 154 ("Press Start to begin a new game.",
  Start 1, Cancel 2), and Start ends the menu and starts level 1 (`OWrGame_Start` 0x4beb40: the current
  level unloaded, the continue cleared, the level loaded); 101 Load Game, dialog 151 (the levels'
  list); 102 Options, dialog 152; 103 Quit, dialog 153 (Yes 1, No 2), Yes sets the quit; 104 Resume.
  The yes-or-no dialogs (`ONiOutGameUI_Choice_Callback` 0x4d2aa0) end with the button's id.

Dialogs' keys: Return posts the command of the default button (a button of style 0x100000 tells its
dialog its id on its create, 0x3f; 0x3e asks), if enabled; Escape posts `WM_CLOSE` (0x14), which the
dialog turns into the cancel's command (2).

## The options

Dialog 152 (`ONiOutGameUI_Options_Callback` 0x4d25c0): the quality (slider 100, 0..4), the gamma (110) and
the overall volume (102, 0..100), the resolution (popup 101, style 0x20000) and the difficulty (108,
`pm_difficulty`: Easy, Normal, Hard, ids 0 to 2), the subtitles (check box 107) and the inverted mouse
(109), and Close (2).

- 0x3d: the sliders' ranges; the resolution's popup filled with the active draw engine's modes of its
  device ("%d x %d x %d bit", 0x54c8f0, the width, height and depth of each, the id its index) and the
  active one selected; then 0x4d2ad0 sets the items: the quality, the round of the gamma and of the
  volume times 100, the two checks, the difficulty selected.
- 0x17 with notice 1: 100 the quality (`ONrPersist_SetGraphicsQuality` 0x50f0e0, then 0x508ef0), 0x66 the
  volume (the value times 0.01, `ONrPersist_SetOverallVolume` 0x50f130 then `SS2rVolume_Set`), 0x6b the
  subtitles (0x50f1f0), 0x6d the inverted mouse (0x50f1a0), 0x6e the gamma (0x50f3e0, times 0.01: refused
  out of 0..1, else the display's gamma, 0x407a60), 2 ends it.
- 0x19 from 0x6c sets the difficulty; from 0x65, another mode than the active one: persisted
  (0x50f2b0), and unless it can be set at once out of a level, "You must restart Oni for your changes to
  take effect; Oni will now exit." (dialog 156) and Oni exits.

The settings are persist.dat's (+0x3c quality, +0x40 volume, +0x44 bit 0 subtitles and bit 1 the
inverted mouse, +0x48 difficulty, +0x4c the resolution, +0x50 the depth, +0x54 gamma). The subtitles:
a dialogue line (0x5125c0) with them on shows its `SUBT` line (`SSrAmbient_GetSubtitle` 0x481c70) for its
length (0x78 ticks without a sound) and 60 more, white (0x533f70) on dark grey (0x533f74), in the
letterbox's console when the letterbox is on. The inverted mouse: the input's y axis (0x403220) is the
move times -0.125 with it (the default), 0.125 without.

## The HUD's right ring

Not the WM's, but drawn with it on the in-game UI's screen (`ONrInGameUI_Display` 0x4fff50; the port's
`src/render/ui_draw.c`, its state in `src/igui.c`). Confirmed from the code:

- **The arcs** are made at the level's load (`ONrInGameUI_SetArcPoints` 0x504270, from 0x503eb0) by
  `ONiInGameUI_NewArc` 0x5045a0 (start and end angle, outer and inner radius, segments in `edx`, the
  ring texture), each full: the health's four at 0x5ed080..0x5ed08c (radii 50/40, the overhealth's 55/40,
  24 segments; full pi to -pi/2, and -pi/2 to -2.967), the invisibility's at 0x5ed004, the shield's two
  at 0x5ecfbc and 0x5ecfc0 (63/51, 8 segments, 0.0349 to 1.5533), the hypos' six at 0x5ed0e0 (63/51, 1
  segment, the table at 0x555480) and the hits' four at 0x5ecf84 (37/30, 4 segments, the table at
  0x5554b0: the ring's right, bottom, left and top quarters). Their points change later only through
  0x5007d0 (an arc's angles); 0x500640 draws one with a colour (0 untinted, else render state 7) and
  an alpha (cut to a byte, state 9).
- **The bars** (`ONiInGameUI_UpdateDamageMeter` 0x5048e0, a 0x14-byte record: +0 dirty, +4 value, +8
  target, +0xc rising, +0xd falling, +0x10 the shown value, a float) are moved each tick by
  `ONrInGameUI_Update` 0x504780 for the player: the health's (0x630fc0) with the hit points, a target of
  the hit points plus the hypo's healing to come (C+0x1ae) and a fall of the maximum times 1/180 a
  tick; the shield's (0x630fe0) with C+0x1b6 (the shown shield, up 2 a tick toward C+0x1b8 in
  `WPrInventory_Update` 0x51bad0, down at once) and a target of C+0x1b8, falling 0.5556 a tick. A fall
  starts the shown value at the old value; a target over the value shows the target. The bars and the
  arcs' on flags (0x630fd4..0x630fd7, 0x630ff4/5) are never reset: from a level to the next a bar that
  sees no change leaves the arcs at SetArcPoints' full angles with the flags of before.
- **DisplayHealth** 0x500970 sets the arcs again only when its bar is dirty: the health from pi by
  -3 pi / 2 of its share of the maximum, the overhealth from -pi/2 by -1.396 of its share (to the
  ONGS cap); while rising or falling the eased parts: from the health's end to the shown share (none
  over the maximum) and, the shown share over the maximum, from the overhealth's end (or -pi/2) on. All
  four in the health's colour (0x4ef450) of the shown value while falling, else of the health; the
  eased parts at 0x50 (0x7f falling) times the flash; unless rising, under 30% the pulse.
- **DisplayShield** 0x501080: from 0.0349 by 1.518 of the shown shield's share of 100, the eased part
  from there to the bar's shown value; drawn at 255 and 0x7f, or while its flash bit (8) is on at the
  flash and the flash squared over 127 (cut to a byte: an original quirk, kept).
- **DisplayHits** 0x500db0: each side's arc at C+0x166c+side, set to 255 in `ONrCharacter_TakeDamage`
  0x4e65f0 from the hit's direction turned by the desired facing less the run's turn (C+0x170 - C+0x174)
  and faded by 3 a tick in the update; the flash bit 2 scales them.
- The draw order: health, invisibility, shield, hypos, hits, the LSI's icon.
- **`ui_fill_element`** (`ONiInGameUI_FillElement` 0x504e30; `ui_flash_element` is 0x504f40, both
  registered by 0x503040 with the element table at 0x5556d0) sets bits of 0x5ed09c, cleared at the
  level's start: health (1) and shield (8) show SetArcPoints' full arcs and mark their bar dirty, the
  hits (2) all four at 255, the invisibility (4) its full arc, the hypos (0x10), clips (0x200) and cells
  (0x400) six, the ammo (0x40) its ring full, the compass (0x80) a whole circle and the arrow (0x100)
  up (unless the flag is more than 10 below). No level script uses it.
- A new objective not yet read (0x5ed098) runs the flash's clock and flashes the compass, a whole
  circle while no target is set.

## The port

`src/wm.c` (the core, the DC, part specs, text, the classes), `src/igui.c` (the pages, the text
console), `src/outgame.c` (the main menu), `src/li.c` (the keys as the LI's codes), `src/render/ui_draw.c` (the sprites and glyphs). The
desktop is as tall as 480, scaled to the display (as the HUD). Unattended runs tap keys into the dialogs
with `--param keys=27,32,...`, or click with `@x:y` in the desktop's pixels (one every 30 frames of a
dialog). Not ported yet: the frames (title bars,
their boxes and dragging), the caret, the click's sound, the menu's music, and the edit field, the menu
bar, the progress bar, the radio button and the scroll bar. The options: the port has one screen mode,
the persisted one, so the resolution's popup lists that alone; the quality and the gamma are persisted
but not applied (the draw engine's quality and the display's gamma are not ported), and the subtitles go
to the one subtitle area, in the letterbox too.
