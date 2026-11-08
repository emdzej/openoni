/* The in-game UI's pages and text consoles: see igui.h. */
#include "igui.h"

#include "cheat.h"
#include "chars.h"
#include "exe.h"
#include "persist.h"
#include "platform.h"
#include "sl.h"
#include "sound.h"
#include "text.h"
#include "tm.h"
#include "ui.h"
#include "weapon.h"
#include "wm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* The text states (0x5ed020: a stack of 5, 0x5ed0b0 the top): what a page's strings are drawn in. */
static WmFont states[5];
static int top;

/* 0x5028d0: a state pushed over the top one, with the fields the IGSt's or IGPG's header sets (its flags
   at +0x0e: 1 the family, 2 the style, 4 the colour, 8 the size), and made the DC's. */
static void push(const uint8_t *h)
{
    if (top + 1 == 5) return;
    states[top + 1] = states[top];
    top++;
    uint16_t set = rd16(h + 0x0e);
    if (set & 1) states[top].family = tm_link(h);
    if (set & 8) states[top].size = rd16(h + 0x0c);
    if (set & 2) states[top].style = rd32(h + 4);
    if (set & 4) states[top].colour = rd32(h + 8);
    wm_dc_set_font(&states[top]);
}

static void pop(void)
{
    if (top) top--;
    wm_dc_set_font(&states[top]);
}

/* The strings of an IGSA, each under the last, each in its own state. */
static void strings(WmDc *dc, const uint8_t *igsa, int16_t pen[2])
{
    pen[1] = (int16_t)(pen[1] + wm_dc_ascending());
    for (uint32_t i = 0; i < rd32(igsa + 0x14); i++) {
        const uint8_t *s = tm_link(igsa + 0x18 + 4 * i);
        if (!s) continue;
        push(s);
        int16_t out[4];
        wm_dc_text(dc, NULL, pen, (const char *)s + 0x10, out);
        pen[1] = (int16_t)(pen[1] + out[3] - out[1]);
        pop();
    }
}

/* ONiIGUI_Page_Paint 0x5029a0: the page in its header's state. The item 110 (0x6e): its image (a PSpc's
   middle column, the parts 0x49, as tall as its top left part; a texture centred at its size), 10 below
   it the first IGSA's strings; the item 111 (0x6f): the second IGSA's. */
void igui_page_paint(const void *igpg, const WmDrawItem *item)
{
    const uint8_t *pg = igpg;
    if (!pg) return;
    push(pg);
    int16_t pen[2] = { 0, 0 };
    if (item->id == 0x6e) {
        const void *image = tm_link(pg + 0x10);
        if (image) {
            const TmInstance *in = tm_handle_instance(tm_handle_of(image));
            uint32_t tag = in && in->tmpl ? in->tmpl->tag : 0;
            if (tag == TM_TAG("PSpc")) {
                int16_t h;
                wm_part_size(image, NULL, &h);
                wm_dc_part(item->dc, image, 0x49, 0, 0, item->item->w, h, 0xff);
                pen[1] = (int16_t)(pen[1] + h);
            } else if (tag == TM_TAG("TXMP")) {
                int16_t tw = (int16_t)rd16((const uint8_t *)image + 0x84), th = (int16_t)rd16((const uint8_t *)image + 0x86);
                wm_dc_texture(item->dc, image, (int16_t)((item->item->w - tw) >> 1), pen[1], -1, -1, false, 0xff);
                pen[1] = (int16_t)(pen[1] + th);
            }
            pen[1] = (int16_t)(pen[1] + 10);
        }
        const uint8_t *igsa = tm_link(pg + 0x14);
        if (igsa) strings(item->dc, igsa, pen);
    } else if (item->id == 0x6f) {
        const uint8_t *igsa = tm_link(pg + 0x18);
        if (igsa) strings(item->dc, igsa, pen);
    }
    pop();
}

/* The console's dialog's data (the TxtC, the page). */
typedef struct { const uint8_t *txtc; uint32_t page; } Console;

static uint32_t page_count(const Console *c)
{
    const uint8_t *igpa = tm_link(c->txtc);
    return igpa ? rd32(igpa + 0x14) : 0;
}

/* Next (102) while pages follow, Close (2) on the last. */
static void buttons(WmWindow *d, const Console *c)
{
    bool more = (int32_t)c->page < (int32_t)page_count(c) - 1;
    wm_set_visible(wm_item(d, 0x66), more);
    wm_set_visible(wm_item(d, 2), !more);
}

