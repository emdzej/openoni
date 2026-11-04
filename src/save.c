/* The continue: see save.h. */
#include "save.h"

#include "chars.h"
#include "exe.h"
#include "persist.h"
#include "physics.h"
#include "sl.h"
#include "tm.h"
#include "ui.h"
#include "weapon.h"
#include "text.h"

#include <stdio.h>
#include <string.h>

/* The continue (0x5ecc10, 0x81 words): its name ("Save Point %d"), the player's health and maximum, place
   and facing, inventory, the LSI, and the weapons by class name and ammo. */
static struct {
    char name[64];
    bool valid;                     /* 0x5ecc50 bit 1 */
    bool ignore;                    /* bit 2: start the level from its beginning all the same */
    uint32_t hp, max_hp;            /* 0x5ecc54 (with C+0x1ae, the port's 0), 0x5ecc58 */
    float pos[3], facing;           /* 0x5ecc5c, 0x5ecc68 */
    uint16_t ammo, cells, shield, invisibility, hypos;  /* 0x5ecc6c..0x5ecc7c */
    bool lsi;                       /* 0x5ecc84 */
    char weapon[64];                /* 0x5ecc88: the hand's class, empty none */
    uint16_t weapon_ammo;           /* 0x5ecd08 */
} cont;
static int32_t point;               /* 0x5ece88: the scripts' save_point */
static int level;                   /* G+0x1679d8 */

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void putf(uint8_t *p, float f) { uint32_t v; memcpy(&v, &f, 4); put32(p, v); }
static float getf(const uint8_t *p) { uint32_t v = get32(p); float f; memcpy(&f, &v, 4); return f; }

/* The continue as the original keeps it (0x204 bytes, persist.dat's): +0 the name, +0x40 the flags,
   +0x44 the health, +0x48 its maximum, +0x4c the place, +0x58 the facing, +0x5c ammo, +0x60 cells,
   +0x64 shield, +0x68 invisibility, +0x6c hypos (C+0x1c0 at +0x70 not kept), +0x74 the LSI; three
   weapons from +0x78, each a class name of 0x80 and the ammo (the port's one: the hand's, the first). */
static void to_record(uint8_t r[PERSIST_CONTINUE_SIZE])
{
    memset(r, 0, PERSIST_CONTINUE_SIZE);
    memcpy(r, cont.name, sizeof cont.name - 1);
    put32(r + 0x40, (cont.valid ? 1u : 0u) | (cont.ignore ? 2u : 0u));
    put32(r + 0x44, cont.hp);
    put32(r + 0x48, cont.max_hp);
    for (int k = 0; k < 3; k++) putf(r + 0x4c + 4 * k, cont.pos[k]);
    putf(r + 0x58, cont.facing);
    put32(r + 0x5c, cont.ammo);
    put32(r + 0x60, cont.cells);
    put32(r + 0x64, cont.shield);
    put32(r + 0x68, cont.invisibility);
    put32(r + 0x6c, cont.hypos);
    put32(r + 0x74, cont.lsi);
    memcpy(r + 0x78, cont.weapon, sizeof cont.weapon - 1);
    put32(r + 0xf8, cont.weapon_ammo);
}

static void from_record(const uint8_t r[PERSIST_CONTINUE_SIZE])
{
    memset(&cont, 0, sizeof cont);
    memcpy(cont.name, r, sizeof cont.name - 1);
    cont.valid = (get32(r + 0x40) & 1) != 0;
    cont.ignore = (get32(r + 0x40) & 2) != 0;
    cont.hp = get32(r + 0x44);
    cont.max_hp = get32(r + 0x48);
    for (int k = 0; k < 3; k++) cont.pos[k] = getf(r + 0x4c + 4 * k);
    cont.facing = getf(r + 0x58);
    cont.ammo = (uint16_t)get32(r + 0x5c);
    cont.cells = (uint16_t)get32(r + 0x60);
    cont.shield = (uint16_t)get32(r + 0x64);
    cont.invisibility = (uint16_t)get32(r + 0x68);
    cont.hypos = (uint16_t)get32(r + 0x6c);
    cont.lsi = get32(r + 0x74) != 0;
    memcpy(cont.weapon, r + 0x78, sizeof cont.weapon - 1);
    cont.weapon_ammo = (uint16_t)get32(r + 0xf8);
}

void save_level_begin(int l)
{
    level = l;
    persist_set_place(l, 0);        /* ONrGameState_LevelBegin 0x4f5350: ONrPersist_SetPlace */
    persist_set_level_reached(l);   /* 0x50efd0 */
}

bool save_use_persisted(int l, int slot)
{
    const uint8_t *r = persist_continue(l, slot);
    if (!r) return false;
    from_record(r);                 /* 0x4fdaa0 */
    point = slot;
    return true;
}

void save_clear(void)
{
    memset(&cont, 0, sizeof cont);
    point = 0;
}

int32_t save_point(void) { return point; }

