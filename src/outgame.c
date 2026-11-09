/* The out-of-game UI: see outgame.h. */
#include "outgame.h"

#include "exe.h"
#include "particle.h"
#include "persist.h"
#include "platform.h"
#include "sound.h"
#include "tm.h"
#include "vfs.h"
#include "wm.h"
#include "ftol.h"

#include <math.h>
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

/* 0x4d2ad0: the options' items set from the persisted ones. */
static void options_show(WmWindow *d)
{
    wm_slider_set_value(wm_item(d, 100), persist_quality());
    wm_slider_set_value(wm_item(d, 110), (int32_t)lrintf(persist_gamma() * 100.0f));   /* 0x434030 */
    wm_slider_set_value(wm_item(d, 102), ftol((double)persist_volume() * 100.0));            /* 0x4264e0 */
    wm_check_set(wm_item(d, 107), persist_subtitles());
    wm_check_set(wm_item(d, 109), persist_invert_mouse());
    wm_popup_select(wm_item(d, 108), (uint16_t)persist_difficulty());
}

/* The options dialog (0x4d25c0): the quality (0..4), the gamma and the overall volume (0..100, as
   hundredths) by sliders, the resolution and the difficulty (the WMM_ pm_difficulty) by popups, the
   subtitles and the inverted mouse by check boxes, each change persisted at once. The resolution's popup
   lists the draw engine's modes ("%d x %d x %d bit", 0x54c8f0) with the active one selected; another
   chosen is persisted and Oni exits ("You must restart Oni...", 0x54c8a4). The port has one mode: it
   draws at the size of gasm's window (resized, or fullscreen with F11), so that is the mode listed, with
   the persisted depth. */
static bool options_proc(WmWindow *d, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    if (msg == WM_INITDIALOG) {
        wm_slider_set_range(wm_item(d, 100), 0, 4);
        wm_slider_set_range(wm_item(d, 110), 0, 100);
        wm_slider_set_range(wm_item(d, 102), 0, 100);
        WmWindow *res = wm_item(d, 101);
        wm_popup_clear(res);
        int w, h, depth;
        persist_resolution(&w, &h, &depth);
        plat_display_size(&w, &h);
        char mode[64];
        snprintf(mode, sizeof mode, exe_str(0x54c8f0), w, h, depth);
        wm_popup_append(res, 0, mode);
        wm_popup_select(res, 0);
        options_show(d);
        return true;
    }
    if (msg == 0x19) {      /* a popup's choice */
        const WmWindow *from = (const WmWindow *)p2;
        if (from && from->id == 0x6c) persist_set_difficulty((uint16_t)p1);
        /* 0x65, the resolution: the one mode is the active one */
        return true;
    }
    if (msg != WM_COMMAND) return false;
    if ((p1 >> 16) != 1) return true;
    WmWindow *item = (WmWindow *)p2;
    switch ((uint16_t)p1) {
    case 100:       /* 0x50f0e0 */
        persist_set_quality(wm_slider_value(item));
        p3_set_detail_from_quality();   /* 0x508ef0 */
        break;
    case 0x66:      /* 0x50f130, then SS2rVolume_Set */
        persist_set_volume((float)((double)wm_slider_value(item) * (double)0.01f));
        sound_set_volume(persist_volume());
        break;
    case 0x6b:      /* 0x50f1f0 */
        persist_set_subtitles(wm_check_get(item));
        break;
    case 0x6d:      /* 0x50f1a0 */
        persist_set_invert_mouse(wm_check_get(item));
        break;
    case 0x6e:      /* 0x50f3e0 */
        persist_set_gamma((float)((double)wm_slider_value(item) * (double)0.01f));
        break;
    case 2:
        wm_dialog_end(d, 0);
        break;
    }
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

/* ONiOGU_MainMenu_FadeMusic 0x4d3100 (and the same inline for Load Game and Quit): the music, if on,
   down to nothing over half a second and stopped, 30 ticks of the sound's updates (OSrUpdate with the
   listener at the origin, 0x483bd0), then halted. */
static void fade_music(void)
{
    if (!sound_music_on()) return;
    sound_music_fade(0.0f, 0.5f);
    sound_music_end();
    static const float origin[3] = { 0, 0, 0 };
    sound_set_listener(origin, origin);
    for (int t = 0; t < 30; t++) {
        wm_redraw();
        sound_update();
        plat_wait_frame();
    }
    sound_music_halt();
}

/* The main menu's procedure (0x4d2cd0). */
static bool menu_proc(WmWindow *d, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    (void)p2;
    Menu *m = (Menu *)wm_dialog_param(d);
    if (msg == WM_INITDIALOG) {
        /* Options out of a game, Resume in one; the level's sounds paused, the menu's music (0x54c904)
           in from nothing over half a second */
        wm_set_visible(wm_item(d, 0x66), !m->in_level);
        wm_set_visible(wm_item(d, 0x68), m->in_level);
        sound_set_script_only(true);
        sound_pause_all();
        sound_music_play(exe_str(0x54c904), 0.0f);
        sound_music_fade(1.0f, 0.5f);
        return true;
    }
    if (msg == WM_DESTROY) {
        /* the music still on (Resume, Options' way out) out over half a second; the sounds on again */
        if (sound_music_on()) {
            sound_music_fade(0.0f, 0.5f);
            sound_music_end();
        }
        sound_set_script_only(false);
        sound_resume_all();
        return true;
    }
    if (msg != WM_COMMAND) return false;
    if ((p1 >> 16) != 1) return true;
    switch ((uint16_t)p1) {
    case 2:         /* Escape: resumes a game */
        if (m->in_level) m->choice = OUTGAME_RESUME, wm_dialog_end(d, 0);
        break;
    case 100:       /* New Game: Start (1) or Cancel */
        if (sub_dialog(0x9a, choice_proc) == 1) fade_music(), m->choice = OUTGAME_NEW_GAME, wm_dialog_end(d, 0);
        break;
    case 101: {     /* Load Game: a level (OWrGame_Start 0x4beb40 with the line's data) */
        uint32_t v = sub_dialog(0x97, load_proc);
        if (v) fade_music(), m->choice = OUTGAME_LOAD, m->start = v, wm_dialog_end(d, 0);
        break;
    }
    case 102:       /* Options */
        sub_dialog(0x98, options_proc);
        break;
    case 103:       /* Quit: Yes (1) or No */
        if (sub_dialog(0x99, choice_proc) == 1) fade_music(), m->choice = OUTGAME_QUIT, wm_dialog_end(d, 0);
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