/* The text console's dialog (0x5049a0). */
static bool console_proc(WmWindow *d, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    Console *c = (Console *)wm_dialog_param(d);
    switch (msg) {
    case WM_INITDIALOG:
        if (!c) {
            wm_dialog_end(d, 0);
            return true;
        }
        c->page = 0;
        buttons(d, c);
        return true;
    case WM_COMMAND:
        if ((uint16_t)p1 == 2) wm_dialog_end(d, 0);
        else if ((uint16_t)p1 == 0x66 && ++c->page >= page_count(c)) {
            c->page = page_count(c) ? page_count(c) - 1 : 0;
            wm_dialog_end(d, 0);
        }
        buttons(d, c);
        if ((uint16_t)p1 == 2) wm_dialog_end(d, 0);
        return true;
    case WM_KEYDOWN: {
        /* Escape is Close's, space Next's (on the last page too, where it ends the console); others
           are a command of no item */
        WmWindow *b = p1 == 0x1b ? wm_item(d, 2) : p1 == 0x20 ? wm_item(d, 0x66) : NULL;
        wm_send(d, WM_COMMAND, (uintptr_t)(b ? b->id : 0) | 0x10000, (uintptr_t)b);
        return true;
    }
    case WM_DRAWITEM: {
        if (p1 != 0x6e) return false;
        const WmDrawItem *item = (const WmDrawItem *)p2;
        const uint8_t *igpa = tm_link(c->txtc);
        top = 0;
        states[0] = item->item->font;
        wm_dc_set_font(&states[0]);
        wm_dc_set_format(1);
        if (igpa && c->page < rd32(igpa + 0x14)) igui_page_paint(tm_link(igpa + 0x18 + 4 * c->page), item);
        return true;
    }
    }
    return false;
}

static void (*modal_begin)(void), (*modal_end)(void);
void igui_set_modal_hooks(void (*begin)(void), void (*end)(void)) { modal_begin = begin, modal_end = end; }

/* ---- the objectives, the diary and the pause screen (Oni_InGameUI.c) ---- */

enum { PAGE_OBJECTIVE = 1, PAGE_ITEMS, PAGE_WEAPONS, PAGE_DIARY, PAGE_HELP };
enum { ITEM_MAIN = 0x6e, ITEM_SUB = 0x6f };

/* The pause screen's data (ONgPauseScreenData, 0x5ed0f8; cleared at each level's load). */
static struct {
    int page;                           /* +0x00: PAGE_* */
    uint32_t flags;                     /* +0x04: 1 << page, something new on it */
    uint32_t open_until;                /* +0x08: the pause key closes it after this (sixtieths) */
    const uint8_t *objective;           /* +0x10: the level's OPge */
    int16_t current, objective_page;    /* +0x14 (the page drawn green; 0, the newest), +0x16 */
    const uint8_t *diary[60];           /* +0x18: DPge */
    int16_t diary_count, diary_page;    /* +0x108, +0x10a */
    const uint8_t *items[10];           /* +0x10c: IPge */
    int16_t item_count, item_page;      /* +0x134, +0x136 */
    const uint8_t *weapons[15];         /* +0x138: WPge */
    int16_t weapon_count, weapon_page;  /* +0x174, +0x176 */
    const uint8_t *help[10];            /* +0x178: HPge */
    int16_t help_count, help_page;      /* +0x1a0, +0x1a2 */
} ps;

static int16_t level_number;            /* 0x5ed104 */
static int16_t objectives_shown;        /* 0x5ecfb0: objective_set's page count */
static int16_t diary_unlocked;          /* 0x5ed002: diary_page_unlock's page of this level */
static bool objective_new, diary_new;   /* 0x5ed098, 0x5ed090: not read yet */
static bool prompt_now;                 /* 0x5ed000: announce at once */
static bool help_on;                    /* 0x5ed099: the help page up (the HUD's help overlays) */
static bool suppress_prompt;            /* 0x5ed0a8: ui_suppress_prompt */
static uint32_t prompt_time;            /* 0x5ecff4 */
static uint32_t restored_time = 0xffffffffu;    /* 0x5ecfe4 */
static const void *new_weapon;          /* 0x5ecfe0: an ONWC */
static int new_item = -1;               /* 0x5ecfec: a powerup type */
static const uint8_t *key_icons;        /* 0x5ed0d8: KeyI "keyicons" */

static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }

static int cmp_level(const void *a, const void *b)     /* 0x503c50 */
{
    return rds16(*(const uint8_t *const *)a) - rds16(*(const uint8_t *const *)b);
}

static int cmp_page(const void *a, const void *b)      /* 0x503c70 */
{
    return rds16(*(const uint8_t *const *)a + 2) - rds16(*(const uint8_t *const *)b + 2);
}

/* 0x5039f0: the diary's pages: every page of the levels before this one, and this one's up to page
   (-1: all), by level and then by page; one this level's unlocking makes new announces itself (its
   byte +4); the page shown the unlocked one, else this level's first. */