/* The weapon class's name (TMrInstance_GetInstanceName), without its tag. */
static bool class_name(const void *cls, char out[64])
{
    for (int f = 0; f < tm_file_slots(); f++) {
        TmFile *file = tm_file_at(f);
        for (uint32_t i = 0; file && i < tm_file_instance_count(file); i++) {
            const TmInstance *in = tm_file_instance(file, i);
            if (in->data == cls && in->name && strlen(in->name) > 4) {
                snprintf(out, 64, "%s", in->name + 4);
                return true;
            }
        }
    }
    return false;
}

/* save_game n [autosave] (AIiScript_SaveGame 0x4bd2e0, "%s game %d"): the continue at save point n;
   an autosave shows the "autosave" message for 120 ticks. Level 1's first point is "Syndicate
   Warehouse", its others one less (the training isn't one). */
static bool cmd_save(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    int32_t n = c >= 1 ? (v[0].type == SL_FLOAT ? (int32_t)v[0].f : v[0].i) : 0;
    bool autosave = c >= 2 && v[1].type == SL_STRING && v[1].s && !strcmp(v[1].s, "autosave");
    if (autosave) {
        const char *s = text_message("autosave");
        if (s) ui_print(UI_AREA_MESSAGE, s, "autosave", 120, 0xffffffffu);
    }
    Chr *p = chars_player();
    memset(&cont, 0, sizeof cont);      /* ("invalid save point" for 0, kept all the same) */
    point = n;
    if (!p) return true;
    /* level 1's first point is "Syndicate Warehouse" (0x554988), the others one less (0x554974);
       elsewhere 0x554960 */
    if (level == 1 && n == 1) snprintf(cont.name, sizeof cont.name, "%s", exe_str(0x554988));
    else if (level == 1) snprintf(cont.name, sizeof cont.name, exe_str(0x554974), (int)n - 1);
    else snprintf(cont.name, sizeof cont.name, exe_str(0x554960), (int)n);
    cont.hp = p->c.hit_points;
    cont.max_hp = p->c.max_hit_points;
    memcpy(cont.pos, p->c.pos, sizeof cont.pos);
    cont.facing = p->c.facing;
    cont.ammo = p->c.ammo;
    cont.cells = p->c.cells;
    cont.shield = p->c.shield;
    cont.invisibility = p->c.invisibility;
    cont.hypos = p->c.hypos;
    cont.lsi = p->c.lsi;
    cont.valid = true;
    if (p->c.weapon && class_name(p->c.weapon->cls, cont.weapon)) cont.weapon_ammo = p->c.weapon->ammo;
    /* kept for the menu's Load Game (ONrPersist_SetContinue 0x50f360, ONrPersist_SetPlace) */
    uint8_t rec[PERSIST_CONTINUE_SIZE];
    to_record(rec);
    persist_set_continue(level, (int)n, rec);
    persist_set_place(level, (int)n);
    return true;
}

/* restore_game (0x4bd360 -> ONrGameState_UseContinue): with a save point, the player's health (scaled if
   the maximum changed), place, facing, inventory, the weapons gone and the saved one in the hand. */
void (*save_restore_hook)(void);

static bool cmd_restore(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    Chr *p = chars_player();
    if (!p || !point || !cont.valid || cont.ignore) {   /* "failed to continue, invalid save point", or
                                                           "valid continue but ignoring it" */
        if (save_restore_hook) save_restore_hook();
        return true;
    }
    if (p->c.max_hit_points == cont.max_hp) p->c.hit_points = cont.hp;
    else if (cont.max_hp) p->c.hit_points = (uint32_t)((uint64_t)p->c.max_hit_points * cont.hp / cont.max_hp);
    physics_place(&p->c, cont.pos);
    p->c.facing = p->c.desired_facing = cont.facing;
    p->c.ammo = cont.ammo;
    p->c.cells = cont.cells;
    p->c.shield = cont.shield;
    p->c.invisibility = cont.invisibility;
    p->c.hypos = cont.hypos;
    p->c.lsi = cont.lsi;
    if (p->c.weapon) {
        Weapon *w = p->c.weapon;
        chr_set_weapon(&p->c, NULL);
        weapon_delete(w);       /* 0x518f90 */
    }
    if (cont.weapon[0]) {
        const void *cls = tm_instance_get(TM_TAG("ONWC"), cont.weapon);
        Weapon *w = cls ? weapon_new(cls, p->c.pos, 0.0f) : NULL;  /* ONrCharacter_UseWeapon_NameAmmo */
        if (w) {
            chr_set_weapon(&p->c, w);
            w->ammo = cont.weapon_ammo;
        }
    }
    if (save_restore_hook) save_restore_hook();     /* ONrInGameUI_NotifyRestoreGame 0x5050b0 */
    return true;
}

void save_register_commands(void)
{
    sl_register_int("save_point", &point);
    sl_register("save_game", cmd_save, NULL);
    sl_register("restore_game", cmd_restore, NULL);
}
