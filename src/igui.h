/* The in-game UI's pages (IGPG: ONiIGUI_Page_Paint 0x5029a0) and the text consoles (text_console,
   ONrInGameUI_TextConsole_Display 0x504c30): a TxtC's pages in the WM's dialog 71, a page at a time
   (Next, Close; space and Escape), the game held meanwhile. docs/re/wm.md. */
#pragma once
#include "wm.h"

void igui_page_paint(const void *igpg, const WmDrawItem *item);
/* What the game does around a modal dialog (its input mode off and back). */
void igui_set_modal_hooks(void (*begin)(void), void (*end)(void));
void igui_register_commands(void);