static void diary_load(int16_t page)
{
    int16_t old = diary_unlocked;
    diary_unlocked = page;
    ps.diary_count = ps.diary_page = 0;
    void *all[0x400];
    int n = tm_instance_list(TM_TAG("DPge"), all, 0x400);
    for (int i = 0; i < n && ps.diary_count < 60; i++) {
        const uint8_t *d = all[i];
        int16_t lv = rds16(d), pg = rds16(d + 2);
        if (lv < level_number || (lv == level_number && (page == -1 || pg <= page))) ps.diary[ps.diary_count++] = d;
    }
    qsort(ps.diary, (size_t)ps.diary_count, sizeof ps.diary[0], cmp_level);
    for (int i = 0; i < ps.diary_count;) {
        int k = i;
        while (k < ps.diary_count && rds16(ps.diary[k]) == rds16(ps.diary[i])) k++;
        if (k - i > 1) qsort(ps.diary + i, (size_t)(k - i), sizeof ps.diary[0], cmp_page);
        i = k;
    }
    int16_t want = page == -1 ? 1 : page, first = -1, exact = -1;
    for (int16_t i = 0; i < ps.diary_count; i++) {
        const uint8_t *d = ps.diary[i];
        if (restored_time == 0xffffffffu && d[4] && rds16(d) == level_number && old < rds16(d + 2))
            diary_new = prompt_now = true;
        if (first == -1 && level_number <= rds16(d)) first = i;
        if (rds16(d) == level_number && rds16(d + 2) == want) exact = i;
    }
    ps.diary_page = exact != -1 ? exact : first != -1 ? first : 0;
}

bool igui_help_shown(void) { return help_on; }

static void hud_arcs_reset(void);

void igui_level_load(int level)
{
    memset(&ps, 0, sizeof ps);
    cheat_message_clear();      /* 0x5ed00c */
    objectives_shown = diary_unlocked = 0;
    help_on = objective_new = diary_new = prompt_now = suppress_prompt = false;
    ui_set_objective_unread(false);
    restored_time = 0xffffffffu;
    new_weapon = NULL;
    new_item = -1;
    prompt_time = 0;
    level_number = (int16_t)level;
    void *all[0x400];
    int n = tm_instance_list(TM_TAG("OPge"), all, 0x400);
    for (int i = 0; i < n; i++)
        if (rds16((const uint8_t *)all[i] + 2) == level_number) {
            ps.objective = all[i];
            break;
        }
    diary_load(-1);
    key_icons = tm_instance_get(TM_TAG("KeyI"), "keyicons");
    hud_arcs_reset();
}

/* ---- the HUD's bars and arcs ---- */

static IguiHud hud;     /* the bars and the arcs' on flags kept from level to level */

/* ONrInGameUI_SetArcPoints 0x504270's angles (at the level's load): each arc full. */
static void hud_arcs_reset(void)
{
    static const float PI = 3.1415927f, HALF = 1.5707964f;
    for (int i = 0; i < 4; i += 2) {
        hud.health[i].a0 = PI, hud.health[i].a1 = -HALF;
        hud.health[i + 1].a0 = -HALF, hud.health[i + 1].a1 = -2.9670596f;     /* 0xc03de44e */
    }
    for (int i = 0; i < 2; i++) hud.shield[i].a0 = 0.034906585f, hud.shield[i].a1 = 1.553343f;
}

/* ONiInGameUI_UpdateDamageMeter 0x5048e0: a bar toward value; a target over it rises (the part shown is
   the target), else a fall shows the old value going down by rate a tick until it reaches the new. */
static void bar_update(IguiBar *b, uint32_t value, uint32_t target, float rate)
{
    if (b->down) {
        b->shown -= rate;
        if (b->shown <= (float)b->value) b->down = false;
        b->dirty = true;
    }
    if (target > value) {
        b->down = false;
        b->up = true;
        if (b->target != target) {
            b->target = target;
            b->shown = (float)target;
            b->dirty = true;
        }
    } else {
        b->target = value;
        if (b->up) {
            b->up = false;
            b->dirty = true;
        }
    }
    if (value > b->value) {
        if (b->down) b->down = false;
        b->dirty = true;
    } else if (value < b->value) {
        if (!b->down && !b->up) {
            b->shown = (float)b->value;
            b->down = true;
        }
        b->dirty = true;
    }
    b->value = value;
}

static float minf(float a, float b) { return a < b ? a : b; }

