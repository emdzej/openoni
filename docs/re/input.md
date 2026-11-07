# The input layer's bindings and key_config.txt

How keys and the mouse become the game's actions. Status: read from the exe (confirmed) unless marked.
Port: `src/li.c` (`li_bind`, `li_config_load`, `li_actions`), the play loop in `src/app.c`, the player's
key filter in `src/chars.c`; test `tests/li_test.c`.

## The tables (read from the exe at run time)

| Address | Entries | What |
|---|---|---|
| 0x52db18 | 0x24 bytes, ended by an empty name | The actions: u16 type (1 a button; 2, 3, 4 an axis added to; 5 an axis taken from), u16 index, name[32] |
| 0x52e220 | 0x24 bytes, ended by an empty name | The key names: name[32], u32 LI code ("fkey1" 0x81, "leftshift" 0x91, "mousebutton1" 1, "mousexaxis" 5, ...) |
| 0x5292b8 | 256 bytes | DirectInput's key codes as LI codes (W 0x77, Escape 0x1b, F1 0x81, Shift 0x91, Enter 0x0d, the keypad's Enter 0xba) |
| 0x5293b8 | 256 bytes | The virtual keys as LI codes (the WM's path, [wm.md](wm.md)) |

A button action sets bit *index* of the frame's 64-bit action mask: "escape" 0, "console" 1,
"pausescreen" 2, "lookmode" 15, "forward" 21, "backward" 22, "turnleft" 23, "turnright" 24,
"stepleft" 25, "stepright" 26, "jump" 27, "crouch" 28, "punch" 29, "kick" 30, "block" 31, "walk" 32,
"action" 33, "hypo" 34, "reload" 35, "swap" 36, "drop" 37, "fire1" 38, "fire2" 39, "fire3" 40. The
axes "aim_lr" and "aim_ud" (type 3) are indexes 7 and 8; the manual camera's axes are 0 to 4. "reload"
is in the table twice.

## The bindings

100 slots of (LI code, action) at 0x55fa20, under a mutex (the input runs on its own thread).

- `LIrBinding_Add` 0x403c60: the action is found by name (`UUrString_Compare_NoCase` 0x4266d0); none,
  nothing happens. A key already in a slot takes the new action, so a key has one action. Escape (0x1b)
  is the exception: it gets another slot. Otherwise the first free slot; none left, the result is 2.
- `LIrBindings_RemoveAll` 0x403dc0: every slot cleared, then escape bound to "escape" (0x52ecf8). So
  escape can't be unbound.
- 0x403d60 unbinds one key (not escape).

## key_config.txt (0x4d4a50, from ONiMain 0x4d34a5)

1. `fopen("key_config.txt", "r")`. If there is none, the file is made (`"w"`) and the default lines
   written with `"%s\n"`: `unbindall`, an empty line, then the bindings in groups with empty lines
   between: W A S D (forward, stepleft, backward, stepright), Q swap, E drop, F punch, C kick; Space
   jump, the mouse's three buttons fire1 to fire3, its x and y axes aim_LR and aim_UD, F1 pausescreen,
   V lookmode; left and right Shift crouch; Caps Lock walk, left Control action; right Control action,
   Tab hypo, R reload, backslash profile_toggle (no such action: nothing), F13 screenshot; a second set
   P L ' ; (forward, stepleft, stepright, backward), O swap, [ drop, K punch, comma kick, Enter walk.
2. The file is read a line at a time (`BFrTextFile`). Lines that are empty or start with '\n', '\v',
   '\r' or '#' are skipped. The words are split on spaces and tabs.
   - `unbindall`: `LIrBindings_RemoveAll`.
   - `bind <key> to <action>`: the key's code (0x403a90: the name lower-cased; one character is its own
     code, else the names' table, else 0) and `LIrBinding_Add`. A line with too few words or without
     "to" prints "failed to parse bind, expected" and "bind <key> to <action>" on the console.
   - Any other first word: ignored.

The port keeps key_config.txt in the user's files (as persist.dat). Without one there, it reads the
game folder's (the original's place), and without that it writes the defaults to the user's files.

## A frame's actions (0x403220)

The input thread (0x404650) samples the devices into a double buffer of up to 0x78 samples (0x30 bytes
each), which the game drains each tick (0x403be0). A sample:

1. The mouse (DirectInput's `GetDeviceState`, or `GetCursorPos` and `GetAsyncKeyState` without
   DirectInput): each of its four buttons held is the event of code 1 to 4. Then the axes, always:
   code 5 the x move × 2 × 0.125, code 6 the y move × 2 × 0.125 (× 2 × −0.125 with the inverted
   mouse, 0x52ecf4), code 7 the wheel × 2 × 0.125. With 0x52d88c and 0x52d88d set the cursor goes back
   to the centre.
2. The keyboard (`GetDeviceState`, 256 bytes): each key held whose LI code (0x5292b8) isn't 0 is an event.
3. Each event (0x403b30) goes to the first slot with its code: a button sets its bit; an axis of type
   2, 3 or 4 adds the value, of type 5 takes it away.

The port does the same once a frame from the platform's keys and the captured mouse. It also counts a
key tapped within the frame as gone down (a headless run's taps). The bits gone down are those held now
and not in the last frame.

## The player's keys (ONiCharacter_PlayerKeyFilter 0x4fa520)

On the player's mask, before the character takes it:

1. In a conversation ([ai.md](ai.md) "Neutrals"), blows and the fire keys stop it, moving counts toward
   stopping it, and the movement, blow, jump and action bits are dropped while it lasts (`& 0x819fffff`,
   high word `& 0xfffffe01`).
2. fire1 (bit 38) also punches if the player has no weapon (C+0x194) or C+8 bit 0 is set (fire went down
   on an empty weapon with nothing to reload).
3. fire2 (bit 39) kicks if there is no weapon, the weapon has no alternate fire (ONWC +0xcc 0x400), or
   C+8 bit 0.
4. fire3 (bit 40) crouches.
5. A weapon with ONWC +0xcc 0x1000 sets walk (bit 32) and C+4 0x8000000 (walking only); otherwise that
   flag is cleared.

## What the player can't reach in v1.0

The console action (bit 1) opens the developer console only with developer access
(`ONiGameState_ProcessMiscActions` 0x4faec0: 0x5ece8e). The debug keys (`ONrDebugKey_WentDown`,
0x4f6200's character class keys, smite, the knockdown) need it too. The only write to 0x5ece8e is
`ONrCheater`'s case 11 (0x4f5db0), which also calls `LIrInitializeDeveloperKeys` 0x404730 (the
profile, record, camera and flag keys). v1.0's cheat table (0x5533f8) has no entry with id 11
([cheat.md](cheat.md)). So in this version a player can bind "console" in key_config.txt, but the key
does nothing, and the developer tools can't be switched on. The port keeps it so.
