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
| 5 | check box (inferred) | 0x46b550 | 0x46b5e0 |
| 6 | dialog | 0x468c40 | 0x468cd0 (`WMiDialog_Create`) |
| 7 | edit field | 0x46b930 | 0x46b9c0 |
| 8 | list box | 0x46c690 | 0x46c720 |
| 9 | menu bar | 0x46fa40 | 0x46fad0 |
| 10 | menu | 0x46ec40 | 0x46ecd0 |
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
parent. `WMrWindow_SetCapture` 0x474900 sets the mouse capture (the old one gets 0x30; the Mac build's neighbours had
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
keys go to 0x477510: the focused window (0x5e999c) gets 0x1c.

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

## The port

`src/wm.c` (the core, the DC, part specs, text, the classes), `src/igui.c` (the pages, the text
console), `src/outgame.c` (the main menu), `src/li.c` (the keys as the LI's codes), `src/render/ui_draw.c` (the sprites and glyphs). The
desktop is as tall as 480, scaled to the display (as the HUD). Unattended runs tap keys into the dialogs
with `--param keys=27,32,...`, or click with `@x:y` in the desktop's pixels (one every 30 frames of a
dialog). Not ported yet: the frames (title bars,
their boxes and dragging), the timers, the caret, the click's sound, the menu's music, and the classes other than these
five (check box, edit field, list box, menus, progress bar, radio button, scroll bar, slider), so Load
Game and Options.