const IguiHud *igui_hud(const Character *c)
{
    static const float PI = 3.1415927f, HALF = 1.5707964f, TURN = 4.712389f, OVER = 1.3962634f;
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    float cap = 2.0f;
    if (gs) memcpy(&cap, gs, 4);    /* the health's cap, of the maximum */
    IguiBar *b = &hud.health_bar;
    IguiArc *h = hud.health;
    if (ui_filled(0x1)) {
        /* ui_fill_element health: the two arcs full, the eased parts off; set again after */
        h[0] = (IguiArc){ true, PI, -HALF };
        h[1] = (IguiArc){ true, -HALF, -2.9670596f };
        h[2].on = h[3].on = false;
        b->dirty = true;
    } else if (b->dirty && c->max_hit_points) {
        /* DisplayHealth: the health from pi round by 3 pi / 2 of its part, the overhealth from -pi/2
           by 1.396 of its part, up to the cap; the eased parts beyond them: the health's to the part
           shown (none while over the maximum), the overhealth's from the overhealth's end */
        float max = (float)c->max_hit_points, f = 0.0f, over = 0.0f;
        if (b->value == 0) {
            h[0].on = h[1].on = false;
        } else {
            f = (float)b->value / max;
            h[0] = (IguiArc){ true, PI, minf(f, 1.0f) * -TURN + PI };
            if (f > 1.0f) {
                over = minf(f, cap) - 1.0f;
                h[1] = (IguiArc){ true, -HALF, over * -OVER - HALF };
            } else {
                h[1].on = false;
            }
        }
        if (b->down || b->up) {
            float s = b->shown / max;
            if (f > 1.0f) h[2].on = false;
            else h[2] = (IguiArc){ true, PI - f * TURN, PI - minf(s, 1.0f) * TURN };
            if (s > 1.0f) h[3] = (IguiArc){ true, over > 0.0f ? -HALF - over * OVER : -HALF,
                                            (minf(s, cap) - 1.0f) * -OVER - HALF };
            else h[3].on = false;
        } else {
            h[2].on = h[3].on = false;
        }
        b->dirty = false;
    }
    b = &hud.shield_bar;
    h = hud.shield;
    if (ui_filled(0x8)) {
        h[0] = (IguiArc){ true, 0.034906585f, 1.553343f };
        h[1].on = false;
        b->dirty = true;
    } else if (b->dirty) {
        /* DisplayShield: from 0.035 by 1.518 of the part left (of 100), the eased part beyond it */
        static const float START = 0.034906585f, SPAN = 1.5184364f;
        float frac = 0.0f;
        if (b->value == 0) h[0].on = false;
        else {
            frac = (float)b->value * 0.01f;
            h[0] = (IguiArc){ true, START, minf(frac, 1.0f) * SPAN + START };
        }
        if (b->up || b->down) h[1] = (IguiArc){ true, frac * SPAN + START, minf(b->shown * 0.01f, 1.0f) * SPAN + START };
        else h[1].on = false;
        b->dirty = false;
    }
    return &hud;
}

void igui_update(Character *player, uint32_t time)
{
    if (!player) return;
    /* the hit arcs fade by 3 a tick */
    for (int i = 0; i < 4; i++) player->hit_flash[i] = player->hit_flash[i] < 3 ? 0 : (uint8_t)(player->hit_flash[i] - 3);
    bar_update(&hud.health_bar, player->hit_points, player->hit_points + player->hypo_heal,
               (float)player->max_hit_points * 0.0055555557f);
    bar_update(&hud.shield_bar, player->shield_shown, player->shield, 0.5555556f);
    if (suppress_prompt || (!objective_new && !diary_new)) return;
    if (!prompt_now && time - prompt_time <= 0x708) return;
    const char *m = diary_new ? "new_move" : "new_objective";
    /* the prompt's sound (12 the objective's first, 13 again) unless in a cutscene or the level's
       first 30 ticks */
    if (!ui_cutscene() && time >= 0x1e) sound_event_play(prompt_now ? 12 : 13);
    if (!ui_message_shown(m)) ui_message_print(m, 0xf0);
    prompt_now = false;
    prompt_time = time;
}

void igui_new_weapon(const void *onwc)
{
    new_weapon = onwc;
    ui_message_remove("new_weapon");
    if (onwc && !ui_message_print("new_weapon", 0xf0)) plat_log("OpenOni: ### unable to find new-weapon message");
}

void igui_new_item(int type)
{
    if (new_item != 5) new_item = type;     /* the data pad's stays */
}

void igui_notify_restore(uint32_t time)
{
    objective_new = diary_new = false;
    ui_set_objective_unread(false);
    restored_time = time;
    ui_message_remove("new_objective");
}

/* objective_set page [silent] (ONiInGameUI_ObjectiveSet 0x503c90): the objectives shown up to that
   page; a change announces itself unless silent or within 300 ticks of a restore. */
static bool cmd_objective_set(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    const uint8_t *igpa = ps.objective ? tm_link(ps.objective + 4) : NULL;
    if (!igpa) {
        plat_log(ps.objective ? "OpenOni: ONiInGameUI_ObjectiveSet found NULL ONgPauseScreenData.objective->objectives"
                              : "OpenOni: ONiInGameUI_ObjectiveSet found NULL ONgPauseScreenData.objective");
        return true;
    }
    if (c < 1) return true;
    int32_t want = v[0].type == SL_FLOAT ? (int32_t)v[0].f : v[0].i;
    int16_t count = rds16(igpa + 0x14), n = (int16_t)want;
    if (count < n) n = count;
    if (n == objectives_shown) return true;
    objectives_shown = n;
    bool silent = c > 1 && v[1].type == SL_STRING && v[1].s && !strcmp(v[1].s, "silent");
    if (restored_time != 0xffffffffu && ui_time() < restored_time + 300) return true;
    if (!silent) {
        objective_new = prompt_now = true;
        ui_set_objective_unread(true);
    }
    return true;
}

