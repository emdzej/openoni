/* The in-game UI's pages (IGPG: ONiIGUI_Page_Paint 0x5029a0), the text consoles (text_console,
   ONrInGameUI_TextConsole_Display 0x504c30: a TxtC's pages in the WM's dialog 71, a page at a time;
   Next, Close; space and Escape) and the pause screen (ONrPauseScreen_Display 0x5018c0: dialog 70, the
   objective, items, weapons, diary and help pages), with the game held meanwhile; the objectives
   (objective_set), the diary (diary_page_unlock) and the prompts for what is new. docs/re/wm.md. */
#pragma once
#include "wm.h"

void igui_page_paint(const void *igpg, const WmDrawItem *item);
/* What the game does around a modal dialog (its input mode off and back). */
void igui_set_modal_hooks(void (*begin)(void), void (*end)(void));
void igui_register_commands(void);

/* ONiInGameUI_LevelLoad 0x503eb0, the pause screen's part: its data cleared, the level's objectives
   (the OPge of that level) and its diary pages. */
void igui_level_load(int level);
/* ONrInGameUI_Update 0x504780, the prompt: a new objective or diary page announced (a sound and the
   message), at once when it comes and again every 1800 ticks while unread. time: the game time. */
void igui_update(uint32_t time);
/* ONrInGameUI_NewWeapon 0x505050 (the ONWC; its message) and 0x5050a0 (a powerup type): the pause
   screen's pages to show as new. */
void igui_new_weapon(const void *onwc);
void igui_new_item(int type);
/* ONrInGameUI_NotifyRestoreGame 0x5050b0: what was new forgotten; for 300 ticks from time no new
   objective is announced. */
void igui_notify_restore(uint32_t time);
/* ONrPauseScreen_Display 0x5018c0: the pause screen, until closed (Close, or the pause key again a
   second after it opened). pause_key: whether the pause key went down this frame. */
void igui_pause_screen(bool (*pause_key)(void));
/* 0x5ed099: the pause screen's help page up (the HUD's help overlays: not ported yet). */
bool igui_help_shown(void);
