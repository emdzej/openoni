/* The input layer's keys (LI), as the WM gets them: the Windows build's window procedure (0x50f7a0) turns
   WM_KEYDOWN of Tab, the page keys, the arrows, Insert, Delete and F1 to F12 into LI codes by the
   virtual-key table at 0x5293b8 (in the exe, read at run time), and every other key into its character
   (WM_CHAR). The platform gives DirectInput codes: here they become the virtual keys and the characters
   Windows would make of them (a US keyboard). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform.h"

/* The LI codes of the keys pressed this frame. */
int li_key_presses(uint8_t *codes, int cap);

/* ---- The bindings (LI's 100 slots of a key and an action, 0x55fa20) ---- */

/* The LI code of a key's name (0x403a90): lower-cased, a single character is its own code, else the
   name in the exe's table at 0x52e220 ("fkey1" 0x81, "leftshift" 0x91, "mousebutton1" 1, ...); 0 none.
   name is lower-cased in place, as the original does. */
uint32_t li_key_code(char *name);
/* LIrBinding_Add 0x403c60: the key to the action of that name (the exe's table at 0x52db18). A key
   already bound takes the new action (but escape, 0x1b, gets another slot); an unknown action does
   nothing. 2 when the slots are full, else 0. */
int li_bind(uint32_t key, const char *action);
/* LIrBindings_RemoveAll 0x403dc0: every slot cleared, then escape bound to "escape". */
void li_unbind_all(void);
/* The key_config.txt part of 0x4d4a50: each line "unbindall" or "bind <key> to <action>" (words between
   spaces and tabs; lines starting with '#' or empty skipped); other lines are reported. */
void li_config_run(const char *text, size_t n);
/* 0x4d4a50: key_config.txt read, or written with the exe's default lines first when there is none.
   The port looks in the user's files (load/store), then in the game's folder (game), and keeps the
   defaults in the user's files. */
void li_config_load(uint8_t *(*load)(const char *name, size_t *size),
                    bool (*store)(const char *name, const void *data, size_t size),
                    uint8_t *(*game)(const char *name, size_t *size));

/* One frame's actions (0x403220 through 0x403b30): the bits of the bound actions held (1 << the action's
   index: "forward" 21, "walk" 32, "fire1" 38, ...), those that went down since the last frame or were
   tapped within it, and the mouse's axes ("aim_lr", "aim_ud": its move by 2 x 0.125, the y by -0.125
   with the inverted mouse). The mouse counts only when given. */
typedef struct {
    uint64_t held, down;
    float aim_lr, aim_ud;
} LiActions;
void li_actions(LiActions *a, const PlatMouse *mouse, bool invert);