/* diary_page_unlock page (0x5039d0): the diary loaded again up to this level's page. */
static bool cmd_diary_page_unlock(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c >= 1) diary_load((int16_t)(v[0].type == SL_FLOAT ? (int32_t)v[0].f : v[0].i));
    return true;
}

/* objective_complete (0x503d60): the event sound 14. */
static bool cmd_objective_complete(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    sound_event_play(14);
    return true;
}

/* 0x502100: the items' pages (IPge) of the types the player has had (5, the data pad, by having it now:
   C+0x1b4), one of each type, at most 10; the new item's shown first. */
static void items_load(void)
{
    ps.item_count = ps.item_page = 0;
    void *all[0x400];
    int n = tm_instance_list(TM_TAG("IPge"), all, 0x400);
    const Chr *p = chars_player();
    uint32_t done = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *g = all[i];
        uint32_t type = rd32(g);
        if (done & (1u << (type & 0x1f))) continue;
        bool had = type == 5 ? (p && p->c.lsi) : persist_item_seen((int)type);
        if (had) {
            if ((int)type == new_item) ps.item_page = ps.item_count;
            ps.items[ps.item_count++] = g;
            done |= 1u << (type & 0x1f);
        }
        if (ps.item_count > 9) break;
    }
}

/* 0x502250: the weapons' pages (WPge) of the classes the player has had, at most 15; shown first the new
   weapon's, else the one in the hand. */
static void weapons_load(void)
{
    ps.weapon_count = ps.weapon_page = 0;
    void *all[0x400];
    int n = tm_instance_list(TM_TAG("WPge"), all, 0x400);
    for (int i = 0; i < n; i++) {
        const uint8_t *onwc = tm_link(all[i]);
        if (!onwc || !persist_weapon_seen(rd16(onwc + 0x136))) continue;
        if (ps.weapon_count > 14) break;
        ps.weapons[ps.weapon_count++] = all[i];
    }
    const void *want = new_weapon;
    if (!want) {
        /* the hand's (C+0x194, one of the three slots: the first held) */
        const Chr *p = chars_player();
        if (!p || !p->c.weapon) return;
        want = p->c.weapon->cls;
    }
    for (int16_t i = 0; i < ps.weapon_count; i++)
        if (tm_link(ps.weapons[i]) == want) {
            ps.weapon_page = i;
            return;
        }
}

/* 0x5023c0: the help's pages (HPge), by their +2 (0x503c70's order). */
static void help_load(void)
{
    ps.help_count = ps.help_page = 0;
    void *all[0x400];
    int n = tm_instance_list(TM_TAG("HPge"), all, 0x400);
    for (int i = 0; i < n && ps.help_count < 10; i++) ps.help[ps.help_count++] = all[i];
    qsort(ps.help, (size_t)ps.help_count, sizeof ps.help[0], cmp_page);
}

/* 0x501ef0: the page's arrows and the sub area shown (not for the help), the help's own (0x70, 0x71)
   never. */
static void show_arrows(WmWindow *d, bool on)
{
    wm_set_visible(wm_item(d, 0x6a), on);
    wm_set_visible(wm_item(d, 0x6b), on);
    wm_set_visible(wm_item(d, ITEM_SUB), on);
    wm_set_visible(wm_item(d, 0x78), on);
    wm_set_visible(wm_item(d, 0x70), false);
    wm_set_visible(wm_item(d, 0x71), false);
}

/* 0x501fa0: Previous and Next enabled while there are pages that way (the objectives' Previous goes to
   older ones: the index up). */
static void enable_arrows(WmWindow *d)
{
    bool prev = true, next = true;
    int16_t count = 0, at = 0;
    switch (ps.page) {
    case PAGE_OBJECTIVE: {
        const uint8_t *igpa = ps.objective ? tm_link(ps.objective + 4) : NULL;
        if (!igpa) {
            prev = next = false;
            break;
        }
        int16_t c = rds16(igpa + 0x14), n = objectives_shown;
        if (c <= n) n = c;
        if (n < 0) n = 0;
        else if (n > c) n = c;
        at = ps.objective_page;
        next = at != 0;
        prev = at + 1 < n && objectives_shown != 0;
        break;
    }
    case PAGE_ITEMS: count = ps.item_count, at = ps.item_page; break;
    case PAGE_WEAPONS: count = ps.weapon_count, at = ps.weapon_page; break;
    case PAGE_DIARY: count = ps.diary_count, at = ps.diary_page; break;
    default: break;
    }
    if (ps.page >= PAGE_ITEMS && ps.page <= PAGE_DIARY) {
        if (!count) prev = next = false;
        else {
            if (at == 0) prev = false;
            if (at + 1 >= count) next = false;
        }
    }
    wm_set_enabled(wm_item(d, 0x6a), prev);
    wm_set_enabled(wm_item(d, 0x6b), next);
}

