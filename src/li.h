/* The input layer's keys (LI), as the WM gets them: the Windows build's window procedure (0x50f7a0) turns
   WM_KEYDOWN of Tab, the page keys, the arrows, Insert, Delete and F1 to F12 into LI codes by the
   virtual-key table at 0x5293b8 (in the exe, read at run time), and every other key into its character
   (WM_CHAR). The platform gives DirectInput codes: here they become the virtual keys and the characters
   Windows would make of them (a US keyboard). */
#pragma once
#include <stdint.h>

/* The LI codes of the keys pressed this frame. */
int li_key_presses(uint8_t *codes, int cap);
