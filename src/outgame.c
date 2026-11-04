/* The out-of-game UI: see outgame.h. */
#include "outgame.h"

#include "exe.h"
#include "wm.h"

/* The yes-or-no dialogs (0x4d2aa0): a button ends them with its id. */
static bool choice_proc(WmWindow *d, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    (void)p2;
    if (msg != WM_COMMAND) return false;
    if ((p1 >> 16) == 1) wm_dialog_end(d, (uint16_t)p1);
    return true;
}

typedef struct { bool in_level; int choice; } Menu;

/* A dialog of the menu's, in the game's look (the PSUI "psui_oniUI", 0x54c898). */
static uint32_t sub_dialog(uint16_t id, WmDialogProc proc)
{
    const void *old = wm_psui_active(), *ui = wm_psui_get(exe_str(0x54c898));
    if (ui) wm_psui_set_active(ui);
    uint32_t r = wm_dialog_modal(id, proc, 0);
    wm_psui_set_active(old);
    return r;
}

/* The main menu's procedure (0x4d2cd0). */
static bool menu_proc(WmWindow *d, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    (void)p2;
    Menu *m = (Menu *)wm_dialog_param(d);
    if (msg == WM_INITDIALOG) {
        /* Options out of a game, Resume in one. Not ported: the menu's music (0x54c904) */
        wm_set_visible(wm_item(d, 0x66), !m->in_level);
        wm_set_visible(wm_item(d, 0x68), m->in_level);
        return true;
    }
    if (msg != WM_COMMAND) return msg == WM_DESTROY;
    if ((p1 >> 16) != 1) return true;
    switch ((uint16_t)p1) {
    case 2:         /* Escape: resumes a game */
        if (m->in_level) m->choice = OUTGAME_RESUME, wm_dialog_end(d, 0);
        break;
    case 100:       /* New Game: Start (1) or Cancel */
        if (sub_dialog(0x9a, choice_proc) == 1) m->choice = OUTGAME_NEW_GAME, wm_dialog_end(d, 0);
        break;
    case 101:       /* Load Game (0x4d2ba0): not ported */
        sub_dialog(0x97, NULL);
        break;
    case 102:       /* Options (0x4d25c0): not ported */
        sub_dialog(0x98, NULL);
        break;
    case 103:       /* Quit: Yes (1) or No */
        if (sub_dialog(0x99, choice_proc) == 1) m->choice = OUTGAME_QUIT, wm_dialog_end(d, 0);
        break;
    case 104:       /* Resume */
        m->choice = OUTGAME_RESUME, wm_dialog_end(d, 0);
        break;
    }
    return true;
}

int outgame_main_menu(bool in_level)
{
    Menu m = { in_level, in_level ? OUTGAME_RESUME : OUTGAME_QUIT };
    const void *old = wm_psui_active(), *ui = wm_psui_get(exe_str(0x54c898));
    if (ui) wm_psui_set_active(ui);
    wm_set_desktop_background(wm_partspec_by_type(1));     /* PSrPartSpec_LoadByType(1) */
    wm_take_input(true);        /* WMrActivate; LIrMode_Set(0) */
    wm_dialog_modal(0x96, menu_proc, (uintptr_t)&m);
    wm_take_input(false);
    wm_set_desktop_background(wm_partspec_by_type(0));
    wm_psui_set_active(old);
    return m.choice;
}