/* 0x502d30 (Previous) and 0x502e20 (Next): a page back and forward; the objectives' back is an older
   one (up to the last shown), forward a newer. */
static void page_step(int dir)
{
    int16_t *at = NULL, count = 0;
    switch (ps.page) {
    case PAGE_OBJECTIVE:
        if (dir < 0) {
            const uint8_t *igpa = tm_link(ps.objective + 4);
            int16_t c = rds16(igpa + 0x14), n = objectives_shown;
            if (c <= n) n = c;
            if (n < 0) n = 0;
            else if (n > c) n = c;
            if (n <= ++ps.objective_page) ps.objective_page = (int16_t)(n - 1);
        } else if (--ps.objective_page < 0)
            ps.objective_page = 0;
        return;
    case PAGE_ITEMS: at = &ps.item_page, count = ps.item_count; break;
    case PAGE_WEAPONS: at = &ps.weapon_page, count = ps.weapon_count; break;
    case PAGE_DIARY: at = &ps.diary_page, count = ps.diary_count; break;
    case PAGE_HELP: at = &ps.help_page, count = ps.help_count; break;
    default: return;
    }
    if (dir < 0) {
        if (--*at < 0) *at = 0;
    } else if (count <= ++*at)
        *at = (int16_t)(count - 1);
}

/* text_console name (ONrInGameUI_TextConsole_Display 0x504c30): the TxtC's pages in the text console's
   dialog (71), in the game's look (the PSUI "psui_oniUI", 0x54c898), the game's input off meanwhile. */
static bool cmd_text_console(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c != 1 || v[0].type != SL_STRING || !v[0].s) return true;
    Console con = { tm_instance_get(TM_TAG("TxtC"), v[0].s), 0 };
    if (!con.txtc) return true;
    char m[96];
    snprintf(m, sizeof m, "OpenOni: text console %s", v[0].s);
    plat_log(m);
    const void *old = wm_psui_active();
    const void *ui = wm_psui_get(exe_str(0x54c898));
    if (ui) wm_psui_set_active(ui);
    if (modal_begin) modal_begin();     /* LIrMode_Set(0) */
    wm_take_input(true);
    uint32_t from = plat_frames();
    wm_dialog_modal(0x47, console_proc, (uintptr_t)&con);
    snprintf(m, sizeof m, "OpenOni: text console closed after %u frames", (unsigned)(plat_frames() - from));
    plat_log(m);
    wm_take_input(false);
    if (modal_end) modal_end();
    wm_psui_set_active(old);
    return true;
}

void igui_register_commands(void)
{
    sl_register("text_console", cmd_text_console, NULL);
    sl_register("objective_set", cmd_objective_set, NULL);
    sl_register("diary_page_unlock", cmd_diary_page_unlock, NULL);
    sl_register("objective_complete", cmd_objective_complete, NULL);
    sl_register_bool("ui_suppress_prompt", &suppress_prompt);
}

/* ONiIGUI_State_Push 0x5028d0 with a state of only a colour (flag 4). */
static void push_colour(uint32_t argb)
{
    uint8_t h[16] = { 0 };
    h[8] = (uint8_t)argb, h[9] = (uint8_t)(argb >> 8), h[10] = (uint8_t)(argb >> 16), h[11] = (uint8_t)(argb >> 24);
    h[0xe] = 4;
    push(h);
}

/* ONiPS_Paint_Objective 0x502bf0: read now (the prompt's message goes); the page shown counted from the
   newest, the current one (+0x14) green, the older red. */
static void paint_objective(const WmDrawItem *item)
{
    ps.flags &= ~2u;
    objective_new = false;
    ui_set_objective_unread(false);
    ui_message_remove("new_objective");
    const uint8_t *igpa = ps.objective ? tm_link(ps.objective + 4) : NULL;
    if (!objectives_shown || !igpa) return;
    int16_t count = rds16(igpa + 0x14);
    if (ps.objective_page < 0 || ps.objective_page >= count || !tm_link(igpa + 0x18 + 4 * ps.objective_page)) return;
    push_colour(ps.objective_page == ps.current ? 0xff00ff00u : 0xffff0000u);
    int k = objectives_shown - ps.objective_page - 1;
    if (k < 0) k = 0;
    if (k < count) igui_page_paint(tm_link(igpa + 0x18 + 4 * k), item);
    pop();
}

