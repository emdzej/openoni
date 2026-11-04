/* The out-of-game UI: see outgame.h. */
#include "outgame.h"

#include "exe.h"
#include "persist.h"
#include "tm.h"
#include "vfs.h"
#include "wm.h"

#include <stdio.h>

/* The yes-or-no dialogs (0x4d2aa0): a button ends them with its id. */
static bool choice_proc(WmWindow *d, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    (void)p2;
    if (msg != WM_COMMAND) return false;
    if ((p1 >> 16) == 1) wm_dialog_end(d, (uint16_t)p1);
    return true;
}

typedef struct { bool in_level; int choice; uint32_t start; } Menu;

/* 0x4217b0: the level's file installed (the original's table of them, 0x56ecd8, made at start-up). */
static bool installed(int level)
{
    char rel[64];
    snprintf(rel, sizeof rel, "GameDataFolder/level%d_Final.dat", level);
    return vfs_exists(rel);
}

/* OWrLevelList_Initialize 0x4be9d0: the list (item 100) of the levels (ONLD: +0 the number, +4 the
   name) installed and reached, each with its continues ("%s" of the name) under it; the data the
   level, or the level with the continue's slot in the high byte; the last place played selected. */
static void level_list(WmWindow *d)
{
    WmWindow *list = wm_item(d, 100);
    void *all[0x400];
    int n = tm_instance_list(TM_TAG("ONLD"), all, 0x400);
    if (!list) return;
    int place_level, place_slot;
    persist_place(&place_level, &place_slot);
    for (int i = 0; i < n; i++) {
        const uint8_t *ld = all[i];
        int level = ld[0] | ld[1] << 8;
        if (!installed(level) || !persist_level_reached(level)) continue;
        uintptr_t k = wm_send(list, 0x45, 0, (uintptr_t)(ld + 4));
        wm_send(list, 0x51, k, (uintptr_t)level);
        for (int slot = 0; slot < PERSIST_CONTINUES; slot++) {
            const uint8_t *c = persist_continue(level, slot);
            if (!c) continue;
            char name[0x41];
            snprintf(name, sizeof name, "%s", (const char *)c);
            k = wm_send(list, 0x45, 0, (uintptr_t)name);
            wm_send(list, 0x51, k, (uintptr_t)(level | slot << 8));
            if (place_level == level && place_slot == slot) wm_send(list, 0x53, k, 0);
        }
    }
}

/* The load game dialog (0x4d2ba0): Load (or a double click on a line) ends it with the selected line's
   data, Cancel with 0. */
static bool load_proc(WmWindow *d, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    (void)p2;
    if (msg == WM_INITDIALOG) {
        level_list(d);
        WmWindow *list = wm_item(d, 100);
        if (list) wm_send(list, WM_SETFOCUS, 0, 0);    /* (WMrWindow_SetFocus 0x4755c0) */
        return true;
    }
    if (msg != WM_COMMAND) return false;
    uint16_t id = (uint16_t)p1, notice = (uint16_t)(p1 >> 16);
    if ((id == 1 && notice == 1) || (id == 100 && notice == 2)) {
        WmWindow *list = wm_item(d, 100);
        uint32_t v = list ? (uint32_t)wm_send(list, 0x47, 0xffffffffu, 0) : 0xffffffffu;
        if ((v & 0xffff) != 0xffff) wm_dialog_end(d, v & 0xffff);
    } else if (id == 2 && notice == 1)
        wm_dialog_end(d, 0);
    return true;
}

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
    case 101: {     /* Load Game: a level (OWrGame_Start 0x4beb40 with the line's data) */
        uint32_t v = sub_dialog(0x97, load_proc);
        if (v) m->choice = OUTGAME_LOAD, m->start = v, wm_dialog_end(d, 0);
        break;
    }
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

int outgame_main_menu(bool in_level, uint32_t *start)
{
    Menu m = { in_level, in_level ? OUTGAME_RESUME : OUTGAME_QUIT, 0 };
    const void *old = wm_psui_active(), *ui = wm_psui_get(exe_str(0x54c898));
    if (ui) wm_psui_set_active(ui);
    wm_set_desktop_background(wm_partspec_by_type(1));     /* PSrPartSpec_LoadByType(1) */
    wm_take_input(true);        /* WMrActivate; LIrMode_Set(0) */
    wm_dialog_modal(0x96, menu_proc, (uintptr_t)&m);
    wm_take_input(false);
    wm_set_desktop_background(wm_partspec_by_type(0));
    wm_psui_set_active(old);
    if (start) *start = m.start;
    return m.choice;
}
