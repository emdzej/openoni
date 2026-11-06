/* The in-game UI's pages (IGPG: ONiIGUI_Page_Paint 0x5029a0), the text consoles (text_console,
   ONrInGameUI_TextConsole_Display 0x504c30: a TxtC's pages in the WM's dialog 71, a page at a time;
   Next, Close; space and Escape) and the pause screen (ONrPauseScreen_Display 0x5018c0: dialog 70, the
   objective, items, weapons, diary and help pages), with the game held meanwhile; the objectives
   (objective_set), the diary (diary_page_unlock) and the prompts for what is new. docs/re/wm.md. */
#pragma once
#include "character.h"
#include "wm.h"

void igui_page_paint(const void *igpg, const WmDrawItem *item);
/* What the game does around a modal dialog (its input mode off and back). */
void igui_set_modal_hooks(void (*begin)(void), void (*end)(void));
void igui_register_commands(void);

/* ONiInGameUI_LevelLoad 0x503eb0, the pause screen's part: its data cleared, the level's objectives
   (the OPge of that level) and its diary pages. */
void igui_level_load(int level);
/* ONrInGameUI_Update 0x504780, for the player (nothing without): the hit arcs faded, the health's and
   the shield's bars eased (igui_hud), then the prompt: a new objective or diary page announced (a sound
   and the message), at once when it comes and again every 1800 ticks while unread. time: the game
   time. */
void igui_update(Character *player, uint32_t time);

/* The HUD's eased bars (ONiInGameUI_UpdateDamageMeter 0x5048e0's state: the health's at 0x630fc0, the
   shield's at 0x630fe0): what it last saw, the target it rises to (healing to come), rising, falling,
   and the part it shows falling (or risen to), and whether the arcs are to be set again. */
typedef struct {
    bool dirty;         /* +0 */
    uint32_t value;     /* +4 */
    uint32_t target;    /* +8 */
    bool up, down;      /* +0xc, +0xd */
    float shown;        /* +0x10 */
} IguiBar;
typedef struct { bool on; float a0, a1; } IguiArc;
/* The right element's arcs: the health's four (0x5ed080..0x5ed08c, on at 0x630fd4..0x630fd7: the health,
   the overhealth and their eased parts) and the shield's two (0x5ecfbc, 0x5ecfc0; 0x630ff4, 0x630ff5),
   with their bars. */
typedef struct {
    IguiBar health_bar, shield_bar;
    IguiArc health[4], shield[2];
} IguiHud;
/* The arcs set again from their bars where those changed (the angles of ONiInGameUI_DisplayHealth
   0x500970 and ONiInGameUI_DisplayShield 0x501080); c: the player. */
const IguiHud *igui_hud(const Character *c);
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