/* ONiIGUI_Page_PaintOverlay 0x5024c0, the diary's page: read now (recorded as the furthest, 0x50f5d0);
   in the main area its strings and then its picture centred, in the sub area its other strings and
   then the move's keys, 34 apart, centred along the top: the key icons (KeyI) by the entry's low byte
   (1 punch, 2 kick, 4 forward, 8 back, 0x10 left, 0x20 right, 0x40 crouch, 0x60 jump; 0x100 the plus),
   at half size with 0x2000, with the hold icon over it for 0x1000. */
static void paint_diary(const WmDrawItem *item)
{
    if (ps.diary_page < 0 || ps.diary_page >= ps.diary_count) return;
    const uint8_t *d = ps.diary[ps.diary_page];
    ps.flags &= ~0x10u;
    diary_new = false;
    persist_diary_read((uint32_t)(int32_t)rds16(d), (uint32_t)(int32_t)rds16(d + 2));
    const uint8_t *pg = tm_link(d + 0x38);
    if (pg) {
        push(pg);
        int16_t pen[2] = { 0, 0 };
        if (item->id == ITEM_MAIN) {
            const uint8_t *igsa = tm_link(pg + 0x14);
            if (igsa) strings(item->dc, igsa, pen);
            const void *image = tm_link(pg + 0x10);
            const TmInstance *in = image ? tm_handle_instance(tm_handle_of(image)) : NULL;
            if (in && in->tmpl && in->tmpl->tag == TM_TAG("TXMP")) {
                int16_t tw = (int16_t)rd16((const uint8_t *)image + 0x84), th = (int16_t)rd16((const uint8_t *)image + 0x86);
                wm_dc_texture(item->dc, image, (int16_t)((item->item->w - tw) >> 1), (int16_t)((item->item->h - th) >> 1), -1, -1,
                              false, 0xff);
            }
        } else if (item->id == ITEM_SUB) {
            const uint8_t *igsa = tm_link(pg + 0x18);
            if (igsa) strings(item->dc, igsa, pen);
        }
        pop();
    }
    if (item->id != ITEM_SUB || !key_icons) return;
    int n = 0;
    while (n < 12 && rd32(d + 8 + 4 * n)) n++;
    int16_t x = (int16_t)((item->item->w - n * 0x22) >> 1), y = 5;
    for (int i = 0; i < 12; i++) {
        uint32_t e = rd32(d + 8 + 4 * i);
        if (!e) return;
        int k = -1;
        switch (e & 0xff) {
        case 0x01: k = 0; break;
        case 0x02: k = 1; break;
        case 0x04: k = 2; break;
        case 0x08: k = 3; break;
        case 0x10: k = 4; break;
        case 0x20: k = 5; break;
        case 0x40: k = 6; break;
        case 0x60: k = 7; break;
        }
        if (e == 0x100) k = 9;
        const uint8_t *tex = k >= 0 ? tm_link(key_icons + 4 * k) : NULL;
        if (tex) {
            if (e & 0x2000) {
                int16_t tw = (int16_t)(rd16(tex + 0x84) >> 1), th = (int16_t)(rd16(tex + 0x86) >> 1);
                wm_dc_texture(item->dc, tex, x, (int16_t)(y + th), tw, th, true, 0xff);
            } else
                wm_dc_texture(item->dc, tex, x, y, -1, -1, false, 0xff);
        }
        if ((e & 0x1000) && (tex = tm_link(key_icons + 4 * 8))) wm_dc_texture(item->dc, tex, x, y, -1, -1, false, 0xff);
        x = (int16_t)(x + 0x22);
    }
}

/* 0x502960, 0x502cc0, 0x502d00: an item's, a weapon's and the help's page (their IGPG, +4); the first
   two read now. */
static void paint_list(const WmDrawItem *item, int page)
{
    const uint8_t *g = NULL;
    if (page == PAGE_ITEMS) {
        ps.flags &= ~4u;
        new_item = -1;
        if (ps.item_count && ps.item_page >= 0 && ps.item_page < ps.item_count) g = ps.items[ps.item_page];
    } else if (page == PAGE_WEAPONS) {
        ps.flags &= ~8u;
        new_weapon = NULL;
        if (ps.weapon_count && ps.weapon_page >= 0 && ps.weapon_page < ps.weapon_count) g = ps.weapons[ps.weapon_page];
    } else if (ps.help_page >= 0 && ps.help_page < ps.help_count)
        g = ps.help[ps.help_page];
    if (g) igui_page_paint(tm_link(g + 4), item);
}

/* 0x447c10: a to b by t (0..1, in 256ths), each channel. */
static uint32_t blend(uint32_t a, uint32_t b, float t)
{
    int i = (int)(t * 256.0f + 0.5f), j = 0x100 - i;
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) out |= ((((a >> s) & 0xff) * (uint32_t)j + ((b >> s) & 0xff) * (uint32_t)i) >> 8 & 0xff) << s;
    return out;
}

static bool (*pause_key_fn)(void);

/* ONiPauseScreen_Paint 0x502ef0: the pause key closes it a second after it opened; the page buttons'
   text white for the page up (or held down), else orange, pulsing to yellow over 60 of every 90
   sixtieths on a page with something new (the table at 0x555738: the buttons 101 to 105, the pages 1
   to 5). */
static void pause_paint(WmWindow *d)
{
    uint32_t now = plat_frames();
    if (ps.open_until < now && pause_key_fn && pause_key_fn()) wm_dialog_end(d, 0);
    uint32_t t = (now + 0xb4 - ps.open_until) % 0x5a;
    uint32_t pulse = t < 0x3c ? blend(0xffff7f00u, 0xffffff00u, sinf((float)t * 3.14159274f * 0.0166666675f)) : 0xffff7f00u;
    for (int page = PAGE_OBJECTIVE; page <= PAGE_HELP; page++) {
        WmWindow *b = wm_item(d, (uint16_t)(100 + page));
        if (!b) continue;
        bool held = b->cls && b->cls->number == 4 && wm_button_pressed(b);
        WmFont f = b->font;
        f.colour = held || page == ps.page ? 0xffffffffu : (ps.flags & (1u << page)) ? pulse : 0xffff7f00u;
        wm_set_font(b, &f);
    }
}

/* The pause screen's dialog (0x501990). */
static bool pause_proc(WmWindow *d, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    switch (msg) {
    case WM_DESTROY:
        help_on = false;
        ui_hide_areas(false);   /* 0x4303c0, 0x4303d0: the text areas back */
        return true;
    case WM_COMMAND: {
        uint16_t id = (uint16_t)p1;
        switch (id) {
        case 2: wm_dialog_end(d, 0); break;
        case 0x65: case 0x66: case 0x67: case 0x68: case 0x69: ps.page = id - 0x64; break;
        case 0x6a: case 0x70: page_step(-1); break;
        case 0x6b: case 0x71: page_step(1); break;
        default: break;
        }
        if (id > 100 && id < 0x6a) {
            wm_check_radio(d, 0x65, 0x69, id);
            bool help = ps.page == PAGE_HELP;
            show_arrows(d, !help);
            help_on = help;     /* (its text context for the HUD's help, TSrContext_New: with the overlays) */
        }
        enable_arrows(d);
        return true;
    }
    case WM_PAINT:
        pause_paint(d);
        return true;
    case WM_DRAWITEM: {
        const WmDrawItem *item = (const WmDrawItem *)p2;
        top = 0;
        states[0] = item->item->font;   /* 0x501ec0 */
        wm_dc_set_font(&states[0]);
        wm_dc_set_format(1);
        /* the sub area shows a cheat's message instead (0x5ed00c), at the font's ascent */
        if (item->id == ITEM_SUB && cheat_message()) {
            int16_t pen[2] = { 0, wm_dc_ascending() };
            wm_dc_text(item->dc, NULL, pen, cheat_message(), NULL);
            return true;
        }
        switch (ps.page) {
        case PAGE_OBJECTIVE: paint_objective(item); break;
        case PAGE_DIARY: paint_diary(item); break;
        case PAGE_ITEMS: case PAGE_WEAPONS: case PAGE_HELP: paint_list(item, ps.page); break;
        default: break;
        }
        return true;
    }
    case WM_INITDIALOG:
        items_load();
        weapons_load();
        help_load();
        ps.flags = (objective_new ? 2u : 0u) | (new_weapon ? 8u : 0u) | (new_item != -1 ? 4u : 0u) | (diary_new ? 0x10u : 0u);
        wm_check_radio(d, 0x65, 0x69, 0x65);
        ps.page = PAGE_OBJECTIVE;
        ps.open_until = plat_frames() + 0x3c;
        show_arrows(d, true);
        enable_arrows(d);
        ui_hide_areas(true);    /* 0x4303c0, 0x4303d0: the text areas hidden */
        return true;
    }
    return false;
}

void igui_pause_screen(bool (*pause_key)(void))
{
    const void *old = wm_psui_active();
    const void *ui = wm_psui_get(exe_str(0x54c898));
    if (ui) wm_psui_set_active(ui);
    if (modal_begin) modal_begin();     /* LIrMode_Set(0) */
    wm_take_input(true);                /* WMrActivate */
    sound_impulse_play("pause_screen_open", 1.0f);
    float volume = sound_volume();
    sound_set_volume(volume * 0.5f);
    pause_key_fn = pause_key;
    plat_log("OpenOni: pause screen");
    wm_key_hook = cheat_key;    /* ONiRunGame 0x4d4640: the cheats' event hook while paused */
    wm_dialog_modal(0x46, pause_proc, 0);
    wm_key_hook = NULL;
    pause_key_fn = NULL;
    sound_set_volume(volume);
    sound_impulse_play("pause_screen_close", 1.0f);
    wm_take_input(false);
    if (modal_end) modal_end();
    wm_psui_set_active(old);
    cheat_message_clear();
}
